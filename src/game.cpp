// ============================================================================
// game.cpp ― ゲームクラス (Game) の実装
// 役割: 初期化 (メッシュ構築と GPU アップロード) と、1 フレームの進行
//       (入力→物理→演出→カメラ→描画) の段取り。個別の処理は各クラス
//       (Car / Race / Effects / Orbit / Hud) に委譲し、ここは統括だけ。
// レイヤー: アプリケーション層。SDL / GL / platform は直接知らず、
//       入力は input.hpp のポート、描画は IRenderer 経由で使う。
//       時間の計測・画面サイズ・表示 (Swap) は外側 (platform) の担当。
// ============================================================================

#include "game.hpp"

#include "carmodel.hpp"  // buildCarNose / buildTailLamps
#include "hud.hpp"       // Hud
#include "input.hpp"     // input::pollUi / sampleDrive (ポート)
#include "scenery.hpp"   // SceneryBuilder
#include "tuning.hpp"    // tuneItems / serializeTuning

#include <cmath>
#include <utility>

namespace {
// 物理の固定ステップ (秒)。表示レートによらず挙動を一定に保つため、
// 物理だけは 60Hz 固定で進め、描画側は補間でつなぐ。
constexpr double kFixedStepSec = 1.0 / 60.0;
// 1 フレームの経過時間の上限 (秒)。タブ復帰などの大きな隙間で
// 物理が暴走しないよう、この値で頭打ちにする。
constexpr double kMaxFrameSec = 0.25;
}  // namespace

Game::Game(std::string title, Track track)
    : title_(std::move(title)), track_(std::move(track)), hud_(std::make_unique<Hud>()) {}

Game::~Game() = default;

// レースを初期状態に戻す。車・計時・演出・カメラをまとめてリセットし、
// 次の周回の計測がきれいに始まるようにする。
void Game::resetRace() {
    car_.reset(track_);
    race_.reset();
    camera_.ready = false;  // カメラは次のフレームで即座に置き直す
    effects_.clear();       // 煙・光跡・タイヤ痕も残さない
}

// 現在のチューニング値を保存する。実務は差し込み先 (platform) の担当で、
// ここは「保存してほしい」という依頼だけを出す。
void Game::saveTuningNow() {
    if (saveSink_) {
        saveSink_(serializeTuning());
    }
}

// 車種を切り替える。特性の倍率と、その車の基準チューニングに差し替え、
// 次回起動時にも残るよう保存まで行う。
void Game::applyCarSpec(int index) {
    carSpec_ = index;
    const CarSpec& spec = kCarSpecs[index];
    car_.powerMul = spec.powerMul;
    car_.topSpeedMul = spec.topSpeedMul;
    car_.steerMul = spec.steerMul;
    driftTuning = spec.tuning;
    clampTuning();
    saveTuningNow();
}

// 初期化: コースに合わせたメッシュを構築し、描画実装へアップロードする。
// 景観と路面はチャンク (区間) 単位で持ち、描画時に距離カリングする。
void Game::init() {
    // 路面チャンクも景観チャンクと同じリストでカリング描画する。
    for (size_t i = 0; i < track_.visualChunks.size(); ++i) {
        scenery_.push_back(renderer_->upload(track_.visualChunks[i]));
        sceneryCenters_.push_back(track_.visualCenters[i]);
    }
    {
        // 景観 (木・建物・ガードレール) は SceneryBuilder がチャンク分割して構築。
        SceneryBuilder builder(track_);
        for (SceneryChunk& chunk : builder.build()) {
            scenery_.push_back(renderer_->upload(chunk.mesh));
            sceneryCenters_.push_back(chunk.center);
        }
        backdrop_ = renderer_->upload(builder.buildBackdrop());
        {
            CpuMesh noseMesh;
            buildCarNose(noseMesh);
            nose_ = renderer_->upload(noseMesh);
        }
        // テールランプは 3 状態 (消灯／ドリフト点灯／ブレーキ強点灯) を作り分ける。
        CpuMesh lampMesh;
        buildTailLamps(lampMesh, {0.22f, 0.03f, 0.03f});
        lampsOff_ = renderer_->upload(lampMesh);
        lampMesh.vertices.clear();
        lampMesh.indices.clear();
        buildTailLamps(lampMesh, {1.15f, 0.10f, 0.10f});
        lampsOn_ = renderer_->upload(lampMesh);
        lampMesh.vertices.clear();
        lampMesh.indices.clear();
        buildTailLamps(lampMesh, {2.6f, 0.22f, 0.20f});
        lampsBright_ = renderer_->upload(lampMesh);
    }
    CpuMesh body;
    CpuMesh wheel;
    CpuMesh shadow;
    buildCarBody(body);
    buildCarWheel(wheel);
    buildCarShadow(shadow);
    body_ = renderer_->upload(body);
    wheel_ = renderer_->upload(wheel);
    shadow_ = renderer_->upload(shadow);
    car_.reset(track_);
}

// 1 フレームの進行。dtSeconds は外側が測った経過時間。
void Game::frame(double dtSeconds) {
    // --- 入力: イベントの解釈は入力アダプタ (外側) が済ませ、「意図」だけ届く ---
    UiActions ui = input::pollUi(showTuning_);
    if (ui.quit) {
        running_ = false;
    }
    if (ui.tuningToggle) {
        showTuning_ = !showTuning_;
    }
    if (showTuning_) {
        // 調整パネル操作: 項目移動と値の増減。値はその場で物理へ反映され、
        // 変更のたびに保存する (閉じ忘れても残るように)。
        if (ui.tuneSelect != 0) {
            tuneIndex_ = (tuneIndex_ + ui.tuneSelect + kTuneItemCount) % kTuneItemCount;
        }
        if (ui.tuneAdjust != 0.0f) {
            TuneItem& item = tuneItems[tuneIndex_];
            *item.value = clampf(*item.value + ui.tuneAdjust * item.step, item.lo, item.hi);
            saveTuningNow();
        }
    }
    bool reset = ui.reset;
    if (ui.cameraNext) camera_.cycle(4);
    if (ui.cameraNext3) camera_.cycle(3);
    if (ui.pauseToggle) paused_ = !paused_;
    if (ui.helpToggle) showHelp_ = !showHelp_;
    if (ui.debugToggle) showDriftDebug_ = !showDriftDebug_;
    if (ui.carSpec >= 0) applyCarSpec(ui.carSpec);

    PadPulse padPulse;
    DriveInput input = input::sampleDrive(padPulse, car_.drifting);
    lastInput_ = input;
    if (showTuning_) {
        input = DriveInput{};  // チューニング中は矢印キーを運転に使わない
    }
    if (padPulse.reset) reset = true;
    if (padPulse.pause) paused_ = !paused_;
    if (padPulse.camera) camera_.cycle(3);
    if (reset) {
        resetRace();
    }

    // 経過時間の整形: 上限で頭打ちし、FPS 表示は平滑化して持つ。
    double elapsed = dtSeconds;
    if (elapsed > kMaxFrameSec) elapsed = kMaxFrameSec;
    if (elapsed > 0.0001) fpsSm_ += float(1.0 / elapsed - fpsSm_) * 0.05f;
    if (elapsed < 0) elapsed = 0;

    // --- 物理: 固定 60Hz で進め、余りは補間に使う --------------------------
    if (paused_) {
        accumulator_ = 0;
        viewX_ = car_.x; viewY_ = car_.y; viewZ_ = car_.z;
        viewYaw_ = car_.yaw; viewSlip_ = car_.slipAngle;
        viewReady_ = true;
    } else {
        prevX_ = car_.x; prevY_ = car_.y; prevZ_ = car_.z;
        prevYaw_ = car_.yaw; prevSlip_ = car_.slipAngle;
        accumulator_ += elapsed;
        int guard = 0;
        while (accumulator_ >= kFixedStepSec && guard < 8) {
            car_.update(float(kFixedStepSec), input, track_);
            race_.tick(car_.ground.s, track_.length, float(kFixedStepSec));
            accumulator_ -= kFixedStepSec;
            ++guard;
        }
        // 描画ポーズの補間: 1 ステップ前と現在を端数でつなぐ。
        float alpha = float(accumulator_ / kFixedStepSec);
        float dx = car_.x - prevX_;
        float dz = car_.z - prevZ_;
        if (dx * dx + dz * dz > 400.0f) alpha = 1.0f;  // リセット等のテレポートでは補間しない
        viewX_ = prevX_ + (car_.x - prevX_) * alpha;
        viewY_ = prevY_ + (car_.y - prevY_) * alpha;
        viewZ_ = prevZ_ + (car_.z - prevZ_) * alpha;
        float dyaw = car_.yaw - prevYaw_;
        while (dyaw > 3.14159265f) dyaw -= 6.2831853f;
        while (dyaw < -3.14159265f) dyaw += 6.2831853f;
        viewYaw_ = prevYaw_ + dyaw * alpha;
        viewSlip_ = prevSlip_ + (car_.slipAngle - prevSlip_) * alpha;
        viewReady_ = true;
    }
    // --- 演出とカメラ -------------------------------------------------------
    if (!paused_) {
        effects_.update(car_, float(elapsed));
    }
    camera_.update({viewX_, viewY_, viewZ_}, viewYaw_, viewSlip_, car_.driftAmount,
                   track_, car_.ground.s, float(elapsed));

    // --- 描画 ---------------------------------------------------------------
    // 画面サイズの反映と表示 (Swap) は外側 (platform のループ) の担当。
    // ここでは renderer が保持する現在サイズで画角だけを決める。
    renderer_->clearFrame();
    renderer_->drawSky();

    float aspect = float(renderer_->width()) /
                   float(renderer_->height() > 0 ? renderer_->height() : 1);
    Mat4 proj = perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.15f, 2200.0f);
    Mat4 view = lookAt(camera_.eye, camera_.look, {0, 1, 0});
    renderer_->beginWorld(mul(proj, view), camera_.eye);

    Mat4 identity;
    renderer_->draw(backdrop_, identity);
    // 景観はチャンク単位で距離カリング (1000m 以内だけ描く)。
    for (size_t i = 0; i < scenery_.size(); ++i) {
        float dx = sceneryCenters_[i].x - camera_.eye.x;
        float dz = sceneryCenters_[i].z - camera_.eye.z;
        if (dx * dx + dz * dz < 1000.0f * 1000.0f) renderer_->draw(scenery_[i], identity);
    }
    // タイヤ痕は路面に残るものなので、車より先 (景観の直後) に描く。
    effects_.drawMarks(*renderer_);
    Mat4 shadow = mul(translation(viewX_, viewY_ + 0.08f, viewZ_), rotationY(viewYaw_));
    if (camera_.mode != 3) renderer_->draw(shadow_, shadow);

    // 車体の傾きは路面の平面に這わせる。前後・左右 4 点の路面高から
    // 面の傾きを求めるので、斜めに止まってもバンクと勾配の両方に合う。
    // 傾き自体は荷重移動として少し遅れて追わせる (即応するとカクつく)。
    {
        Vec3 pos{viewX_, viewY_, viewZ_};
        Vec3 fw{std::sin(viewYaw_), 0, std::cos(viewYaw_)};
        Vec3 rt{fw.z, 0, -fw.x};
        float preferS = car_.ground.found ? car_.ground.s : -1.0f;
        float hF = surfaceHeightAt(track_, pos + fw * 2.2f, car_.y, preferS);
        float hB = surfaceHeightAt(track_, pos - fw * 2.2f, car_.y, preferS);
        float hR = surfaceHeightAt(track_, pos + rt * 1.0f, car_.y, preferS);
        float hL = surfaceHeightAt(track_, pos - rt * 1.0f, car_.y, preferS);
        float pitchT = std::atan2(hF - hB, 4.4f);
        float rollT = std::atan2(hR - hL, 2.0f) -
                      clampf(car_.lateralSpeed * 0.009f, -0.12f, 0.12f);
        const float k = 1.0f - std::exp(-3.0f / 60.0f);
        bodyPitchSm_ += (pitchT - bodyPitchSm_) * k;
        bodyRollSm_ += (rollT - bodyRollSm_) * k;
    }
    Car viewCar = car_;
    viewCar.x = viewX_; viewCar.y = viewY_; viewCar.z = viewZ_; viewCar.yaw = viewYaw_;
    Mat4 model = carModelMatrix(viewCar, bodyPitchSm_, bodyRollSm_);
    model = mul(translation(0, 0.05f, 0), model);  // 平面に這わせた際の角のめり込み防止
    if (camera_.mode == 3) {
        renderer_->draw(nose_, model);
    } else {
        renderer_->draw(body_, model);
        // テールランプ: ブレーキで強点灯、ドリフト中は点灯、それ以外は消灯色。
        const GpuMesh& lamps = lastInput_.brake > 0.05f ? lampsBright_
                               : car_.drifting        ? lampsOn_
                                                      : lampsOff_;
        renderer_->draw(lamps, model);
    }
    const float wheelX = 0.82f;
    const float wheelZ[2] = {1.42f, -1.38f};
    const float steerForWheel[2] = {car_.steerAngle, 0};
    for (int i = 0; i < 2; ++i) {
        for (int side = -1; side <= 1; side += 2) {
            Mat4 place = mul(translation(float(side) * wheelX, 0.34f, wheelZ[i]),
                             mul(rotationY(steerForWheel[i]), rotationX(car_.wheelSpin)));
            renderer_->draw(wheel_, mul(model, place));
        }
    }
    effects_.drawSmoke(*renderer_, camera_);
    effects_.drawTrails(*renderer_);
    hud_->draw(*this);
}
