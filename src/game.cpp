// ============================================================================
// game.cpp ― ゲーム進行の統括
// 役割: 初期化（コースに合わせたメッシュ構築と GPU アップロード）と、
//       1 フレームの進行（イベント→入力→物理→演出→カメラ→描画）。
//       個別の処理は各モジュールに委譲し、ここは段取りだけを持つ。
// ============================================================================

#include "game.hpp"

#include "camera.hpp"
#include "carmodel.hpp"
#include "effects.hpp"
#include "hud.hpp"
#include "input.hpp"
#include "platform.hpp"
#include "scenery.hpp"
#include "tuning.hpp"

#include <SDL.h>

#include <cmath>
#include <utility>

namespace {
constexpr double kFixedStepSec = 1.0 / 60.0;
constexpr double kMaxFrameSec = 0.25;
}  // namespace

void resetRace(Game& app) {
    app.car.reset(app.track);
    app.race = Race{};
    app.camera.ready = false;
    app.smoke.clear();
    app.trailL.clear();
    app.trailR.clear();
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


void gameInit(Game& app) {
    openPad(app);
// 路面チャンクも景観チャンクと同じリストでカリング描画する。
for (size_t i = 0; i < app.track.visualChunks.size(); ++i) {
    app.scenery.push_back(app.renderer.upload(app.track.visualChunks[i]));
    app.sceneryCenters.push_back(app.track.visualCenters[i]);
}
{
    // 景観（木・建物・ガードレール）はクラス側でチャンク分割して構築。
    SceneryBuilder builder(app.track);
    for (SceneryChunk& chunk : builder.build()) {
        app.scenery.push_back(app.renderer.upload(chunk.mesh));
        app.sceneryCenters.push_back(chunk.center);
    }
    app.backdrop = app.renderer.upload(builder.buildBackdrop());
    {
        CpuMesh noseMesh;
        buildCarNose(noseMesh);
        app.nose = app.renderer.upload(noseMesh);
    }
    CpuMesh lampMesh;
    buildTailLamps(lampMesh, {0.22f, 0.03f, 0.03f});
    app.lampsOff = app.renderer.upload(lampMesh);
    lampMesh.vertices.clear(); lampMesh.indices.clear();
    buildTailLamps(lampMesh, {1.15f, 0.10f, 0.10f});
    app.lampsOn = app.renderer.upload(lampMesh);
    lampMesh.vertices.clear(); lampMesh.indices.clear();
    buildTailLamps(lampMesh, {2.6f, 0.22f, 0.20f});
    app.lampsBright = app.renderer.upload(lampMesh);
}
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
}

void gameShutdown(Game& app) {
    closePad(app);
    platform::destroyWindow(app);
}

void gameFrame(Game& app) {
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
                else if (code == SDL_SCANCODE_C) app.camera.mode = (app.camera.mode + 1) % 4;
                else if (code == SDL_SCANCODE_P) app.paused = !app.paused;
                else if (code == SDL_SCANCODE_H) app.showHelp = !app.showHelp;
                else if (code == SDL_SCANCODE_B) app.showDriftDebug = !app.showDriftDebug;
                else if (code == SDL_SCANCODE_F) {
                    // フルスクリーンは SDL 経由で切り替える。ページ側から
                    // 強制するとキャンバスと描画バッファがずれて暗転する。
                    Uint32 flags = SDL_GetWindowFlags(app.window);
                    SDL_SetWindowFullscreen(app.window, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                else if (code == SDL_SCANCODE_1) applyCarSpec(app, 0);
                else if (code == SDL_SCANCODE_2) applyCarSpec(app, 1);
                else if (code == SDL_SCANCODE_3) applyCarSpec(app, 2);
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
    DriveInput input = readDrive(app.window, app.pad, padPulse, app.car.drifting);
    app.lastInput = input;
// 外部ツール（Chrome 拡張など）が調整値を書き換えたら、0.5 秒以内に反映する。
    {
        static int pollTick = 0;
        if (++pollTick >= 30) {
            pollTick = 0;
            pollExternalTuning();
        }
    }
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
    if (elapsed > 0.0001) app.fpsSm += float(1.0 / elapsed - app.fpsSm) * 0.05f;
    if (elapsed < 0) elapsed = 0;

    if (app.paused) {
        app.accumulator = 0;
        app.viewX = app.car.x; app.viewY = app.car.y; app.viewZ = app.car.z;
        app.viewYaw = app.car.yaw; app.viewSlip = app.car.slipAngle;
        app.viewReady = true;
    } else {
        app.prevX = app.car.x; app.prevY = app.car.y; app.prevZ = app.car.z;
        app.prevYaw = app.car.yaw; app.prevSlip = app.car.slipAngle;
        app.accumulator += elapsed;
        int guard = 0;
        while (app.accumulator >= kFixedStepSec && guard < 8) {
            app.car.update(float(kFixedStepSec), input, app.track);
            tickRace(app.race, app.car.ground.s, app.track.length, float(kFixedStepSec));
            app.accumulator -= kFixedStepSec;
            ++guard;
        }
        float alpha = float(app.accumulator / kFixedStepSec);
        float dx = app.car.x - app.prevX;
        float dz = app.car.z - app.prevZ;
        if (dx * dx + dz * dz > 400.0f) alpha = 1.0f;  // リセット等のテレポートでは補間しない
        app.viewX = app.prevX + (app.car.x - app.prevX) * alpha;
        app.viewY = app.prevY + (app.car.y - app.prevY) * alpha;
        app.viewZ = app.prevZ + (app.car.z - app.prevZ) * alpha;
        float dyaw = app.car.yaw - app.prevYaw;
        while (dyaw > 3.14159265f) dyaw -= 6.2831853f;
        while (dyaw < -3.14159265f) dyaw += 6.2831853f;
        app.viewYaw = app.prevYaw + dyaw * alpha;
        app.viewSlip = app.prevSlip + (app.car.slipAngle - app.prevSlip) * alpha;
        app.viewReady = true;
    }
    if (!app.paused) {
        updateSmoke(app, float(elapsed));
        updateTrails(app, float(elapsed));
    }
    updateCamera(app, float(elapsed));

    int drawableW = 0;
    int drawableH = 0;
    SDL_GL_GetDrawableSize(app.window, &drawableW, &drawableH);
    app.renderer.resize(drawableW, drawableH);
    app.renderer.clearFrame();
    app.renderer.drawSky();

    float aspect = float(drawableW) / float(drawableH > 0 ? drawableH : 1);
    Mat4 proj = perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.15f, 2200.0f);
    Mat4 view = lookAt(app.camera.eye, app.camera.look, {0, 1, 0});
    app.renderer.beginWorld(mul(proj, view), app.camera.eye);

    Mat4 identity;
    app.renderer.draw(app.backdrop, identity);
    // 景観はチャンク単位で距離カリング（1200m 以内だけ描く）。
    for (size_t i = 0; i < app.scenery.size(); ++i) {
        float dx = app.sceneryCenters[i].x - app.camera.eye.x;
        float dz = app.sceneryCenters[i].z - app.camera.eye.z;
        if (dx * dx + dz * dz < 1000.0f * 1000.0f) app.renderer.draw(app.scenery[i], identity);
    }
    Mat4 shadow = mul(translation(app.viewX, app.viewY + 0.08f, app.viewZ), rotationY(app.viewYaw));
    if (app.camera.mode != 3) app.renderer.draw(app.shadow, shadow);

    // 車体の傾きは荷重移動なので、目標へ一気に行かず少し遅れて追う
    // （以前は横速度に即応してカクついて見えた）。
    {
        // 車体の角度は路面の平面に這わせる。前後・左右 4 点の路面高から
        // 面の傾きを求めるので、斜めに止まってもバンクと勾配の両方に合う。
        Vec3 pos{app.viewX, app.viewY, app.viewZ};
        Vec3 fw{std::sin(app.viewYaw), 0, std::cos(app.viewYaw)};
        Vec3 rt{fw.z, 0, -fw.x};
        float hF = surfaceHeightAt(app, pos + fw * 2.2f);
        float hB = surfaceHeightAt(app, pos - fw * 2.2f);
        float hR = surfaceHeightAt(app, pos + rt * 1.0f);
        float hL = surfaceHeightAt(app, pos - rt * 1.0f);
        float pitchT = std::atan2(hF - hB, 4.4f);
        float rollT = std::atan2(hR - hL, 2.0f) -
                      clampf(app.car.lateralSpeed * 0.009f, -0.12f, 0.12f);
        const float k = 1.0f - std::exp(-3.0f / 60.0f);
        app.bodyPitchSm += (pitchT - app.bodyPitchSm) * k;
        app.bodyRollSm += (rollT - app.bodyRollSm) * k;
    }
    Car viewCar = app.car;
    viewCar.x = app.viewX; viewCar.y = app.viewY; viewCar.z = app.viewZ; viewCar.yaw = app.viewYaw;
    Mat4 model = carModelMatrix(viewCar, app.bodyPitchSm, app.bodyRollSm);
    model = mul(translation(0, 0.05f, 0), model);  // 平面に這わせた際の角のめり込み防止
    if (app.camera.mode == 3) {
        app.renderer.draw(app.nose, model);
    } else {
        app.renderer.draw(app.body, model);
        // テールランプ: ブレーキで強点灯、ドリフト中は点灯、それ以外は消灯色。
        const GpuMesh& lamps = app.lastInput.brake > 0.05f ? app.lampsBright
                               : app.car.drifting        ? app.lampsOn
                                                         : app.lampsOff;
        app.renderer.draw(lamps, model);
    }
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
    drawSmoke(app);
    drawTrails(app);
    drawHud(app);
    SDL_GL_SwapWindow(app.window);
}

