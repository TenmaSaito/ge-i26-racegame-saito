// ============================================================================
// input.cpp ― 入力アダプタ (外側の層)
// 役割: キーボード / ゲームパッド / マウス / ブラウザの Gamepad API を読み、
//       input.hpp のポート (DriveInput / UiActions) に翻訳する。
// 依存: SDL2 と platform (フルスクリーン切替)。どちらも外側の層同士。
//       ゲームの状態は直接書き換えず、値で返すだけにする。
// ============================================================================

#include "input.hpp"

#include "car.hpp"       // DriveInput
#include "platform.hpp"  // platform::toggleFullscreen

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>  // ブラウザの Gamepad API 読み取り用
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef __EMSCRIPTEN__
extern std::string gLastPadId;  // 実体は Gamepad API 読み取り部の近くに置く
#endif

namespace {

// このアダプタが所有する入力デバイス。Game はパッドやウィンドウを知らない。
SDL_Window* gWindow = nullptr;
SDL_GameController* gPad = nullptr;

// ベンチ用の差し込み入力 (通常は nullptr)。
const UiActions* gBenchUi = nullptr;
const DriveInput* gBenchDrive = nullptr;


void openPad() {
#ifdef __EMSCRIPTEN__
    // ブラウザのパッドは Gamepad API 側で読む。SDL で開くと入力が二重になる。
    return;
#else
    if (gPad) {
        return;
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            gPad = SDL_GameControllerOpen(i);
            if (gPad) {
                SDL_Log("controller: %s", SDL_GameControllerName(gPad));
                break;
            }
        }
    }
#endif
}

void closePad() {
    if (gPad) {
        SDL_GameControllerClose(gPad);
        gPad = nullptr;
    }
}

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

}  // namespace

namespace input {

void attachWindow(SDL_Window* window) {
    gWindow = window;
    openPad();
}

// パッドの認識状態。ネイティブは SDL が掴んだ名前、ブラウザ版は
// Gamepad API で最後に見えたパッドの ID を返す。
void setBenchUi(const UiActions* actions) { gBenchUi = actions; }
void setBenchDrive(const DriveInput* input) { gBenchDrive = input; }

void rumble(float low, float high, int durationMs) {
    if (!gPad) {
        return;  // パッドが無ければ何もしない
    }
    // SDL の振動は 16bit 指定。対応していないパッド/環境では -1 が返る
    // だけで害はない (ブラウザ版は Gamepad API の振動に橋渡しされる)。
    auto to16 = [](float v) {
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return Uint16(v * 65535.0f);
    };
    SDL_GameControllerRumble(gPad, to16(low), to16(high), Uint32(durationMs < 0 ? 0 : durationMs));
}

std::string padStatus() {
#ifndef __EMSCRIPTEN__
    if (gPad) {
        const char* name = SDL_GameControllerName(gPad);
        return name ? name : "GAMEPAD";
    }
    return "NONE";
#else
    if (!::gLastPadId.empty()) {  // readBrowserPad が更新するグローバル
        return ::gLastPadId;
    }
    return "NONE - PRESS ANY PAD BUTTON";
#endif
}

void detach() {
    closePad();
    gWindow = nullptr;
}

}  // namespace input

namespace input {

// SDL イベントをすべて消化し、UI の意図 (UiActions) に翻訳する。
// 割り当ての解釈はこの層の責任。F キーのフルスクリーンだけは表示系の
// 操作なので、その場で platform に実行させる (意図として内側へ上げない)。
UiActions pollUi(bool tuningOpen) {
    UiActions ui;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            ui.quit = true;
        } else if (event.type == SDL_KEYDOWN) {
            SDL_Scancode code = event.key.keysym.scancode;
            if (code == SDL_SCANCODE_T && !event.key.repeat) {
                ui.tuningToggle = true;
            } else if (tuningOpen) {
                // チューニング中は矢印キーを項目の選択と調整に使う（長押しリピート可）。
                if (code == SDL_SCANCODE_UP) {
                    ui.tuneSelect = -1;
                } else if (code == SDL_SCANCODE_DOWN) {
                    ui.tuneSelect = 1;
                } else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT) {
                    float dir = code == SDL_SCANCODE_RIGHT ? 1.0f : -1.0f;
                    float fast = (SDL_GetModState() & KMOD_SHIFT) ? 5.0f : 1.0f;
                    ui.tuneAdjust = dir * fast;
                }
            } else if (code == SDL_SCANCODE_UP || code == SDL_SCANCODE_W) {
                // メニュー操作 (セレクト画面等)。チューニングが閉じている間は
                // 矢印/WASD を選択の意図として拾う。運転中は RaceScene が
                // この意図を読まないので、走行への影響はない。
                ui.tuneSelect = -1;
            } else if (code == SDL_SCANCODE_DOWN || code == SDL_SCANCODE_S) {
                ui.tuneSelect = 1;
            } else if (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_A) {
                ui.tuneAdjust = -1.0f;
            } else if (code == SDL_SCANCODE_RIGHT || code == SDL_SCANCODE_D) {
                ui.tuneAdjust = 1.0f;
            } else if (!event.key.repeat) {
                if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER ||
                    code == SDL_SCANCODE_SPACE) ui.confirm = true;
                else if (code == SDL_SCANCODE_ESCAPE) ui.quit = true;
                else if (code == SDL_SCANCODE_R) ui.reset = true;
                else if (code == SDL_SCANCODE_C) ui.cameraNext = true;
                else if (code == SDL_SCANCODE_P) ui.pauseToggle = true;
                else if (code == SDL_SCANCODE_H) ui.helpToggle = true;
                else if (code == SDL_SCANCODE_B) ui.debugToggle = true;
                else if (code == SDL_SCANCODE_F) platform::toggleFullscreen();
                else if (code == SDL_SCANCODE_1) ui.carSpec = 0;
                else if (code == SDL_SCANCODE_2) ui.carSpec = 1;
                else if (code == SDL_SCANCODE_3) ui.carSpec = 2;
            }
        } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
            openPad();
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && gPad &&
                   event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gPad))) {
            closePad();
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) ui.reset = true;
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) { ui.pauseToggle = true; ui.confirm = true; }
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) ui.confirm = true;
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_Y) ui.cameraNext3 = true;
            // 十字キーもメニュー操作として同じ意図に流す。
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) ui.tuneSelect = -1;
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) ui.tuneSelect = 1;
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) ui.tuneAdjust = -1.0f;
            else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ui.tuneAdjust = 1.0f;
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
            ui.click = true;  // 決定とは分ける (フォーカス取りのクリックで誤爆しないため)
        }
    }
    if (gBenchUi) {
        return *gBenchUi;  // ベンチ差し込み中は実入力の代わりに返す
    }
    return ui;
}

}  // namespace input

#ifdef __EMSCRIPTEN__
std::string gLastPadId;  // padStatus() 用に最後に見えたパッドの ID を覚える
namespace {
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
        if (gLastPadId.empty() && pad.id[0]) {
            gLastPadId = pad.id;  // 認識確認用に最初の 1 台を覚える
        }
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
}  // namespace
#endif

namespace input {

DriveInput sampleDrive(PadPulse& pulse, bool carDrifting) {
    SDL_Window* window = gWindow;
    SDL_GameController* pad = gPad;
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
    // --- マウス = ドリフトの強さダイヤル -------------------------------------
    // マウスで車体そのものは操作しない。ドリフトが始まった瞬間のマウス位置を
    // 原点として保存し、そこからの横方向のずれを mouseDrift として渡す。
    // 物理側では既存の操作に加算方式で合成される（離すほど強まる/原点に
    // 戻せば弱まる）。スティック入力と混ぜないのは、スティック側の
    // 「0.45 を超えたら主役交代」という崖をマウスに持ち込むと、効かない→
    // 突然奪うという怪しい挙動になるため。デッドゾーンは約 20px と小さく、
    // そこから比例させる。ドリフトしていない間はマウスは何もしない。
    static bool mouseAnchorActive = false;
    static int mouseAnchorX = 0;
    static float mouseSmooth = 0;
    if (window) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        if (carDrifting) {
            if (!mouseAnchorActive) {
                mouseAnchorActive = true;
                mouseAnchorX = mx;
                mouseSmooth = 0;
            }
            int ww = 0;
            int wh = 0;
            SDL_GetWindowSize(window, &ww, &wh);
            float scale = ww > 0 ? ww * 0.25f : 300.0f;
            if (scale < 120.0f) scale = 120.0f;
            float rel = clampf((float)(mx - mouseAnchorX) / scale, -1.0f, 1.0f);
            const float dead = 20.0f / scale;  // 約20px
            float target = 0;
            if (std::fabs(rel) > dead) {
                target = (rel > 0 ? 1.0f : -1.0f) *
                         (std::fabs(rel) - dead) / (1.0f - dead);
            }
            // 手ブレで値が跳ねないよう軽く平滑化（1フレームで最大 0.12 まで）。
            mouseSmooth += clampf(target - mouseSmooth, -0.12f, 0.12f);
            input.mouseDrift = mouseSmooth;
        } else {
            mouseAnchorActive = false;
            mouseSmooth = 0;
        }
    }
    input.throttle = clampf(input.throttle, 0, 1);
    input.brake = clampf(input.brake, 0, 1);
    input.steer = clampf(input.steer, -1, 1);
    input.driftSteer = clampf(input.driftSteer, -1, 1);
    if (gBenchDrive) {
        return *gBenchDrive;  // ベンチ差し込み中は実入力の代わりに返す
    }
    return input;
}

}  // namespace input
