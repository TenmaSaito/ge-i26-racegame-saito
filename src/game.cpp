// ============================================================================
// game.cpp ― ゲーム進行の統括
// 役割: 初期化（コースに合わせたメッシュ構築と GPU アップロード）と、
//       1 フレームの進行（入力→物理→演出→カメラ→描画）。
//       個別の処理は各モジュールに委譲し、ここは段取りだけを持つ。
// レイヤー: アプリケーション層。SDL / GL / platform は直接知らず、
//       入力は input.hpp のポート、描画は IRenderer 経由で使う。
//       時間の計測・画面サイズ・表示 (Swap) は外側 (platform) の担当。
// ============================================================================

#include "game.hpp"

#include "camera.hpp"
#include "carmodel.hpp"
#include "effects.hpp"
#include "hud.hpp"
#include "input.hpp"
#include "scenery.hpp"
#include "tuning.hpp"

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
// 路面チャンクも景観チャンクと同じリストでカリング描画する。
for (size_t i = 0; i < app.track.visualChunks.size(); ++i) {
    app.scenery.push_back(app.renderer->upload(app.track.visualChunks[i]));
    app.sceneryCenters.push_back(app.track.visualCenters[i]);
}
{
    // 景観（木・建物・ガードレール）はクラス側でチャンク分割して構築。
    SceneryBuilder builder(app.track);
    for (SceneryChunk& chunk : builder.build()) {
        app.scenery.push_back(app.renderer->upload(chunk.mesh));
        app.sceneryCenters.push_back(chunk.center);
    }
    app.backdrop = app.renderer->upload(builder.buildBackdrop());
    {
        CpuMesh noseMesh;
        buildCarNose(noseMesh);
        app.nose = app.renderer->upload(noseMesh);
    }
    CpuMesh lampMesh;
    buildTailLamps(lampMesh, {0.22f, 0.03f, 0.03f});
    app.lampsOff = app.renderer->upload(lampMesh);
    lampMesh.vertices.clear(); lampMesh.indices.clear();
    buildTailLamps(lampMesh, {1.15f, 0.10f, 0.10f});
    app.lampsOn = app.renderer->upload(lampMesh);
    lampMesh.vertices.clear(); lampMesh.indices.clear();
    buildTailLamps(lampMesh, {2.6f, 0.22f, 0.20f});
    app.lampsBright = app.renderer->upload(lampMesh);
}
CpuMesh body;
CpuMesh wheel;
CpuMesh shadow;
buildCarBody(body);
buildCarWheel(wheel);
buildCarShadow(shadow);
app.body = app.renderer->upload(body);
app.wheel = app.renderer->upload(wheel);
app.shadow = app.renderer->upload(shadow);
app.car.reset(app.track);
}

void gameFrame(Game& app, double dtSeconds) {
    // 入力イベントの解釈は入力アダプタ (外側) が済ませ、「意図」だけが届く。
    UiActions ui = input::pollUi(app.showTuning);
    if (ui.quit) {
        app.running = false;
    }
    if (ui.tuningToggle) {
        app.showTuning = !app.showTuning;
    }
    if (app.showTuning) {
        if (ui.tuneSelect != 0) {
            app.tuneIndex = (app.tuneIndex + ui.tuneSelect + kTuneItemCount) % kTuneItemCount;
        }
        if (ui.tuneAdjust != 0.0f) {
            TuneItem& item = tuneItems[app.tuneIndex];
            *item.value = clampf(*item.value + ui.tuneAdjust * item.step, item.lo, item.hi);
            if (app.saveTuningText) {
                app.saveTuningText(serializeTuning());
            }
        }
    }
    bool reset = ui.reset;
    if (ui.cameraNext) app.camera.mode = (app.camera.mode + 1) % 4;
    if (ui.cameraNext3) app.camera.mode = (app.camera.mode + 1) % 3;
    if (ui.pauseToggle) app.paused = !app.paused;
    if (ui.helpToggle) app.showHelp = !app.showHelp;
    if (ui.debugToggle) app.showDriftDebug = !app.showDriftDebug;
    if (ui.carSpec >= 0) applyCarSpec(app, ui.carSpec);

    PadPulse padPulse;
    DriveInput input = input::sampleDrive(padPulse, app.car.drifting);
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

    // 経過時間は外側 (platform のループ) が測って渡す。この層は時計を持たない。
    double elapsed = dtSeconds;
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

    // 画面サイズの反映と表示 (Swap) は外側 (platform のループ) の担当。
    // ここでは renderer が保持する現在サイズで画角だけを決める。
    app.renderer->clearFrame();
    app.renderer->drawSky();

    float aspect = float(app.renderer->width()) /
                   float(app.renderer->height() > 0 ? app.renderer->height() : 1);
    Mat4 proj = perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.15f, 2200.0f);
    Mat4 view = lookAt(app.camera.eye, app.camera.look, {0, 1, 0});
    app.renderer->beginWorld(mul(proj, view), app.camera.eye);

    Mat4 identity;
    app.renderer->draw(app.backdrop, identity);
    // 景観はチャンク単位で距離カリング（1200m 以内だけ描く）。
    for (size_t i = 0; i < app.scenery.size(); ++i) {
        float dx = app.sceneryCenters[i].x - app.camera.eye.x;
        float dz = app.sceneryCenters[i].z - app.camera.eye.z;
        if (dx * dx + dz * dz < 1000.0f * 1000.0f) app.renderer->draw(app.scenery[i], identity);
    }
    Mat4 shadow = mul(translation(app.viewX, app.viewY + 0.08f, app.viewZ), rotationY(app.viewYaw));
    if (app.camera.mode != 3) app.renderer->draw(app.shadow, shadow);

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
        app.renderer->draw(app.nose, model);
    } else {
        app.renderer->draw(app.body, model);
        // テールランプ: ブレーキで強点灯、ドリフト中は点灯、それ以外は消灯色。
        const GpuMesh& lamps = app.lastInput.brake > 0.05f ? app.lampsBright
                               : app.car.drifting        ? app.lampsOn
                                                         : app.lampsOff;
        app.renderer->draw(lamps, model);
    }
    const float wheelX = 0.82f;
    const float wheelZ[2] = {1.42f, -1.38f};
    const float steerForWheel[2] = {app.car.steerAngle, 0};
    for (int i = 0; i < 2; ++i) {
        for (int side = -1; side <= 1; side += 2) {
            Mat4 place = mul(translation(float(side) * wheelX, 0.34f, wheelZ[i]),
                             mul(rotationY(steerForWheel[i]), rotationX(app.car.wheelSpin)));
            app.renderer->draw(app.wheel, mul(model, place));
        }
    }
    drawSmoke(app);
    drawTrails(app);
    drawHud(app);
}

