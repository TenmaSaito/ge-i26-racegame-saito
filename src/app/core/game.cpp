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
#include "carobject.hpp"  // CarObject ("Player" として登録)
#include "hud.hpp"       // Hud
#include "racescene.hpp"    // RaceScene
#include "selectscene.hpp"  // SelectScene
#include "resultscene.hpp"  // ResultScene
#include "titlescene.hpp"   // TitleScene
#include "input.hpp"     // input::pollUi / sampleDrive (ポート)
#include "scenery.hpp"   // SceneryBuilder
#include "tuning.hpp"    // tuneItems / serializeTuning

#include <algorithm>
#include <cmath>
#include <utility>

Game::Game(std::string initialStem)
    : initialStem_(std::move(initialStem)), hud_(std::make_unique<Hud>()) {}

Game::~Game() = default;

// レースを初期状態に戻す。車・計時・演出・カメラをまとめてリセットし、
// 次の周回の計測がきれいに始まるようにする。
void Game::resetRace() {
    car_.reset(track_);
    race_.reset();
    race_.startCountdown();  // 3・2・1 の後にスタート
    // エンジンはアイドルから。ギア比の基準は車種ごとの実際の最高速。
    engine_.reset(std::min(kCarMaxSpeed, kCarMaxSpeed * car_.topSpeedMul));
    camera_.ready = false;  // カメラは次のフレームで即座に置き直す
    effects_.clear();       // 煙・光跡・タイヤ痕も残さない
    ghost_.discard();       // 記録中ラップは捨てる (保存済みゴーストは残す)
    profileRecorder_.reset(track_.length);  // 区間速度の記録もやり直す
    viewPoseValid_ = false;  // 描画用ポーズは次のレースフレームで設定される
    setupRivals();          // 対戦相手をグリッドへ並べ直す
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
    rebuildCarMeshes();  // 色と形も車種に合わせる
}

// 車の見た目 (ボディ/ホイール/影) を現在の車種で作り直す。
void Game::rebuildCarMeshes() {
    if (!renderer_) {
        return;  // 初期化前 (applyCarSpec が先に呼ばれた場合) は何もしない
    }
    CpuMesh body;
    CpuMesh wheel;
    CpuMesh shadow;
    buildCarBody(body, carSpec_);
    buildCarDetails(body, carSpec_);   // キャノピーや空力部品などの作り込み
    buildCarWheel(wheel);
    buildWheelDetails(wheel, carSpec_);  // センターハブ
    buildCarShadow(shadow);
    body_ = renderer_->upload(body);
    wheel_ = renderer_->upload(wheel);
    shadow_ = renderer_->upload(shadow);
    // 対戦相手の車: 形はプレイヤーと同じ車種、色だけ塗り分ける。
    for (int i = 0; i < kMaxRivals; ++i) {
        const Vec3 color = rivalColor(i);
        CpuMesh rivalBody;
        CpuMesh rivalWheel;
        buildCarBody(rivalBody, carSpec_, &color);
        buildCarDetails(rivalBody, carSpec_, &color);
        buildCarWheel(rivalWheel);
        buildWheelDetails(rivalWheel, carSpec_, &color);
        rivalBody_[i] = renderer_->upload(rivalBody);
        rivalWheel_[i] = renderer_->upload(rivalWheel);
    }
}

// 初期化: 車まわりのメッシュ (コースに依らない) を作ってアップロードし、
// 最後に初期コースを読み込んでタイトルへ入る。
void Game::init() {
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
        lampMesh.vertices.clear();
        lampMesh.indices.clear();
        buildHeadLamps(lampMesh, {2.4f, 2.3f, 2.0f});
        headLamps_ = renderer_->upload(lampMesh);
    rebuildCarMeshes();
    // 初期コースの読み込み (失敗したら既定コースへ落とす)。
    if (!loadCourse(initialStem_)) {
        loadCourse("suzuka");
    }
    switchScene(SceneId::Title);  // 起動直後はタイトルから
}

// コースを読み直す。路面・背景・景観メッシュとオブジェクト登録を
// 丸ごと作り直し、車をスタートへ置き直す。古い GPU メッシュは
// 差し替えで破棄される (切替は頻繁ではないので明示解放はしない)。
bool Game::loadCourse(const std::string& stem) {
    if (!trackLoader_) {
        return false;
    }
    Track track;
    if (!trackLoader_(stem, track)) {
        return false;
    }
    track_ = std::move(track);
    courseStem_ = stem;
    title_ = "Suzuka Circuit";
    for (int i = 0; i < kCourseCount; ++i) {
        if (stem == kCourses[i].stem) {
            title_ = kCourses[i].name;
        }
    }
    // 前コースの景観メッシュとオブジェクト登録を捨ててから作り直す。
    scenery_.clear();
    sceneryCenters_.clear();
    objects_.clear();
    // 路面チャンクも景観チャンクと同じリストでカリング描画する。
    for (size_t i = 0; i < track_.visualChunks.size(); ++i) {
        scenery_.push_back(renderer_->upload(track_.visualChunks[i]));
        sceneryCenters_.push_back(track_.visualCenters[i]);
    }
    SceneryBuilder& builder = sceneryBuilder_;  // メンバを使い回す (高さ参照用)
    for (SceneryChunk& chunk : builder.build(objects_)) {
        scenery_.push_back(renderer_->upload(chunk.mesh));
        sceneryCenters_.push_back(chunk.center);
    }
    backdrop_ = renderer_->upload(builder.buildBackdrop());
    ghost_.clear();  // コースが変われば前コースのゴーストは意味がない
    // プレイヤー車もオブジェクトとして登録する (Find("Player") で引ける)。
    objects_.add<CarObject>("Player", car_);
    resetRace();
    requestSharedRecords();  // このコースの共有記録を取りに行く (届けば学習に使う)
    return true;
}

// ゴール結果を記録する: ランキング・順位・走行記録 (学習用) と、
// 対戦相手の一覧 (これまで戦った人数)。記録はオンラインへも共有する。
int Game::recordResult() {
    lastOutcome_ = RaceOutcome{};
    if (laps_ <= 0 || race_.completed() < laps_ || race_.total() <= 0) {
        return -1;  // フリー走行や途中終了は記録しない
    }
    int rank = ranking_.addResult(rankingKey(courseStem_, laps_), race_.total());
    if (rankingSink_) {
        rankingSink_(ranking_.serialize());
    }

    // --- 順位と、対戦の学習 (勝敗で AI のペースを調整) ----------------------
    lastOutcome_.position = playerPosition();
    lastOutcome_.entrants = 1 + static_cast<int>(rivals_.size());
    std::vector<std::string> opponents;
    for (const Rival& rival : rivals_) opponents.push_back(rival.name);
    recordBook_.noteRace(int(aiLevel_), opponents, lastOutcome_.position, lastOutcome_.entrants);

    // --- 走行記録 (区間速度つき)。手元に残し、オンラインへ共有する ---------
    RaceRecord record;
    record.name = playerName();
    record.course = courseStem_;
    record.car = carSpec_;
    record.level = rivals_.empty() ? -1 : int(aiLevel_);
    record.laps = laps_;
    record.total = race_.total();
    record.bestLap = race_.best();
    record.profile = profileRecorder_.bestProfile();
    if (record.valid()) {
        recordBook_.addOwn(record);
        if (recordService_ && recordService_->available()) {
            recordService_->submit(record.serialize());
            lastOutcome_.shared = true;
        }
    }
    saveRecords();
    return rank;
}

// シーンを実際に切り替える。前シーンの exit を呼んでから新しいシーンを
// 作り、enter で初期化する。生成はここに集約し、増えたらここへ足す。
void Game::switchScene(SceneId id) {
    if (scene_) {
        scene_->exit();
    }
    switch (id) {
        case SceneId::Title: scene_ = std::make_unique<TitleScene>(*this); break;
        case SceneId::Select: scene_ = std::make_unique<SelectScene>(*this); break;
        case SceneId::Race: scene_ = std::make_unique<RaceScene>(*this); break;
        case SceneId::Result: scene_ = std::make_unique<ResultScene>(*this); break;
    }
    scene_->enter();
}

// 1 フレームの進行。切り替え予約があればフレームの頭で切り替え、
// あとは現在のシーンに委譲する (Game 自身は段取りを持たない)。
void Game::frame(double dtSeconds) {
    lastDt_ = float(dtSeconds);
    ++frameNo_;
    // オブジェクトの一括更新 (動く実体が増えたらここで一斉に進む)。
    objects_.updateAll(float(dtSeconds));
    if (hasPendingScene_) {
        hasPendingScene_ = false;
        switchScene(pendingScene_);
    }
    pollSharedRecords();  // オンラインの記録が届いていれば取り込む
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
    applyLighting();
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
    if (sceneryEnabled_) {
        if (drawBackdrop_) {
            renderer_->draw(backdrop_, identity);
        }
        // 景観はチャンク単位で距離カリング (通常 900m、ミラー時は 420m)。
        for (size_t i = 0; i < scenery_.size(); ++i) {
            float dx = sceneryCenters_[i].x - camera_.eye.x;
            float dz = sceneryCenters_[i].z - camera_.eye.z;
            if (dx * dx + dz * dz < cullRadius_ * cullRadius_) renderer_->draw(scenery_[i], identity);
        }
    }
    // タイヤ痕は路面に残るものなので、車より先 (景観の直後) に描く。
    effects_.drawMarks(*renderer_);
    Mat4 shadow = mul(translation(carPos.x, carPos.y + 0.08f, carPos.z), rotationY(carYaw));
    if (camera_.mode != 3) renderer_->draw(shadow_, shadow);

    // 車体の傾きは路面の平面に這わせる。前後・左右 4 点の路面高から
    // 面の傾きを求めるので、斜めに止まってもバンクと勾配の両方に合う。
    // 路面高はコースのサンプル間隔 (約 4m) で折れ曲がるため、短い
    // ベースラインで拾うと高速時に目標角度が細かく跳ねて車体が
    // ブルブルする。前後 ±3.6m の長いベースラインで平均し、平滑化も
    // 実フレーム時間で 6/s に強めて抑える。更新は 1 フレームに 1 回
    // (ミラーの二度目の描画では進めない)。
    if (tiltFrame_ != frameNo_) {
        tiltFrame_ = frameNo_;
        Vec3 fw{std::sin(carYaw), 0, std::cos(carYaw)};
        Vec3 rt{fw.z, 0, -fw.x};
        float preferS = car_.ground.found ? car_.ground.s : -1.0f;
        float hF = surfaceHeightAt(track_, carPos + fw * 3.6f, carPos.y, preferS);
        float hB = surfaceHeightAt(track_, carPos - fw * 3.6f, carPos.y, preferS);
        float hR = surfaceHeightAt(track_, carPos + rt * 1.3f, carPos.y, preferS);
        float hL = surfaceHeightAt(track_, carPos - rt * 1.3f, carPos.y, preferS);
        float pitchT = std::atan2(hF - hB, 7.2f);
        float rollT = std::atan2(hR - hL, 2.6f) -
                      clampf(car_.lateralSpeed * 0.009f, -0.12f, 0.12f);
        const float k = 1.0f - std::exp(-6.0f * lastDt_);
        bodyPitchSm_ += (pitchT - bodyPitchSm_) * k;
        bodyRollSm_ += (rollT - bodyRollSm_) * k;
    }
    Car viewCar = car_;
    viewCar.x = carPos.x; viewCar.y = carPos.y; viewCar.z = carPos.z; viewCar.yaw = carYaw;
    // 診断 (7 キー): 傾きを切ると路面平面からの pitch/roll を無効化できる。
    Mat4 model = carModelMatrix(viewCar, diagTilt_ ? bodyPitchSm_ : 0.0f,
                                diagTilt_ ? bodyRollSm_ : 0.0f);
    model = mul(translation(0, 0.05f, 0), model);  // 平面に這わせた際の角のめり込み防止
    if (camera_.mode == 3) {
        renderer_->draw(nose_, model);
    } else {
        renderer_->draw(body_, model);
        // テールランプ: ブレーキで強点灯、ドリフト中と夜は点灯、それ以外は消灯色。
        const GpuMesh& lamps = lastInput_.brake > 0.05f    ? lampsBright_
                               : (car_.drifting || night_) ? lampsOn_
                                                           : lampsOff_;
        renderer_->draw(lamps, model);
        if (night_) {
            renderer_->draw(headLamps_, model);
        }
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

// ゴーストの描画。保存済みゴーストがあり、計測ラップ中のときだけ、
// 現在のラップ経過時刻に対応するポーズを半透明 (0.42) で描く。
void Game::drawGhost() {
    if (!ghost_.hasGhost() || !race_.timing()) {
        return;
    }
    Vec3 pos;
    float yaw = 0;
    if (!ghost_.poseAt(race_.lap(), pos, yaw)) {
        return;
    }
    Mat4 model = mul(translation(pos.x, pos.y + 0.05f, pos.z), rotationY(yaw));
    renderer_->drawAlpha(body_, model, 0.42f);
    // ホイールも薄く出す (ステア・回転は再現しない簡易版)。
    const float wheelX = 0.82f;
    const float wheelZ[2] = {1.42f, -1.38f};
    for (int i = 0; i < 2; ++i) {
        for (int side = -1; side <= 1; side += 2) {
            Mat4 place = mul(translation(float(side) * wheelX, 0.34f, wheelZ[i]), Mat4());
            renderer_->drawAlpha(wheel_, mul(model, place), 0.42f);
        }
    }
    // バックランプの残像もゴーストに付ける (記録時にドリフトしていた区間だけ)。
    std::vector<Ghost::TrailPt> gl, gr;
    if (ghost_.trailPointsAt(race_.lap(), gl, gr)) {
        auto conv = [](const std::vector<Ghost::TrailPt>& src) {
            std::vector<TrailPoint> out;
            out.reserve(src.size());
            for (const auto& p : src) out.push_back({p.pos, p.age});
            return out;
        };
        effects_.drawGhostTrails(*renderer_, conv(gl), conv(gr));
    }
}

// バックミラー。画面上部中央の子画面に、車の後ろ向きカメラの映像を
// もう一度ワールド描画して映す。自車はカメラの死角 (前方) なので映らない。
// 枠と REAR の文字は HUD 座標 (左上原点) で最後に描く。
void Game::drawMirror(Vec3 carPos, float carYaw) {
    if (!renderer_) {
        return;
    }
    const int fullW = renderer_->width();
    const int fullH = renderer_->height();
    const int w = fullW * 44 / 100;         // 子画面の幅 (画面の 44%)
    const int h = fullH * 16 / 100;         // 子画面の高さ (画面の 16%)
    const int x = (fullW - w) / 2;          // 中央寄せ
    const int marginTop = fullH * 2 / 100;  // 上端からの余白
    const int yBottom = fullH - h - marginTop;  // GL は下端原点

    Orbit saved = camera_;  // メインのカメラを退避して後方カメラに差し替える
    Vec3 fwd{std::sin(carYaw), 0, std::cos(carYaw)};
    Orbit rear;
    // 目の位置はルーフより上・少し前。自車のウイングやフィンが映り込むと
    // 視界の真ん中を塞ぐので、それらが下端の外へ出る高さにしてある。
    rear.eye = carPos + Vec3{0, 2.0f, 0} + fwd * 0.5f;
    rear.look = rear.eye - fwd * 10.0f + Vec3{0, -1.0f, 0};  // 真後ろを少し下向きに
    rear.ready = true;
    rear.mode = saved.mode;
    camera_ = rear;
    inMirror_ = true;  // 夜は明るさを上げ、後ろの車やゴーストを見やすくする
    // ミラー側は軽く: 地形 (遠景) は省き、景観も 420m までだけ描く。
    const float savedCull = cullRadius_;
    const bool savedBackdrop = drawBackdrop_;
    cullRadius_ = 420.0f;
    drawBackdrop_ = false;
    renderer_->setViewport(x, yBottom, w, h);
    beginWorldDraw();           // 子画面の範囲だけ消して空と投影を設定
    drawWorld(carPos, carYaw);  // 景観・痕・影 (自車は死角で映らない)
    drawGhost();                // 後ろにいるゴーストも映す
    drawRivals(false);          // 後ろの対戦相手も映す (傾きは本描画で更新済み)
    renderer_->clearViewport();
    inMirror_ = false;
    cullRadius_ = savedCull;
    drawBackdrop_ = savedBackdrop;
    camera_ = saved;

    // 枠 (4 本の帯) とラベル。HUD は左上原点なので y を読み替える。
    const float fx = float(x), fy = float(marginTop), fw = float(w), fh = float(h);
    renderer_->beginHud();
    renderer_->addQuad(fx - 3, fy - 3, fx + fw + 3, fy, 0.04f, 0.05f, 0.07f, 0.9f);
    renderer_->addQuad(fx - 3, fy + fh, fx + fw + 3, fy + fh + 3, 0.04f, 0.05f, 0.07f, 0.9f);
    renderer_->addQuad(fx - 3, fy, fx, fy + fh, 0.04f, 0.05f, 0.07f, 0.9f);
    renderer_->addQuad(fx + fw, fy, fx + fw + 3, fy + fh, 0.04f, 0.05f, 0.07f, 0.9f);
    renderer_->drawText(fx + 8, fy + 6, 2.0f, "REAR", 0.92f, 0.96f, 1.0f, 0.95f);
    renderer_->flushHud();
}

// 煙と光跡の描画 (車の後、HUD の前)。
void Game::drawEffects() {
    effects_.drawSmoke(*renderer_, camera_);
    effects_.drawTrails(*renderer_);
    effects_.drawSparks(*renderer_, camera_);
}
