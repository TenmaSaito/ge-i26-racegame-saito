// ============================================================================
// racescene.cpp ― レースシーン (RaceScene) の実装
// 役割: 入力の意図を受けて状態を変え、物理を固定ステップで進め、
//       補間ポーズで描く。指定の周回数を完了したらリザルトへ遷移する
//       (周回数が無制限のフリー走行ではゴールしない)。
//
// 1 フレームの流れ (frame):
//   handleUi → readDriveInput → stepPhysics → updateCamera
//   → detectCountdown → updateSound / updateRumble → ゴール判定 → draw
// ============================================================================

#include "racescene.hpp"

#include "game.hpp"
#include "hud.hpp"  // Hud (drawTitle / drawResult / draw)
#include "input.hpp"
#include "tuning.hpp"

#include <algorithm>
#include <cmath>

namespace {
// 物理の固定ステップ (秒) と、1 フレームの経過時間の上限 (秒)。
// 上限はタブ復帰などの大きな隙間で物理が暴走しないための頭打ち。
constexpr double kFixedStepSec = 1.0 / 60.0;
constexpr double kMaxFrameSec = 0.25;

// 角度の差を -π..π に畳む (補間で 1 周回り込まないように)。
float wrapAngle(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}
}  // namespace

// ============================================================================
// 開始と 1 フレームの段取り
// ============================================================================

void RaceScene::enter() {
    game_.resetRace();
    syncViewToCar();
    game_.setPaused(false);
    game_.setShowTuning(false);
    wasCounting_ = false;
    prevCountNum_ = 0;
    slideTime_ = 0;
    slideGap_ = 0;
}

void RaceScene::frame(double dtSeconds) {
    Game& app = game_;

    // --- 入力 ---------------------------------------------------------------
    bool reset = false;
    if (!handleUi(reset)) {
        return;  // タイトルへ戻る等、このフレームはここまで
    }
    DriveInput rawInput;
    DriveInput input = readDriveInput(rawInput, reset);
    if (reset) {
        app.resetRace();
        syncViewToCar();
    }

    // 経過時間の整形: 上限で頭打ちし、FPS は Game 側で平滑化する。
    double elapsed = dtSeconds;
    if (elapsed > kMaxFrameSec) elapsed = kMaxFrameSec;
    if (elapsed < 0) elapsed = 0;
    app.noteFps(elapsed);

    // --- 物理・演出・カメラ -------------------------------------------------
    stepPhysics(elapsed, input);
    if (!app.paused()) {
        app.effects().update(app.car(), float(elapsed));
    }
    updateCamera(float(elapsed));

    // --- 音と振動 -----------------------------------------------------------
    const CountdownEvents countdown = detectCountdown();
    updateSound(float(elapsed), rawInput, input, countdown);
    updateRumble(float(elapsed), countdown);

    // --- ゴール判定: 指定の周回を完了したらリザルトへ (0 = 無制限ならしない) ---
    if (app.laps() > 0 && app.race().completed() >= app.laps()) {
        app.requestScene(SceneId::Result);
    }

    draw();
}

// ============================================================================
// 入力
// ============================================================================

bool RaceScene::handleUi(bool& resetRequested) {
    Game& app = game_;
    // イベントの解釈は入力アダプタ (外側) が済ませ、「意図」だけ届く。
    UiActions ui = input::pollUi(app.showTuning());
    if (ui.quit) {
        app.requestQuit();
    }
    if (ui.tuningToggle) {
        app.setShowTuning(!app.showTuning());
    }
    if (app.showTuning()) {
        // 調整パネル操作: 項目移動と値の増減。値はその場で物理へ反映され、
        // 変更のたびに保存する (閉じ忘れても残るように)。
        if (ui.tuneSelect != 0) {
            app.setTuneIndex((app.tuneIndex() + ui.tuneSelect + kTuneItemCount) % kTuneItemCount);
        }
        if (ui.tuneAdjust != 0.0f) {
            TuneItem& item = tuneItems[app.tuneIndex()];
            *item.value = clampf(*item.value + ui.tuneAdjust * item.step, item.lo, item.hi);
            app.saveTuningNow();
        }
    }
    if (ui.reset) resetRequested = true;
    if (ui.cameraNext) app.camera().cycle(4);
    if (ui.cameraNext3) app.camera().cycle(3);
    if (ui.pauseToggle) app.setPaused(!app.paused());
    if (app.paused() && ui.tuningToggle) {
        // ポーズ中の T はタイトルへ戻る (調整パネルの切替より優先)。
        app.setPaused(false);
        app.requestScene(SceneId::Title);
        return false;
    }
    if (ui.helpToggle) app.setShowHelp(!app.showHelp());
    if (ui.debugToggle) app.setShowDriftDebug(!app.showDriftDebug());
    // 揺れ切り分け診断 (7/8/9 キー): 傾き・カメラ地面補正・ミラーを個別に切る。
    if (ui.diagTilt) app.setDiagTilt(!app.diagTilt());
    if (ui.diagCam) app.camera().groundLift = !app.camera().groundLift;
    if (ui.diagMirror) app.setDiagMirror(!app.diagMirror());
    // 車種 (1/2/3) は選択画面でのみ変更する。レース中の誤操作で
    // 特性が突然変わらないように、ここでは受け付けない。
    return true;
}

DriveInput RaceScene::readDriveInput(DriveInput& raw, bool& resetRequested) {
    Game& app = game_;
    PadPulse padPulse;
    raw = input::sampleDrive(padPulse, app.car().drifting);
    if (app.autopilot() && !app.race().counting() && !app.paused()) {
        raw = app.autopilotInput(float(kFixedStepSec));  // 動作確認用: AI が代わりに運転する
    }
    app.setLastInput(raw);

    DriveInput input = raw;
    if (app.showTuning()) {
        input = DriveInput{};  // チューニング中は矢印キーを運転に使わない
    }
    if (app.race().counting()) {
        // カウントダウン中は車を動かせない。ただし生のスロットルは
        // 空ぶかしの音とロケットスタートの溜めに使うので raw に残す。
        input = DriveInput{};
    }
    app.race().setLaunchHeld(app.race().counting() && raw.throttle > 0.25f);

    // パッドの単発ボタン (リセット・ポーズ・カメラ)。
    if (padPulse.reset) resetRequested = true;
    if (padPulse.pause) app.setPaused(!app.paused());
    if (padPulse.camera) app.camera().cycle(3);
    return input;
}

// ============================================================================
// 物理とカメラ
// ============================================================================

void RaceScene::syncViewToCar() {
    // 補間の始点と描画ポーズを、リセット直後の車の位置へ同期する。
    // 同期しないと、初回フレームで物理が進まなかった場合に、前回の
    // レースの残り座標やゼロ座標から補間が始まり、開始直後の表示が
    // 跳ぶ。カメラ用の平滑化値と蓄積時間もここで初期化する。
    const Car& car = game_.car();
    prevX_ = viewX_ = car.x;
    prevY_ = viewY_ = car.y;
    prevZ_ = viewZ_ = car.z;
    prevYaw_ = viewYaw_ = car.yaw;
    prevSlip_ = viewSlip_ = car.slipAngle;
    camSlip_ = car.slipAngle;
    camAmount_ = car.driftAmount;
    accumulator_ = 0;
    viewReady_ = true;
}

void RaceScene::stepPhysics(double elapsed, const DriveInput& input) {
    Game& app = game_;
    Car& car = app.car();
    if (app.paused()) {
        accumulator_ = 0;
        viewX_ = car.x; viewY_ = car.y; viewZ_ = car.z;
        viewYaw_ = car.yaw; viewSlip_ = car.slipAngle;
        viewReady_ = true;
        return;
    }

    // --- 固定 60Hz で進め、余りは補間に使う ---------------------------------
    accumulator_ += elapsed;
    int guard = 0;
    while (accumulator_ >= kFixedStepSec && guard < 8) {
        // 補間の始点は「最後の物理ステップの直前」のポーズ。
        // フレーム頭でまとめて退避すると、ステップ 0 回のフレームで
        // 補間が最新値へ飛び、2 回以上のフレームでは古い位置から
        // 引いてしまうため、ステップごとにここで退避する。
        prevX_ = car.x; prevY_ = car.y; prevZ_ = car.z;
        prevYaw_ = car.yaw; prevSlip_ = car.slipAngle;
        car.update(float(kFixedStepSec), input, app.track());
        if (app.race().launchBoost() > 0 && car.speed < 42.0f) {
            // ロケットスタート: GO 直後だけ、溜めに応じた加速を上乗せ。
            // 序盤の速度域だけに効かせ、最高速は車の性能のままにする。
            float fade = app.race().launchBoost() / 0.85f;
            car.speed += app.race().launchPower() * 30.0f * float(kFixedStepSec) * fade;
        }
        app.race().tick(car.ground.s, app.track().length, float(kFixedStepSec));
        if (app.race().timing()) {
            // 学習用: コースのどの区間を何 m/s で走ったか。
            app.profileRecorder().sample(car.ground.s, std::sqrt(car.velX * car.velX + car.velZ * car.velZ));
        }
        stepRivals(float(kFixedStepSec));
        resolveContacts();
        // ゴースト: 計測が始まったら記録開始、計測中は毎ステップ記録、
        // 周回が完了したら最速判定つきで締める。
        if (app.race().timing() && !wasTiming_) {
            app.ghost().beginLap();
        }
        if (app.race().timing()) {
            app.ghost().recordPose(car);
        }
        wasTiming_ = app.race().timing();
        if (app.race().completed() != prevCompleted_) {
            app.ghost().finishLap(app.race().lastLap());
            app.profileRecorder().finishLap(app.race().lastLap());
            prevCompleted_ = app.race().completed();
        }
        accumulator_ -= kFixedStepSec;
        ++guard;
    }
    // ステップ上限 (8回) に達して時間が余った場合は、余りを
    // 持ち越さない。elapsed は最大 0.25 秒 = 15 ステップ分ある
    // ため、そのまま残すと補間率 alpha が 1 を大きく超え (最大 7)、
    // 描画ポーズが未来へ数メートル飛んでしまう。初回起動時の
    // シェーダコンパイル等のひっかかりで起きやすく、「初回だけ
    // ガクつく」主因になる。余分な時間は捨てて、その分だけ
    // ゲーム内時間がゆっくり流れる扱いにする (暴走防止)。
    if (accumulator_ > kFixedStepSec) {
        accumulator_ = kFixedStepSec;
    }

    // --- 描画ポーズの補間: 1 ステップ前と現在を端数でつなぐ ---------------
    // 物理は 60Hz 刻みで車が約 1m ずつ進むため、ステップ値を
    // そのまま描くと、滑らかに動くカメラとの間で車が細かく
    // 震えて見える。補間して連続な軌跡として描くのが要る。
    // (以前、同速時は最新値へ寄せる方式を試したが、0/2 ステップ
    // フレームで段差が戻ってしまうため補間に戻した。)
    float alpha = clampf(float(accumulator_ / kFixedStepSec), 0.0f, 1.0f);  // 外挿はしない
    float dx = car.x - prevX_;
    float dz = car.z - prevZ_;
    if (dx * dx + dz * dz > 400.0f) alpha = 1.0f;  // リセット等のテレポートでは補間しない
    viewX_ = prevX_ + (car.x - prevX_) * alpha;
    viewY_ = prevY_ + (car.y - prevY_) * alpha;
    viewZ_ = prevZ_ + (car.z - prevZ_) * alpha;
    viewYaw_ = prevYaw_ + wrapAngle(car.yaw - prevYaw_) * alpha;
    viewSlip_ = prevSlip_ + (car.slipAngle - prevSlip_) * alpha;
    viewReady_ = true;

    // 対戦相手も同じ端数で補間する。
    for (Rival& rival : app.rivals()) {
        const Car& rc = rival.car;
        const float rdx = rc.x - rival.prevX;
        const float rdz = rc.z - rival.prevZ;
        const float a = (rdx * rdx + rdz * rdz > 400.0f) ? 1.0f : alpha;
        rival.viewX = rival.prevX + rdx * a;
        rival.viewY = rival.prevY + (rc.y - rival.prevY) * a;
        rival.viewZ = rival.prevZ + rdz * a;
        rival.viewYaw = rival.prevYaw + wrapAngle(rc.yaw - rival.prevYaw) * a;
    }
}

// ============================================================================
// 対戦相手
// ============================================================================

void RaceScene::stepRivals(float dt) {
    Game& app = game_;
    std::vector<Rival>& rivals = app.rivals();
    if (rivals.empty()) {
        return;
    }
    // 追い越し判断に使う「ほかの車」の一覧 (プレイヤーを含む)。
    std::vector<AiTraffic> traffic;
    traffic.push_back({app.car().x, app.car().z, app.car().ground.s});
    for (const Rival& r : rivals) traffic.push_back({r.car.x, r.car.z, r.car.ground.s});

    for (size_t i = 0; i < rivals.size(); ++i) {
        Rival& rival = rivals[i];
        std::vector<AiTraffic> others = traffic;
        others.erase(others.begin() + long(i) + 1);  // 自分を除く
        DriveInput input = rival.driver.drive(rival.car, app.track(), dt, others);
        if (rival.race.counting()) {
            input = DriveInput{};  // カウントダウン中はプレイヤーと同じく動けない
        }
        rival.prevX = rival.car.x;
        rival.prevY = rival.car.y;
        rival.prevZ = rival.car.z;
        rival.prevYaw = rival.car.yaw;
        rival.car.update(dt, input, app.track());
        rival.race.tick(rival.car.ground.s, app.track().length, dt);
    }
}

void RaceScene::resolveContacts() {
    // 車同士の接触: 車体を前後 2 つの円で近似し、重なった分だけ押し離す。
    // 後ろからぶつかった側は少し減速する (追突で前の車を押し出さないように)。
    constexpr float kRadius = 0.95f;   // 円の半径 (m)
    constexpr float kOffset = 1.15f;   // 円の中心の前後位置 (m)
    Game& app = game_;
    std::vector<Car*> cars;
    cars.push_back(&app.car());
    for (Rival& r : app.rivals()) cars.push_back(&r.car);
    if (cars.size() < 2) {
        return;
    }
    for (size_t a = 0; a < cars.size(); ++a) {
        for (size_t b = a + 1; b < cars.size(); ++b) {
            Car& ca = *cars[a];
            Car& cb = *cars[b];
            const float cdx = ca.x - cb.x;
            const float cdz = ca.z - cb.z;
            if (cdx * cdx + cdz * cdz > 36.0f) continue;  // 6m 以上離れていれば調べない
            // 4 組の円のうち、最も深く重なっている組で押し離す。
            float bestPen = 0.0f, nx = 0.0f, nz = 0.0f;
            for (int i = -1; i <= 1; i += 2) {
                for (int j = -1; j <= 1; j += 2) {
                    const float ax = ca.x + std::sin(ca.yaw) * kOffset * float(i);
                    const float az = ca.z + std::cos(ca.yaw) * kOffset * float(i);
                    const float bx = cb.x + std::sin(cb.yaw) * kOffset * float(j);
                    const float bz = cb.z + std::cos(cb.yaw) * kOffset * float(j);
                    const float d = std::hypot(ax - bx, az - bz);
                    const float pen = 2.0f * kRadius - d;
                    if (pen > bestPen && d > 1e-4f) {
                        bestPen = pen;
                        nx = (ax - bx) / d;
                        nz = (az - bz) / d;
                    }
                }
            }
            if (bestPen <= 0.0f) continue;
            ca.x += nx * bestPen * 0.5f;
            ca.z += nz * bestPen * 0.5f;
            cb.x -= nx * bestPen * 0.5f;
            cb.z -= nz * bestPen * 0.5f;
            // 後ろ側 (相手の方へ向かって進んでいる車) を少し減速させる。
            Car& rear = (std::sin(ca.yaw) * -nx + std::cos(ca.yaw) * -nz) > 0.0f ? ca : cb;
            rear.speed *= 0.985f;
            rear.velX *= 0.985f;
            rear.velZ *= 0.985f;
        }
    }
}

void RaceScene::updateCamera(float elapsed) {
    Game& app = game_;
    const Car& car = app.car();
    // カメラの向きはスリップ角に追随するが、物理のスリップはステップ
    // ごとに細かく揺れる (ドリフトの出入りやスティックの微入力) ため、
    // そのまま使うと視点が小刻みに振れて車体がブルブルして見える。
    // カメラ専用に平滑化した値を渡す (物理と車体の表示は生のまま)。
    float kSlip = 1.0f - std::exp(-10.0f * elapsed);
    float kAmt = 1.0f - std::exp(-12.0f * elapsed);
    camSlip_ += (car.slipAngle - camSlip_) * kSlip;
    camAmount_ += (car.driftAmount - camAmount_) * kAmt;
    app.camera().update({viewX_, viewY_, viewZ_}, viewYaw_, camSlip_, camAmount_,
                        app.track(), car.ground.s, elapsed);
}

// ============================================================================
// 音
// ============================================================================

RaceScene::CountdownEvents RaceScene::detectCountdown() {
    CountdownEvents events;
    const Race& race = game_.race();
    if (game_.paused()) {
        return events;  // ポーズ中は時間が止まっているので何も起きない
    }
    // 数字 (3・2・1) が変わった瞬間と、カウントが終わった瞬間 (GO) を拾う。
    const int num = race.counting() ? int(std::ceil(race.countdownLeft() / Race::kCountdownSlot)) : 0;
    events.tick = num > 0 && num != prevCountNum_;
    events.go = wasCounting_ && !race.counting();
    prevCountNum_ = num;
    wasCounting_ = race.counting();
    return events;
}

void RaceScene::updateSound(float elapsed, const DriveInput& raw, const DriveInput& input,
                            const CountdownEvents& countdown) {
    Game& app = game_;
    const Car& car = app.car();

    // --- 合図音: カウントダウンのランプ点灯と同時に鳴らす -------------------
    if (countdown.tick) app.playCue(SoundCue::CountdownTick);
    if (countdown.go) app.playCue(SoundCue::CountdownGo);

    // --- ポーズ中は静かにする -------------------------------------------------
    if (app.paused()) {
        app.silenceAudio();
        return;
    }

    // --- エンジン音: 回転数モデルを進め、その状態を音へ渡す -----------------
    // カウント中は駆動系を切り離した「空ぶかし」(生のスロットルで回る)。
    const bool counting = app.race().counting();
    const float throttle = counting ? raw.throttle : input.throttle;
    const float wheelSlip = car.drifting ? car.driftAmount : 0.0f;
    app.engine().update(elapsed, car.speed, throttle, wheelSlip, counting);
    app.setEngineSound(app.engine().sound(true));

    // --- タイヤ音: 滑りの「深さ」と「続いている時間」を求める ---------------
    // 深さ: ドリフト中はドリフト量とスリップ角の両方から (角度が深いほど強く)。
    //       ドリフト判定外でも、横に大きく流れたりスピンしたりしていれば鳴らす
    //       (低速では音声側で自然に小さくなる)。
    const float angle = clampf(std::fabs(car.slipAngle) / 0.9f, 0.0f, 1.0f);
    float depth = 0.0f;
    if (car.drifting) {
        depth = clampf(0.55f * car.driftAmount + 0.45f * angle, 0.0f, 1.0f);
    } else {
        const float lateral = (std::fabs(car.lateralSpeed) - 4.0f) / 10.0f;
        depth = clampf(std::max(lateral, angle * 0.8f - 0.1f), 0.0f, 0.8f);
    }
    if (!car.onAsphalt) depth *= 0.25f;  // 芝ではタイヤは鳴かない (擦れる音だけ)
    if (counting) depth = 0.0f;

    // 継続時間: 0.25 秒未満の途切れ (切り返しの一瞬など) は同じ滑りとして数える。
    if (depth > 0.12f) {
        slideTime_ += elapsed;
        slideGap_ = 0;
    } else {
        slideGap_ += elapsed;
        if (slideGap_ > 0.25f) slideTime_ = 0;
    }

    TireSound tire;
    tire.depth = depth;
    tire.duration = slideTime_;
    tire.speed = std::sqrt(car.velX * car.velX + car.velZ * car.velZ);
    app.setTireSound(tire);
}

// ============================================================================
// 振動
// ============================================================================

void RaceScene::updateRumble(float elapsed, const CountdownEvents& countdown) {
    Game& app = game_;
    const Car& car = app.car();
    // 継続もの (ドリフト・芝・壁擦り) は約 70ms ごとに短く送り直し、
    // 単発もの (壁ヒット・カウント・GO) は起きた瞬間に強く叩く。
    auto pulse = [](float low, float high, int ms) { input::rumble(low, high, ms); };
    if (app.paused() && !wasPaused_) {
        pulse(0.0f, 0.0f, 1);  // ポーズに入ったら振動を止める
    }
    wasPaused_ = app.paused();
    if (app.paused()) {
        return;
    }

    // カウントの数字が変わるたびに小刻みに、GO で強く。
    if (countdown.tick) {
        pulse(0.25f, 0.15f, 70);
    }
    if (countdown.go) {
        bool rocket = app.race().launchBoost() > 0;
        pulse(rocket ? 0.95f : 0.75f, rocket ? 0.6f : 0.45f, rocket ? 320 : 240);
    }
    // 壁ヒット: 衝突が強まった瞬間にだけ叩く (擦りは継続側で扱う)。
    if (car.wallImpact > 0.05f && car.wallImpact > prevWallImpact_ + 0.03f) {
        pulse(0.5f + car.wallImpact * 0.5f, 0.35f + car.wallImpact * 0.45f,
              int(130.0f + car.wallImpact * 130.0f));
        rumbleTimer_ = 0.05f;
    }
    prevWallImpact_ = car.wallImpact;

    // 継続振動: 候補の中で一番強いものを採用する。
    float low = 0.0f, high = 0.0f;
    if (car.wallContact && std::fabs(car.speed) > 5.0f) {
        low = 0.5f; high = 0.35f;  // 壁擦り: ゴリゴリした重い振動
    }
    if (car.drifting && car.driftAmount > 0.15f) {
        low = std::max(low, 0.28f + car.driftAmount * 0.45f);
        high = std::max(high, 0.18f + car.driftAmount * 0.35f);
    } else if (!car.onAsphalt && std::fabs(car.speed) > 8.0f) {
        low = std::max(low, 0.22f); high = std::max(high, 0.10f);  // 芝: 軽い凸凹
    }
    rumbleTimer_ -= elapsed;
    if (rumbleTimer_ <= 0.0f) {
        if (low > 0.0f || high > 0.0f) {
            pulse(low, high, 110);
        }
        rumbleTimer_ = 0.07f;
    }
}

// ============================================================================
// 描画
// ============================================================================

void RaceScene::draw() {
    Game& app = game_;
    app.setPlayerViewPose({viewX_, viewY_, viewZ_}, viewYaw_);  // ヘッドライトを補間ポーズに合わせる
    app.beginWorldDraw();
    app.drawWorld({viewX_, viewY_, viewZ_}, viewYaw_);
    app.drawRivals(true);
    app.drawGhost();
    app.drawEffects();
    if (app.diagMirror()) {
        app.drawMirror({viewX_, viewY_, viewZ_}, viewYaw_);  // 上部の後方カメラ
    }
    app.hud().draw(app);
}
