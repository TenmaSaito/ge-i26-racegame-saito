// ============================================================================
// racescene.cpp ― レースシーン (RaceScene) の実装
// 役割: 入力の意図を受けて状態を変え、物理を固定ステップで進め、
//       補間ポーズで描く。kRaceLaps 周を完了したらリザルトへ遷移する。
// ============================================================================

#include "racescene.hpp"

#include "game.hpp"
#include "hud.hpp"  // Hud (drawTitle / drawResult / draw)
#include "input.hpp"
#include "tuning.hpp"

#include <cmath>

namespace {
// 物理の固定ステップ (秒) と、1 フレームの経過時間の上限 (秒)。
// 上限はタブ復帰などの大きな隙間で物理が暴走しないための頭打ち。
constexpr double kFixedStepSec = 1.0 / 60.0;
constexpr double kMaxFrameSec = 0.25;
}  // namespace

void RaceScene::enter() {
    game_.resetRace();
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
    game_.setPaused(false);
    game_.setShowTuning(false);
}

void RaceScene::frame(double dtSeconds) {
    Game& app = game_;
    // --- 入力: イベントの解釈は入力アダプタ (外側) が済ませ、「意図」だけ届く ---
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
    bool reset = ui.reset;
    if (ui.cameraNext) app.camera().cycle(4);
    if (ui.cameraNext3) app.camera().cycle(3);
    if (ui.pauseToggle) app.setPaused(!app.paused());
    if (app.paused() && ui.tuningToggle) {
        // ポーズ中の T はタイトルへ戻る (調整パネルの切替より優先)。
        app.setPaused(false);
        app.requestScene(SceneId::Title);
        return;
    }
    if (ui.helpToggle) app.setShowHelp(!app.showHelp());
    if (ui.debugToggle) app.setShowDriftDebug(!app.showDriftDebug());
    // 車種 (1/2/3) は選択画面でのみ変更する。レース中の誤操作で
    // 特性が突然変わらないように、ここでは受け付けない。

    PadPulse padPulse;
    DriveInput rawInput = input::sampleDrive(padPulse, app.car().drifting);
    app.setLastInput(rawInput);
    DriveInput input = rawInput;
    if (app.showTuning()) {
        input = DriveInput{};  // チューニング中は矢印キーを運転に使わない
    }
    if (app.race().counting()) {
        // カウントダウン中は車を動かせない。ただし生のスロットルは
        // 空ぶかしの音とロケットスタートの溜めに使うので rawInput に残す。
        input = DriveInput{};
    }
    app.race().setLaunchHeld(app.race().counting() && rawInput.throttle > 0.25f);
    if (padPulse.reset) reset = true;
    if (padPulse.pause) app.setPaused(!app.paused());
    if (padPulse.camera) app.camera().cycle(3);
    if (reset) {
        app.resetRace();
        // リセット直後も補間の始点と描画ポーズを車へ同期する
        // (enter() と同じ理由。古い座標からの補間で表示が跳ぶのを防ぐ)。
        const Car& rc = app.car();
        prevX_ = viewX_ = rc.x;
        prevY_ = viewY_ = rc.y;
        prevZ_ = viewZ_ = rc.z;
        prevYaw_ = viewYaw_ = rc.yaw;
        prevSlip_ = viewSlip_ = rc.slipAngle;
        camSlip_ = rc.slipAngle;
        camAmount_ = rc.driftAmount;
        accumulator_ = 0;
        viewReady_ = true;
    }

    // 経過時間の整形: 上限で頭打ちし、FPS は Game 側で平滑化する。
    double elapsed = dtSeconds;
    if (elapsed > kMaxFrameSec) elapsed = kMaxFrameSec;
    app.noteFps(elapsed);
    if (elapsed < 0) elapsed = 0;

    // --- 物理: 固定 60Hz で進め、余りは補間に使う --------------------------
    Car& car = app.car();
    if (app.paused()) {
        accumulator_ = 0;
        viewX_ = car.x; viewY_ = car.y; viewZ_ = car.z;
        viewYaw_ = car.yaw; viewSlip_ = car.slipAngle;
        viewReady_ = true;
    } else {
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
        // 描画ポーズの補間: 1 ステップ前と現在を端数でつなぐ。
        // 物理は 60Hz 刻みで車が約 1m ずつ進むため、ステップ値を
        // そのまま描くと、滑らかに動くカメラとの間で車が細かく
        // 震えて見える。補間して連続な軌跡として描くのが要る。
        // (以前、同速時は最新値へ寄せる方式を試したが、0/2 ステップ
        // フレームで段差が戻ってしまうため補間に戻した。)
        float alpha = float(accumulator_ / kFixedStepSec);
        if (alpha < 0.0f) alpha = 0.0f;  // 念のため 0〜1 に制限する
        if (alpha > 1.0f) alpha = 1.0f;  // (外挿は描画ポーズを飛ばす)
        float dx = car.x - prevX_;
        float dz = car.z - prevZ_;
        if (dx * dx + dz * dz > 400.0f) alpha = 1.0f;  // リセット等のテレポートでは補間しない
        viewX_ = prevX_ + (car.x - prevX_) * alpha;
        viewY_ = prevY_ + (car.y - prevY_) * alpha;
        viewZ_ = prevZ_ + (car.z - prevZ_) * alpha;
        float dyaw = car.yaw - prevYaw_;
        while (dyaw > 3.14159265f) dyaw -= 6.2831853f;
        while (dyaw < -3.14159265f) dyaw += 6.2831853f;
        viewYaw_ = prevYaw_ + dyaw * alpha;
        viewSlip_ = prevSlip_ + (car.slipAngle - prevSlip_) * alpha;
        viewReady_ = true;
    }
    // --- 演出とカメラ -------------------------------------------------------
    if (!app.paused()) {
        app.effects().update(car, float(elapsed));
    }
    // カメラの向きはスリップ角に追随するが、物理のスリップはステップ
    // ごとに細かく揺れる (ドリフトの出入りやスティックの微入力) ため、
    // そのまま使うと視点が小刻みに振れて車体がブルブルして見える。
    // カメラ専用に平滑化した値を渡す (物理と車体の表示は生のまま)。
    {
        float kSlip = 1.0f - std::exp(-10.0f * float(elapsed));
        float kAmt = 1.0f - std::exp(-12.0f * float(elapsed));
        camSlip_ += (car.slipAngle - camSlip_) * kSlip;
        camAmount_ += (car.driftAmount - camAmount_) * kAmt;
    }
    app.camera().update({viewX_, viewY_, viewZ_}, viewYaw_, camSlip_, camAmount_,
                        app.track(), car.ground.s, float(elapsed));

    // --- 音: タイヤのスリップ音 (仮) ---------------------------------------
    // ドリフト中はドリフト量、通常時は横滑りが大きいときだけ少し鳴らす。
    // 速度が低いときは鳴らさない (止まり際のノイズ防止)。
    {
        float slip = car.drifting ? clampf(car.driftAmount, 0.0f, 1.0f)
                                  : clampf((std::fabs(car.lateralSpeed) - 6.0f) / 12.0f, 0.0f, 0.6f);
        slip *= clampf(std::fabs(car.speed) / 15.0f, 0.0f, 1.0f);
        app.setSlip(app.paused() ? 0.0f : slip);
        // エンジン音: 速度で音程の段、スロットルで音量。アイドル分として
        // load に下限を持たせ、レース中は常時鳴らす (停止合図は 0,0)。
        if (app.paused()) {
            app.setEngine(0, 0);
        } else if (app.race().counting()) {
            // カウント中は空ぶかし: 踏み込みで回転が少し上がり、音も張る。
            float rev = clampf(rawInput.throttle, 0.0f, 1.0f);
            app.setEngine(0.10f + rev * 0.30f, 0.15f + rev * 0.85f);
        } else {
            float rpm = clampf(std::fabs(car.speed) / 58.0f, 0.0f, 1.0f);
            float load = 0.12f + clampf(input.throttle, 0.0f, 1.0f) * 0.88f;
            app.setEngine(rpm, load);
        }
    }

    // --- 振動: パッドへの触覚フィードバック ---------------------------------
    // 継続もの (ドリフト・芝・壁擦り) は約 70ms ごとに短く送り直し、
    // 単発もの (壁ヒット・カウント・GO) は起きた瞬間に強く叩く。
    {
        auto pulse = [](float low, float high, int ms) { input::rumble(low, high, ms); };
        if (app.paused() && !wasPaused_) {
            pulse(0.0f, 0.0f, 1);  // ポーズに入ったら振動を止める
        }
        wasPaused_ = app.paused();
        if (!app.paused()) {
            // カウントの数字が変わるたびに小刻みに、GO で強く。
            int num = app.race().counting()
                          ? int(std::ceil(app.race().countdownLeft() / Race::kCountdownSlot))
                          : 0;
            if (num != prevCountNum_ && num > 0) {
                pulse(0.25f, 0.15f, 70);
            }
            prevCountNum_ = num;
            if (wasCounting_ && !app.race().counting()) {
                bool rocket = app.race().launchBoost() > 0;
                pulse(rocket ? 0.95f : 0.75f, rocket ? 0.6f : 0.45f, rocket ? 320 : 240);
            }
            wasCounting_ = app.race().counting();
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
            rumbleTimer_ -= float(elapsed);
            if (rumbleTimer_ <= 0.0f) {
                if (low > 0.0f || high > 0.0f) {
                    pulse(low, high, 110);
                }
                rumbleTimer_ = 0.07f;
            }
        }
    }

    // --- ゴール判定: 規定周回を完了したらリザルトへ -------------------------
    if (app.race().completed() >= kRaceLaps) {
        app.requestScene(SceneId::Result);
    }

    // --- 描画 ---------------------------------------------------------------
    app.beginWorldDraw();
    app.drawWorld({viewX_, viewY_, viewZ_}, viewYaw_);
    app.drawGhost();
    app.drawEffects();
    app.drawMirror({viewX_, viewY_, viewZ_}, viewYaw_);  // 上部の後方カメラ
    app.hud().draw(app);
}
