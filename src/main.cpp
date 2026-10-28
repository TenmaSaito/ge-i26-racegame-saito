#include "car.hpp"
#include "render.hpp"
#include "track.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstring>

// ブラウザ版の調整値の保存先は localStorage（ファイルはリロードで消えるため）。
EM_JS(void, tuningSaveJs, (const char* data), {
    try { localStorage.setItem('racegameDriftTuning', UTF8ToString(data)); } catch (e) {}
});
EM_JS(char*, tuningLoadJs, (), {
    try {
        const s = localStorage.getItem('racegameDriftTuning');
        if (!s) return 0;
        const n = lengthBytesUTF8(s) + 1;
        const p = _malloc(n);
        stringToUTF8(s, p, n);
        return p;
    } catch (e) { return 0; }
});
// コース選択は URL の ?track=<名前> で受ける（例: racegame.html?track=flow）。
EM_JS(char*, trackQueryJs, (), {
    try {
        const s = new URLSearchParams(location.search).get('track');
        if (!s) return 0;
        const n = lengthBytesUTF8(s) + 1;
        const p = _malloc(n);
        stringToUTF8(s, p, n);
        return p;
    } catch (e) { return 0; }
});
#endif

namespace {

constexpr double kFixedStepSec = 1.0 / 60.0;
constexpr double kMaxFrameSec = 0.25;

struct Race {
    int completed = 0;
    bool timing = false;
    float lap = 0;
    float best = -1;
    float prevS = -1;
};

struct Orbit {
    Vec3 eye{0, 8, -12};
    Vec3 look{0, 0, 0};
    bool ready = false;
    int mode = 0;
};

struct App {
    std::string title = "Suzuka Circuit";  // ウィンドウタイトル（コース名）
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    SDL_GameController* pad = nullptr;
    Renderer renderer;
    Track track;
    Car car;
    GpuMesh course;
    GpuMesh body;
    GpuMesh wheel;
    GpuMesh shadow;
    Race race;
    Orbit camera;
    bool running = true;
    bool paused = false;
    bool showHelp = true;
    bool showDriftDebug = true;  // ドリフトのデバッグ表示（B キーで切替、既定はオン）
    DriveInput lastInput;        // デバッグ表示用に直近の入力を覚えておく
    bool showTuning = false;     // ドリフトのチューニングパネル（T キーで開閉）
    int tuneIndex = 0;           // パネルで選択中の項目
    double accumulator = 0;
    Uint64 prevCounter = 0;
};

// コース名（ファイル名から .txt を除いたもの）からパスを探す。
// 既定は suzuka。ネイティブは --track <名前>、ブラウザは ?track=<名前> で切替。
std::string findTrackPath(const std::string& stem) {
    const std::string file = stem + ".txt";
    const std::string paths[] = {
        "assets/track/" + file,
        "../assets/track/" + file,
        "/assets/track/" + file,
    };
    for (const std::string& path : paths) {
        std::ifstream in(path);
        if (in) {
            return path;
        }
    }
    return paths[0];
}

// コース名はパストラバーサル防止に英数字と _- のみ許可する。
std::string sanitizeTrackStem(const char* raw) {
    std::string out;
    if (!raw) {
        return out;
    }
    for (const char* p = raw; *p && out.size() < 32; ++p) {
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out += c;
        }
    }
    return out;
}

std::string pickTrackStem(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--track") == 0) {
            std::string stem = sanitizeTrackStem(argv[i + 1]);
            if (!stem.empty()) {
                return stem;
            }
        }
    }
#ifdef __EMSCRIPTEN__
    char* p = trackQueryJs();
    if (p) {
        std::string stem = sanitizeTrackStem(p);
        std::free(p);
        if (!stem.empty()) {
            return stem;
        }
    }
#endif
    return "suzuka";
}

void formatTime(char* buf, int size, float seconds) {
    if (seconds < 0) {
        std::snprintf(buf, size, "--:--.---");
        return;
    }
    int minutes = static_cast<int>(seconds / 60.0f);
    float rest = seconds - float(minutes) * 60.0f;
    std::snprintf(buf, size, "%d:%06.3f", minutes, rest);
}

// --- ドリフトのチューニング -------------------------------------------------
// 項目テーブル。value は driftTuning の各フィールドへのポインタ。
struct TuneItem {
    const char* name;
    float* value;
    float lo;
    float hi;
    float step;
    int display;  // 0: %.2f / 1: 度 / 2: km/h / 3: %.1f
};

TuneItem tuneItems[] = {
    {"DRIFT GRIP", &driftTuning.gripDrift, 0.2f, 4.0f, 0.05f, 0},
    {"INERTIA", &driftTuning.inertia, 0.4f, 2.5f, 0.05f, 0},
    {"DRIFT DECEL", &driftTuning.driftDecel, 0.0f, 1.5f, 0.05f, 0},
    {"MAX SLIP", &driftTuning.maxSlip, 0.35f, 1.25f, 0.035f, 1},
    {"MIN SPEED", &driftTuning.minSpeed, 0.0f, 25.0f, 1.0f, 2},
    {"NORMAL GRIP", &driftTuning.gripNormal, 6.0f, 30.0f, 0.5f, 3},
    {"ENTRY", &driftTuning.entrySharp, 0.0f, 1.0f, 0.05f, 3},
};
constexpr int kTuneItemCount = static_cast<int>(sizeof(tuneItems) / sizeof(tuneItems[0]));

void clampTuning() {
    for (const TuneItem& item : tuneItems) {
        *item.value = clampf(*item.value, item.lo, item.hi);
    }
}

std::string serializeTuning() {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "gripNormal=%.3f gripDrift=%.3f inertia=%.3f driftDecel=%.3f maxSlip=%.3f minSpeed=%.3f entry=%.3f",
                  driftTuning.gripNormal, driftTuning.gripDrift, driftTuning.inertia,
                  driftTuning.driftDecel, driftTuning.maxSlip, driftTuning.minSpeed,
                  driftTuning.entrySharp);
    return buf;
}

void parseTuning(const char* text) {
    if (!text) {
        return;
    }
    auto get = [&](const char* key, float& out) {
        const char* p = std::strstr(text, key);
        if (p && p[std::strlen(key)] == '=') {
            out = std::strtof(p + std::strlen(key) + 1, nullptr);
        }
    };
    get("gripNormal", driftTuning.gripNormal);
    get("gripDrift", driftTuning.gripDrift);
    get("inertia", driftTuning.inertia);
    get("driftDecel", driftTuning.driftDecel);
    get("maxSlip", driftTuning.maxSlip);
    get("minSpeed", driftTuning.minSpeed);
    get("entry", driftTuning.entrySharp);
    clampTuning();
}

void saveTuning() {
    std::string data = serializeTuning();
#ifdef __EMSCRIPTEN__
    tuningSaveJs(data.c_str());
#else
    std::ofstream out("drift_tuning.txt", std::ios::trunc);
    if (out) {
        out << data << "\n";
    }
#endif
}

void loadTuning() {
#ifdef __EMSCRIPTEN__
    char* p = tuningLoadJs();
    if (p) {
        parseTuning(p);
        std::free(p);
    }
#else
    std::ifstream in("drift_tuning.txt");
    if (in) {
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        parseTuning(data.c_str());
    }
#endif
}

void openPad(App& app) {
#ifdef __EMSCRIPTEN__
    // ブラウザのパッドは Gamepad API 側で読む。SDL で開くと入力が二重になる。
    (void)app;
    return;
#else
    if (app.pad) {
        return;
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            app.pad = SDL_GameControllerOpen(i);
            if (app.pad) {
                SDL_Log("controller: %s", SDL_GameControllerName(app.pad));
                break;
            }
        }
    }
#endif
}

struct PadPulse {
    bool reset = false;
    bool pause = false;
    bool camera = false;
};

float stickDead(float value) {
    const float dead = 0.14f;
    if (std::fabs(value) < dead) {
        return 0;
    }
    float sign = value > 0 ? 1.0f : -1.0f;
    return (value - sign * dead) / (1.0f - dead);
}

#ifndef __EMSCRIPTEN__
float stickAxis(SDL_GameController* pad, SDL_GameControllerAxis axis) {
    return stickDead(SDL_GameControllerGetAxis(pad, axis) / 32767.0f);
}
#endif

#ifdef __EMSCRIPTEN__
// ブラウザの Gamepad API は 0 が離した状態、1 が押し込み。古いパッドは -1..1 で、離すと -1。
float triggerLevel(float value) {
    if (value < 0) {
        value = (value + 1.0f) * 0.5f;
    }
    return value < 0.04f ? 0.0f : clampf(value, 0.0f, 1.0f);
}

PadPulse readBrowserPad(DriveInput& input) {
    PadPulse pulse;
    static bool prevCamera = false;
    static bool prevReset = false;
    static bool prevPause = false;
    bool camera = false;
    bool reset = false;
    bool pause = false;
    if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS) {
        prevCamera = prevReset = prevPause = false;
        return pulse;
    }
    const int count = emscripten_get_num_gamepads();
    for (int i = 0; i < count; ++i) {
        EmscriptenGamepadEvent pad;
        if (emscripten_get_gamepad_status(i, &pad) != EMSCRIPTEN_RESULT_SUCCESS || !pad.connected) {
            continue;
        }
        auto axis = [&](int index) {
            return index < pad.numAxes ? static_cast<float>(pad.axis[index]) : 0.0f;
        };
        auto analog = [&](int index) {
            return index < pad.numButtons ? static_cast<float>(pad.analogButton[index]) : 0.0f;
        };
        auto held = [&](int index) {
            return index < pad.numButtons && pad.digitalButton[index];
        };
        const bool standard = std::strcmp(pad.mapping, "standard") == 0 || (pad.numButtons >= 16 && pad.numAxes >= 2);
        input.steer += stickDead(axis(0));
        if (standard) {
            // 右スティックはステアに混ぜず、ドリフト専用（ツインスティック）。
            input.driftSteer += stickDead(axis(2));
            if (held(1) || held(4) || held(5)) input.drift = true;  // B / LB / RB
            if (held(14)) input.steer -= 1;
            if (held(15)) input.steer += 1;
            if (held(12)) input.throttle = 1;
            if (held(13)) input.brake = 1;
            float throttle = analog(7);
            float brake = analog(6);
            if (throttle < 0.01f && pad.numAxes > 5) throttle = triggerLevel(axis(5));
            if (brake < 0.01f && pad.numAxes > 4) brake = triggerLevel(axis(4));
            input.throttle = std::max(input.throttle, throttle);
            input.brake = std::max(input.brake, brake);
            camera = camera || held(3);
            reset = reset || held(8);
            pause = pause || held(9);
        } else if (pad.numAxes >= 6) {
            input.throttle = std::max(input.throttle, triggerLevel(axis(5)));
            input.brake = std::max(input.brake, triggerLevel(axis(4)));
        }
    }
    pulse.camera = camera && !prevCamera;
    pulse.reset = reset && !prevReset;
    pulse.pause = pause && !prevPause;
    prevCamera = camera;
    prevReset = reset;
    prevPause = pause;
    return pulse;
}
#endif

DriveInput readDrive(SDL_GameController* pad, PadPulse& pulse) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    DriveInput input;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) input.throttle = 1;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) input.brake = 1;
    // キーボードのツインスティック: A/D と矢印キーを両方使っているときは、
    // A/D をステア、矢印左右をドリフトスティックとして分ける（Inertial Drift 風）。
    // 片方だけのときは従来どおり、その側がステアになる。
    float wasdSteer = 0;
    float arrowSteer = 0;
    if (keys[SDL_SCANCODE_A]) wasdSteer -= 1;
    if (keys[SDL_SCANCODE_D]) wasdSteer += 1;
    if (keys[SDL_SCANCODE_LEFT]) arrowSteer -= 1;
    if (keys[SDL_SCANCODE_RIGHT]) arrowSteer += 1;
    if (wasdSteer != 0 && arrowSteer != 0) {
        input.steer += wasdSteer;
        input.driftSteer += arrowSteer;
    } else {
        input.steer += wasdSteer + arrowSteer;
    }
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ||
        keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) {
        input.drift = true;
    }
#ifndef __EMSCRIPTEN__
    if (pad) {
        float throttle = stickAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        float brake = stickAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        if (throttle < 0) throttle = 0;
        if (brake < 0) brake = 0;
        input.throttle = std::max(input.throttle, throttle);
        input.brake = std::max(input.brake, brake);
        input.steer += stickAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        // 右スティックはステアに混ぜず、ドリフト専用（ツインスティック）。
        input.driftSteer += stickAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ||
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
            input.drift = true;
        }
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) input.throttle = 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) input.brake = 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) input.steer -= 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) input.steer += 1;
    }
#else
    (void)pad;
#endif
#ifdef __EMSCRIPTEN__
    // SDL はブラウザのパッドをゲームコントローラと認識しないことが多い。
    // 標準マッピングを Gamepad API から直接読む。
    PadPulse browser = readBrowserPad(input);
    pulse.camera = pulse.camera || browser.camera;
    pulse.reset = pulse.reset || browser.reset;
    pulse.pause = pulse.pause || browser.pause;
#else
    (void)pulse;
#endif
    input.throttle = clampf(input.throttle, 0, 1);
    input.brake = clampf(input.brake, 0, 1);
    input.steer = clampf(input.steer, -1, 1);
    input.driftSteer = clampf(input.driftSteer, -1, 1);
    return input;
}

void resetRace(App& app) {
    app.car.reset(app.track);
    app.race = Race{};
    app.camera.ready = false;
}

void tickRace(Race& race, float s, float length, float dt) {
    if (race.prevS >= 0) {
        float gap = (length - race.prevS) + s;
        if (race.prevS > length * 0.5f && s < length * 0.5f && gap < 160.0f) {
            if (race.timing) {
                if (race.best < 0 || race.lap < race.best) {
                    race.best = race.lap;
                }
                race.completed += 1;
            }
            race.timing = true;
            race.lap = 0;
        }
    }
    race.prevS = s;
    race.lap += dt;
}

void updateCamera(App& app, float dt) {
    const float back[3] = {8.6f, 4.1f, 16.0f};
    const float up[3] = {3.3f, 1.55f, 7.2f};
    const float ahead[3] = {9.0f, 6.0f, 14.0f};
    int mode = app.camera.mode;
    // ドリフト中は車体の向きではなく進行方向寄りにカメラを向ける。
    // 車がカニ走りしても視線が置いていかれず、滑っている感じが伝わる。
    float heading = app.car.yaw + app.car.slipAngle * 0.55f * app.car.driftAmount;
    Vec3 forward{std::sin(heading), 0, std::cos(heading)};
    Vec3 eye = Vec3{app.car.x, app.car.y, app.car.z} - forward * back[mode] + Vec3{0, up[mode], 0};
    Vec3 look = Vec3{app.car.x, app.car.y, app.car.z} + forward * ahead[mode] + Vec3{0, 1.15f, 0};
    TrackPose under = app.track.query(eye.x, eye.z, app.car.y, app.car.ground.s);
    if (under.found && under.xzDist < 30.0f && eye.y < under.centerY + 0.8f) {
        eye.y = under.centerY + 0.8f;
    }
    if (!app.camera.ready) {
        app.camera.eye = eye;
        app.camera.look = look;
        app.camera.ready = true;
        return;
    }
    float blend = 1.0f - std::exp(-dt * 7.0f);
    app.camera.eye = app.camera.eye + (eye - app.camera.eye) * blend;
    app.camera.look = app.camera.look + (look - app.camera.look) * blend;
}

void drawMinimap(App& app) {
    float size = 190.0f;
    float x0 = 24.0f;
    float y0 = float(app.renderer.height) - size - 28.0f;
    app.renderer.addQuad(x0, y0, x0 + size, y0 + size, 0.02f, 0.03f, 0.05f, 0.78f);
    float minX = app.track.samples[0].x;
    float maxX = minX;
    float minZ = app.track.samples[0].z;
    float maxZ = minZ;
    for (const TrackSample& sample : app.track.samples) {
        minX = std::min(minX, sample.x);
        maxX = std::max(maxX, sample.x);
        minZ = std::min(minZ, sample.z);
        maxZ = std::max(maxZ, sample.z);
    }
    float midX = (minX + maxX) * 0.5f;
    float midZ = (minZ + maxZ) * 0.5f;
    float span = std::max(maxX - minX, maxZ - minZ) * 1.12f;
    auto mapX = [&](float x) { return x0 + size * 0.5f + (x - midX) / span * (size - 18.0f); };
    auto mapY = [&](float z) { return y0 + size * 0.5f - (z - midZ) / span * (size - 18.0f); };
    const int n = static_cast<int>(app.track.samples.size());
    for (int i = 0; i < n; ++i) {
        const TrackSample& a = app.track.samples[i];
        const TrackSample& b = app.track.samples[(i + 1) % n];
        float r = 0.75f, g = 0.78f, bl = 0.82f;
        if (a.bridge || b.bridge) {
            r = 0.95f;
            g = 0.72f;
            bl = 0.28f;
        }
        app.renderer.drawLine(mapX(a.x), mapY(a.z), mapX(b.x), mapY(b.z), 2.0f, r, g, bl, 1);
    }
    float mx = mapX(app.car.x);
    float my = mapY(app.car.z);
    Vec3 nose{std::sin(app.car.yaw), 0, std::cos(app.car.yaw)};
    Vec3 side{std::cos(app.car.yaw), 0, -std::sin(app.car.yaw)};
    float nx = mx + nose.x * 9.0f;
    float ny = my - nose.z * 9.0f;
    float lx = mx - nose.x * 5.0f + side.x * 5.0f;
    float ly = my + nose.z * 5.0f - side.z * 5.0f;
    float rx = mx - nose.x * 5.0f - side.x * 5.0f;
    float ry = my + nose.z * 5.0f + side.z * 5.0f;
    app.renderer.drawLine(lx, ly, nx, ny, 2.0f, 0.9f, 0.2f, 0.18f, 1);
    app.renderer.drawLine(rx, ry, nx, ny, 2.0f, 0.9f, 0.2f, 0.18f, 1);
    app.renderer.drawLine(lx, ly, rx, ry, 2.0f, 0.9f, 0.2f, 0.18f, 1);
}

// ドリフトのデバッグ表示（B キーで切替）。状態・角度・横速度・入力と、
// ドリフトに入らないときの理由を右上に常時出す。走らなくても切り分けられる。
void drawDriftDebug(App& app) {
    Renderer& hud = app.renderer;
    const Car& car = app.car;
    const DriveInput& in = app.lastInput;
    const float pw = 336.0f;
    const float lineH = 21.0f;
    const int lines = 11;
    const float ph = 14.0f + lineH * float(lines);
    const float px = float(hud.width) - pw - 16.0f;
    const float py = 52.0f;
    hud.addQuad(px, py, px + pw, py + ph, 0.02f, 0.03f, 0.05f, 0.72f);
    float tx = px + 12.0f;
    float ty = py + 8.0f;
    char line[96];
    hud.drawText(tx, ty, 1.6f, "DRIFT DEBUG", 0.85f, 0.88f, 0.92f, 1);
    ty += lineH;
    if (car.drifting) {
        hud.drawText(tx, ty, 1.6f, "STATE: DRIFT", 1.0f, 0.62f, 0.2f, 1);
    } else {
        hud.drawText(tx, ty, 1.6f, "STATE: GRIP", 0.65f, 0.7f, 0.75f, 1);
    }
    ty += lineH;
    std::snprintf(line, sizeof(line), "SLIP: %.0f DEG", car.slipAngle * 57.29578f);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "LAT V: %.1f M/S", car.lateralSpeed);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "FWD V: %.1f M/S", car.speed);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "AMOUNT: %d %%", static_cast<int>(std::lround(car.driftAmount * 100.0f)));
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "CMD: %.2f", car.driftCommand);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "IN: BTN %d STEER %.2f", in.drift ? 1 : 0, in.steer);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "IN: DSTICK %.2f THR %.1f", in.driftSteer, in.throttle);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "SCORE %d CHAIN %d M",
                  static_cast<int>(car.driftScore), static_cast<int>(car.driftChain));
    hud.drawText(tx, ty, 1.6f, line, 1.0f, 0.8f, 0.45f, 1);
    ty += lineH;
    // ドリフト操作をしているのに入らないときは、理由を出す。
    const char* why = "";
    bool trying = in.drift || std::fabs(in.driftSteer) > 0.45f;
    if (!car.drifting && trying) {
        if (car.speed < 9.0f) {
            why = "WHY: TOO SLOW - NEED 32 KM/H";
        } else if (std::fabs(in.steer) < 0.2f && std::fabs(in.driftSteer) <= 0.45f) {
            why = "WHY: NEED STEER OR STICK";
        }
    }
    hud.drawText(tx, ty, 1.6f, why, 0.95f, 0.5f, 0.45f, 1);
}

// ドリフトのチューニングパネル（T キー）。値はその場で物理に反映され、自動保存される。
void drawTuning(App& app) {
    Renderer& hud = app.renderer;
    const float pw = 540.0f;
    const float rowH = 34.0f;
    const float ph = 58.0f + rowH * float(kTuneItemCount) + 44.0f;
    const float px = (float(hud.width) - pw) * 0.5f;
    const float py = (float(hud.height) - ph) * 0.5f - 30.0f;
    hud.addQuad(px, py, px + pw, py + ph, 0.02f, 0.03f, 0.05f, 0.85f);
    hud.drawText(px + 20, py + 14, 2.2f, "DRIFT TUNING", 1.0f, 0.62f, 0.2f, 1);
    char line[96];
    for (int i = 0; i < kTuneItemCount; ++i) {
        const TuneItem& item = tuneItems[i];
        float ry = py + 56.0f + rowH * float(i);
        bool selected = i == app.tuneIndex;
        if (selected) {
            hud.addQuad(px + 10, ry - 4, px + pw - 10, ry + 26, 0.95f, 0.5f, 0.15f, 0.35f);
        }
        float r = selected ? 1.0f : 0.85f;
        float g = selected ? 0.75f : 0.88f;
        float b = selected ? 0.4f : 0.92f;
        hud.drawText(px + 20, ry, 1.8f, item.name, r, g, b, 1);
        if (item.display == 1) {
            std::snprintf(line, sizeof(line), "%.0f DEG", *item.value * 57.29578f);
        } else if (item.display == 2) {
            std::snprintf(line, sizeof(line), "%.0f KM/H", *item.value * 3.6f);
        } else if (item.display == 3) {
            std::snprintf(line, sizeof(line), "%.1f", *item.value);
        } else {
            std::snprintf(line, sizeof(line), "%.2f", *item.value);
        }
        hud.drawText(px + 300, ry, 1.8f, line, r, g, b, 1);
        // 値の範囲内での位置を示すバー。
        float frac = (*item.value - item.lo) / (item.hi - item.lo);
        float bx0 = px + 420;
        float bx1 = px + pw - 20;
        hud.addQuad(bx0, ry + 4, bx1, ry + 12, 0.1f, 0.12f, 0.16f, 0.9f);
        hud.addQuad(bx0, ry + 4, bx0 + (bx1 - bx0) * frac, ry + 12, 1.0f, 0.62f, 0.2f, 0.95f);
    }
    float fy = py + 56.0f + rowH * float(kTuneItemCount) + 8.0f;
    hud.drawText(px + 20, fy, 1.5f, "UP/DOWN SELECT  LEFT/RIGHT ADJUST  SHIFT: FAST", 0.75f, 0.8f, 0.84f, 1);
    hud.drawText(px + 20, fy + 20, 1.5f, "T: CLOSE   VALUES APPLY LIVE AND SAVE AUTOMATICALLY", 0.75f, 0.8f, 0.84f, 1);
}

void drawHud(App& app) {
    Renderer& hud = app.renderer;
    hud.beginHud();
    char line[96];
    // 速度表示はベクトルの大きさ。ドリフト中は前方向成分だけだと実際より遅く見える。
    float velMag = std::sqrt(app.car.velX * app.car.velX + app.car.velZ * app.car.velZ);
    int kmh = static_cast<int>(std::lround(velMag * 3.6f));
    std::snprintf(line, sizeof(line), "%d KM/H", kmh);
    hud.drawText(24, 22, 4.0f, line, 0.95f, 0.96f, 0.98f, 1);

    if (!app.race.timing) {
        std::snprintf(line, sizeof(line), "OUT LAP");
    } else {
        std::snprintf(line, sizeof(line), "LAP %d", app.race.completed + 1);
    }
    hud.drawText(24, 64, 2.2f, line, 0.85f, 0.88f, 0.92f, 1);
    formatTime(line, sizeof(line), app.race.lap);
    hud.drawText(150, 64, 2.2f, line, 0.95f, 0.96f, 0.98f, 1);

    char best[32];
    formatTime(best, sizeof(best), app.race.best);
    std::snprintf(line, sizeof(line), "BEST %s", best);
    hud.drawText(24, 90, 2.0f, line, 0.75f, 0.8f, 0.7f, 1);

    int percent = static_cast<int>(app.car.ground.s / app.track.length * 100.0f);
    const char* corner = app.track.nameOf(app.car.ground.nameId);
    if (app.car.ground.corner > 0 && corner[0]) {
        std::snprintf(line, sizeof(line), "T%d %s  %d%%", app.car.ground.corner, corner, percent);
    } else if (corner[0]) {
        std::snprintf(line, sizeof(line), "%s  %d%%", corner, percent);
    } else {
        std::snprintf(line, sizeof(line), "%d%%", percent);
    }
    hud.drawText(24, 116, 2.0f, line, 0.9f, 0.9f, 0.92f, 1);

    // ドリフト表示: 走行中でも目に入るよう、画面中央上に大きく出す。
    // 下のバーはスリップ角（中央が 0、左右に最大約60度）。
    if (app.car.drifting) {
        int deg = static_cast<int>(std::lround(std::fabs(app.car.slipAngle) * 57.29578f));
        std::snprintf(line, sizeof(line), "DRIFT %d DEG", deg);
        float scale = 4.0f;
        float textW = float(std::strlen(line)) * 6.0f * scale;
        hud.drawText((float(hud.width) - textW) * 0.5f, 20, scale, line, 1.0f, 0.62f, 0.2f, 1);
        float cx = float(hud.width) * 0.5f;
        hud.addQuad(cx - 130, 58, cx + 130, 66, 0.02f, 0.03f, 0.05f, 0.6f);
        float fill = clampf(app.car.slipAngle / 1.05f, -1.0f, 1.0f) * 130.0f;
        float fx0 = fill < 0 ? cx + fill : cx;
        float fx1 = fill < 0 ? cx : cx + fill;
        hud.addQuad(fx0, 58, fx1, 66, 1.0f, 0.62f, 0.2f, 0.95f);
        hud.addQuad(cx - 1.5f, 56, cx + 1.5f, 68, 0.95f, 0.96f, 0.98f, 0.9f);
        std::snprintf(line, sizeof(line), "SCORE %d  CHAIN %d M",
                      static_cast<int>(app.car.driftScore), static_cast<int>(app.car.driftChain));
        float subW = float(std::strlen(line)) * 6.0f * 2.0f;
        hud.drawText((float(hud.width) - subW) * 0.5f, 76, 2.0f, line, 1.0f, 0.8f, 0.45f, 1);
    } else if (std::fabs(app.car.slipAngle) > 0.15f && app.car.speed > 5.0f) {
        // ドリフト判定ではないが実際に滑っているとき（芝など）。
        int deg = static_cast<int>(std::lround(std::fabs(app.car.slipAngle) * 57.29578f));
        std::snprintf(line, sizeof(line), "SLIDE %d DEG", deg);
        float scale = 3.0f;
        float textW = float(std::strlen(line)) * 6.0f * scale;
        hud.drawText((float(hud.width) - textW) * 0.5f, 22, scale, line, 0.95f, 0.85f, 0.3f, 1);
    }

    const char* camName = "CHASE";
    if (app.camera.mode == 1) camName = "CLOSE";
    if (app.camera.mode == 2) camName = "FAR";
    hud.drawText(float(hud.width) - 140, 22, 2.0f, camName, 0.8f, 0.84f, 0.88f, 1);

    if (app.showHelp) {
        hud.drawText(24, float(hud.height) - 102, 1.7f,
                     "W/UP ACCEL  S/DOWN BRAKE  A/D STEER  SPACE/SHIFT/CTRL DRIFT  R RESET  C CAMERA  P PAUSE  B DEBUG  T TUNE",
                     0.9f, 0.92f, 0.94f, 1);
        hud.drawText(24, float(hud.height) - 78, 1.7f,
                     "KEYBOARD TWIN-STICK: A/D STEER PLUS ARROW KEYS AS DRIFT STICK",
                     0.75f, 0.8f, 0.84f, 1);
        hud.drawText(24, float(hud.height) - 54, 1.7f,
                     "PAD: RT ACCEL  LT BRAKE  L-STICK STEER  R-STICK OR B/LB/RB DRIFT  Y CAMERA",
                     0.75f, 0.8f, 0.84f, 1);
    }
    hud.drawText(float(hud.width) - 250, float(hud.height) - 28, 1.6f, "(C) OSM / GSI", 0.85f, 0.88f, 0.9f, 1);
    if (app.paused) {
        hud.drawText(float(hud.width) * 0.5f - 70, float(hud.height) * 0.45f, 5.0f, "PAUSE", 1, 0.95f, 0.9f, 1);
    }
    drawMinimap(app);
    if (app.showDriftDebug) {
        drawDriftDebug(app);
    }
    if (app.showTuning) {
        drawTuning(app);
    }
    hud.flushHud();
}

float carPitch(const App& app) {
    Vec3 forward{std::sin(app.car.yaw), 0, std::cos(app.car.yaw)};
    float preferS = app.car.ground.found ? app.car.ground.s : -1.0f;
    TrackPose ahead = app.track.query(app.car.x + forward.x * 2.4f, app.car.z + forward.z * 2.4f, app.car.y, preferS);
    TrackPose behind = app.track.query(app.car.x - forward.x * 2.4f, app.car.z - forward.z * 2.4f, app.car.y, preferS);
    if (!ahead.found || !behind.found) {
        return 0;
    }
    return std::atan2(ahead.centerY - behind.centerY, 4.8f);
}

void frame(void* userdata) {
    App& app = *static_cast<App*>(userdata);
    SDL_Event event;
    bool reset = false;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            app.running = false;
        } else if (event.type == SDL_KEYDOWN) {
            SDL_Scancode code = event.key.keysym.scancode;
            if (code == SDL_SCANCODE_T && !event.key.repeat) {
                app.showTuning = !app.showTuning;
            } else if (app.showTuning) {
                // チューニング中は矢印キーを項目の選択と調整に使う（長押しリピート可）。
                TuneItem& item = tuneItems[app.tuneIndex];
                if (code == SDL_SCANCODE_UP) {
                    app.tuneIndex = (app.tuneIndex + kTuneItemCount - 1) % kTuneItemCount;
                } else if (code == SDL_SCANCODE_DOWN) {
                    app.tuneIndex = (app.tuneIndex + 1) % kTuneItemCount;
                } else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT) {
                    float dir = code == SDL_SCANCODE_RIGHT ? 1.0f : -1.0f;
                    float fast = (SDL_GetModState() & KMOD_SHIFT) ? 5.0f : 1.0f;
                    *item.value = clampf(*item.value + dir * item.step * fast, item.lo, item.hi);
                    saveTuning();
                }
            } else if (!event.key.repeat) {
                if (code == SDL_SCANCODE_ESCAPE) app.running = false;
                else if (code == SDL_SCANCODE_R) reset = true;
                else if (code == SDL_SCANCODE_C) app.camera.mode = (app.camera.mode + 1) % 3;
                else if (code == SDL_SCANCODE_P) app.paused = !app.paused;
                else if (code == SDL_SCANCODE_H) app.showHelp = !app.showHelp;
                else if (code == SDL_SCANCODE_B) app.showDriftDebug = !app.showDriftDebug;
            }
        } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
            openPad(app);
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && app.pad &&
                   event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(app.pad))) {
            SDL_GameControllerClose(app.pad);
            app.pad = nullptr;
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) reset = true;
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) app.paused = !app.paused;
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_Y) app.camera.mode = (app.camera.mode + 1) % 3;
        }
    }
    PadPulse padPulse;
    DriveInput input = readDrive(app.pad, padPulse);
    app.lastInput = input;
    if (app.showTuning) {
        input = DriveInput{};  // チューニング中は矢印キーを運転に使わない
    }
    if (padPulse.reset) reset = true;
    if (padPulse.pause) app.paused = !app.paused;
    if (padPulse.camera) app.camera.mode = (app.camera.mode + 1) % 3;
    if (reset) {
        resetRace(app);
    }

    Uint64 now = SDL_GetPerformanceCounter();
    double elapsed = double(now - app.prevCounter) / double(SDL_GetPerformanceFrequency());
    app.prevCounter = now;
    if (elapsed > kMaxFrameSec) elapsed = kMaxFrameSec;
    if (elapsed < 0) elapsed = 0;

    if (app.paused) {
        app.accumulator = 0;
    } else {
        app.accumulator += elapsed;
        int guard = 0;
        while (app.accumulator >= kFixedStepSec && guard < 8) {
            app.car.update(float(kFixedStepSec), input, app.track);
            tickRace(app.race, app.car.ground.s, app.track.length, float(kFixedStepSec));
            app.accumulator -= kFixedStepSec;
            ++guard;
        }
    }
    updateCamera(app, float(elapsed));

    int drawableW = 0;
    int drawableH = 0;
    SDL_GL_GetDrawableSize(app.window, &drawableW, &drawableH);
    app.renderer.resize(drawableW, drawableH);
    app.renderer.clearFrame();
    app.renderer.drawSky();

    float aspect = float(drawableW) / float(drawableH > 0 ? drawableH : 1);
    Mat4 proj = perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.15f, 4000.0f);
    Mat4 view = lookAt(app.camera.eye, app.camera.look, {0, 1, 0});
    app.renderer.beginWorld(mul(proj, view), app.camera.eye);

    Mat4 identity;
    app.renderer.draw(app.course, identity);
    Mat4 shadow = mul(translation(app.car.x, app.car.y + 0.08f, app.car.z), rotationY(app.car.yaw));
    app.renderer.draw(app.shadow, shadow);

    float pitch = carPitch(app);
    // 路面のバンクに、横滑りの荷重で沈むロールを足す。
    float roll = std::atan(app.car.ground.cross) -
                 clampf(app.car.lateralSpeed * 0.010f, -0.12f, 0.12f);
    Mat4 model = carModelMatrix(app.car, pitch, roll);
    app.renderer.draw(app.body, model);
    const float wheelX = 0.82f;
    const float wheelZ[2] = {1.42f, -1.38f};
    const float steerForWheel[2] = {app.car.steerAngle, 0};
    for (int i = 0; i < 2; ++i) {
        for (int side = -1; side <= 1; side += 2) {
            Mat4 place = mul(translation(float(side) * wheelX, 0.34f, wheelZ[i]),
                             mul(rotationY(steerForWheel[i]), rotationX(app.car.wheelSpin)));
            app.renderer.draw(app.wheel, mul(model, place));
        }
    }
    drawHud(app);
    SDL_GL_SwapWindow(app.window);

#ifndef __EMSCRIPTEN__
    if (!app.running) {
        // ネイティブの while が抜ける。
    }
#endif
}

// ドリフトの自己確認: まっすぐ加速 -> ドリフトボタン+全開ステアで滑らせる ->
// 緩めると収束する、の 3 段階を見る。滑らなければ (スリップ角が付かなければ) 失敗。
int driftTest(const Track& track) {
    Car car;
    car.reset(track);
    const float dt = 1.0f / 60.0f;
    DriveInput straight;
    straight.throttle = 1;
    for (int i = 0; i < 60 * 3; ++i) {
        car.update(dt, straight, track);
    }
    if (car.speed < 15.0f) {
        std::printf("drift-test: did not reach speed (%.1f m/s)\n", car.speed);
        return 5;
    }
    DriveInput driftIn;
    driftIn.throttle = 1;
    driftIn.steer = 1;
    driftIn.drift = true;
    float maxSlip = 0;
    float maxLateral = 0;
    for (int i = 0; i < 48; ++i) {  // 0.8 秒
        car.update(dt, driftIn, track);
        maxSlip = std::max(maxSlip, std::fabs(car.slipAngle));
        maxLateral = std::max(maxLateral, std::fabs(car.lateralSpeed));
    }
    std::printf("drift-test: speed %.1f km/h  maxSlip %.0f deg  maxLateral %.1f m/s  score %.0f  chain %.0f m\n",
                car.speed * 3.6f, maxSlip * 57.29578f, maxLateral, car.driftScore, car.driftChain);
    if (maxSlip < 0.30f) {  // 約17度も付かないなら滑っていない
        std::printf("drift-test: no slide\n");
        return 6;
    }
    if (maxLateral < 3.0f) {
        std::printf("drift-test: no lateral velocity\n");
        return 7;
    }
    DriveInput calm;
    calm.throttle = 0.6f;
    for (int i = 0; i < 60 * 2; ++i) {
        car.update(dt, calm, track);
    }
    std::printf("drift-test: after release slip %.0f deg  drifting %d\n",
                std::fabs(car.slipAngle) * 57.29578f, car.drifting ? 1 : 0);
    if (std::fabs(car.slipAngle) > 0.12f || car.drifting) {
        std::printf("drift-test: did not recover grip\n");
        return 8;
    }
    // ツインスティック単体（キーボードの A/D + 矢印、パッドの右スティック）でも
    // 滑ることを確認する。ステアは 0、ドリフトスティックだけを倒す。
    Car car2;
    car2.reset(track);
    for (int i = 0; i < 60 * 3; ++i) {
        car2.update(dt, straight, track);
    }
    DriveInput twin;
    twin.throttle = 1;
    twin.driftSteer = -1;
    float twinMaxSlip = 0;
    for (int i = 0; i < 48; ++i) {
        car2.update(dt, twin, track);
        twinMaxSlip = std::max(twinMaxSlip, std::fabs(car2.slipAngle));
    }
    std::printf("drift-test: twin-stick maxSlip %.0f deg  drifting %d\n",
                twinMaxSlip * 57.29578f, car2.drifting ? 1 : 0);
    if (twinMaxSlip < 0.30f) {
        std::printf("drift-test: twin-stick did not slide\n");
        return 9;
    }
    return 0;
}

int selfTest(const Track& track, float minLength, float maxLength) {
    int driftResult = driftTest(track);
    if (driftResult != 0) {
        return driftResult;
    }
    Car car;
    car.reset(track);
    float travelled = 0;
    float prevS = -1;
    float maxLat = 0;
    for (int step = 0; step < 60 * 50; ++step) {
        TrackPose pose = track.query(car.x, car.z, car.y, car.ground.found ? car.ground.s : -1.0f);
        float lookS = pose.s + 70.0f;
        if (lookS >= track.length) {
            lookS -= track.length;
        }
        int nearest = 0;
        float nearestGap = 1.0e9f;
        for (int i = 0; i < static_cast<int>(track.samples.size()); i += 2) {
            float gap = std::fabs(track.samples[i].s - lookS);
            if (gap > track.length * 0.5f) {
                gap = track.length - gap;
            }
            if (gap < nearestGap) {
                nearestGap = gap;
                nearest = i;
            }
        }
        int next = (nearest + 1) % static_cast<int>(track.samples.size());
        float tx = track.samples[next].x - track.samples[nearest].x;
        float tz = track.samples[next].z - track.samples[nearest].z;
        float targetYaw = std::atan2(tx, tz);
        float errNow = wrapPi(pose.yaw - car.yaw);
        float errAhead = wrapPi(targetYaw - car.yaw);
        float bend = std::fabs(wrapPi(targetYaw - pose.yaw));
        DriveInput input;
        input.steer = clampf(errNow * 2.6f + errAhead * 0.5f, -1.0f, 1.0f);
        input.steer = clampf(input.steer - pose.lateral * 0.12f, -1.0f, 1.0f);
        input.throttle = bend < 0.15f ? 1.0f : 0.45f;
        input.brake = 0;
        if (bend > 0.28f && car.speed > 22.0f) {
            input.throttle = 0;
            input.brake = 1.0f;
        } else if (bend > 0.12f && car.speed > 34.0f) {
            input.throttle = 0;
            input.brake = 0.55f;
        }
        if (car.speed < 10.0f) {
            input.brake = 0;
            input.throttle = std::max(input.throttle, 0.5f);
        }
        car.update(1.0f / 60.0f, input, track);
        pose = track.query(car.x, car.z, car.y, car.ground.found ? car.ground.s : -1.0f);
        if (prevS >= 0) {
            float ds = pose.s - prevS;
            if (ds < -track.length * 0.5f) ds += track.length;
            if (ds > track.length * 0.5f) ds -= track.length;
            if (ds > 0) travelled += ds;
        }
        prevS = pose.s;
        maxLat = std::max(maxLat, pose.xzDist);
        if (!std::isfinite(car.x) || !std::isfinite(car.speed) || !std::isfinite(car.yaw)) {
            std::printf("self-test non-finite\n");
            return 1;
        }
    }
    std::printf("self-test length %.0f travelled %.0f maxLat %.1f speed %.1f km/h\n",
                track.length, travelled, maxLat, car.speed * 3.6f);
    if (track.length < minLength || track.length > maxLength) return 2;
    if (travelled < 1200.0f) return 3;
    if (maxLat > 30.0f) return 4;
    return 0;
}

bool initGl(App& app) {
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    app.window = SDL_CreateWindow(app.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!app.window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    app.gl = SDL_GL_CreateContext(app.window);
    if (!app.gl) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    if (!app.renderer.init()) {
        SDL_Log("GL init failed");
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-test") == 0) {
            test = true;
        }
    }
    const std::string stem = pickTrackStem(argc, argv);
    Track track;
    std::string path = findTrackPath(stem);
    if (!track.load(path)) {
        if (stem == "suzuka") {
            std::fprintf(stderr, "コースを読めません: %s\n", path.c_str());
            return 1;
        }
        // 指定コースが無ければ既定コースに落とす（ブラウザのタイプミス対策）。
        path = findTrackPath("suzuka");
        track = Track{};
        if (!track.load(path)) {
            std::fprintf(stderr, "コースを読めません: %s\n", path.c_str());
            return 1;
        }
    }
    if (test) {
        // 鈴鹿は従来どおり厳密な全長チェック、その他のコースは緩い範囲で見る。
        return stem == "suzuka" ? selfTest(track, 5200.0f, 6500.0f)
                                : selfTest(track, 1500.0f, 15000.0f);
    }
    loadTuning();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    bool imageOk = (IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) != 0;
    if (!imageOk) {
        SDL_Log("IMG_Init failed: %s", IMG_GetError());
    }
    bool audioOk = Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0;
    if (!audioOk) {
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
    }

    App app;
    if (stem == "flow") {
        app.title = "Flow Circuit";
    }
    app.track = std::move(track);
    if (!initGl(app)) {
        SDL_Quit();
        return 1;
    }
    openPad(app);
    app.course = app.renderer.upload(app.track.visual);
    CpuMesh body;
    CpuMesh wheel;
    CpuMesh shadow;
    buildCarBody(body);
    buildCarWheel(wheel);
    buildCarShadow(shadow);
    app.body = app.renderer.upload(body);
    app.wheel = app.renderer.upload(wheel);
    app.shadow = app.renderer.upload(shadow);
    app.car.reset(app.track);
    app.prevCounter = SDL_GetPerformanceCounter();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(frame, &app, 0, 1);
#else
    while (app.running) {
        Uint64 frameStart = SDL_GetPerformanceCounter();
        frame(&app);
        double frameMs = double(SDL_GetPerformanceCounter() - frameStart) / double(SDL_GetPerformanceFrequency()) * 1000.0;
        if (frameMs < 14.0) {
            SDL_Delay(static_cast<Uint32>(14.0 - frameMs));
        }
    }
#endif

    if (app.pad) SDL_GameControllerClose(app.pad);
    if (audioOk) {
        Mix_CloseAudio();
        Mix_Quit();
    }
    if (imageOk) IMG_Quit();
    SDL_GL_DeleteContext(app.gl);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
