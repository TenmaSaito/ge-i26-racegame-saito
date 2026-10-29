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
#include "racescene.hpp"    // RaceScene
#include "resultscene.hpp"  // ResultScene
#include "titlescene.hpp"   // TitleScene
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
    switchScene(SceneId::Title);  // 起動直後はタイトルから
}

// シーンを実際に切り替える。前シーンの exit を呼んでから新しいシーンを
// 作り、enter で初期化する。生成はここに集約し、増えたらここへ足す。
void Game::switchScene(SceneId id) {
    if (scene_) {
        scene_->exit();
    }
    switch (id) {
        case SceneId::Title: scene_ = std::make_unique<TitleScene>(*this); break;
        case SceneId::Race: scene_ = std::make_unique<RaceScene>(*this); break;
        case SceneId::Result: scene_ = std::make_unique<ResultScene>(*this); break;
    }
    scene_->enter();
}

// 1 フレームの進行。切り替え予約があればフレームの頭で切り替え、
// あとは現在のシーンに委譲する (Game 自身は段取りを持たない)。
void Game::frame(double dtSeconds) {
    if (hasPendingScene_) {
        hasPendingScene_ = false;
        switchScene(pendingScene_);
    }
    if (scene_) {
        scene_->frame(dtSeconds);
    }
}

// FPS 表示の平滑化。瞬間値だと読みにくいので、少しずつ寄せる。
void Game::noteFps(double elapsed) {
    if (elapsed > 0.0001) {
        fpsSm_ += float(1.0 / elapsed - fpsSm_) * 0.05f;
    }
}

// ワールド描画の開始: 画面を消し、空を描き、投影と視点を設定する。
// 画面サイズの反映と表示 (Swap) は外側 (platform のループ) の担当。
void Game::beginWorldDraw() {
    renderer_->clearFrame();
    renderer_->drawSky();
    float aspect = float(renderer_->width()) /
                   float(renderer_->height() > 0 ? renderer_->height() : 1);
    Mat4 proj = perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.15f, 2200.0f);
    Mat4 view = lookAt(camera_.eye, camera_.look, {0, 1, 0});
    renderer_->beginWorld(mul(proj, view), camera_.eye);
}

// ワールド本体の描画 (背景・景観・タイヤ痕・影・車体・ホイール)。
// タイトル／レース／リザルトで共通。carPos/carYaw は描画用ポーズ。
void Game::drawWorld(Vec3 carPos, float carYaw) {
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
    Mat4 shadow = mul(translation(carPos.x, carPos.y + 0.08f, carPos.z), rotationY(carYaw));
    if (camera_.mode != 3) renderer_->draw(shadow_, shadow);

    // 車体の傾きは路面の平面に這わせる。前後・左右 4 点の路面高から
    // 面の傾きを求めるので、斜めに止まってもバンクと勾配の両方に合う。
    // 傾き自体は荷重移動として少し遅れて追わせる (即応するとカクつく)。
    {
        Vec3 fw{std::sin(carYaw), 0, std::cos(carYaw)};
        Vec3 rt{fw.z, 0, -fw.x};
        float preferS = car_.ground.found ? car_.ground.s : -1.0f;
        float hF = surfaceHeightAt(track_, carPos + fw * 2.2f, car_.y, preferS);
        float hB = surfaceHeightAt(track_, carPos - fw * 2.2f, car_.y, preferS);
        float hR = surfaceHeightAt(track_, carPos + rt * 1.0f, car_.y, preferS);
        float hL = surfaceHeightAt(track_, carPos - rt * 1.0f, car_.y, preferS);
        float pitchT = std::atan2(hF - hB, 4.4f);
        float rollT = std::atan2(hR - hL, 2.0f) -
                      clampf(car_.lateralSpeed * 0.009f, -0.12f, 0.12f);
        const float k = 1.0f - std::exp(-3.0f / 60.0f);
        bodyPitchSm_ += (pitchT - bodyPitchSm_) * k;
        bodyRollSm_ += (rollT - bodyRollSm_) * k;
    }
    Car viewCar = car_;
    viewCar.x = carPos.x; viewCar.y = carPos.y; viewCar.z = carPos.z; viewCar.yaw = carYaw;
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
}

// 煙と光跡の描画 (車の後、HUD の前)。
void Game::drawEffects() {
    effects_.drawSmoke(*renderer_, camera_);
    effects_.drawTrails(*renderer_);
}
