// ============================================================================
// gamerivals.cpp ― Game の対戦相手・走行記録まわり
// 役割: 対戦相手をグリッドに並べる / 順位表 / 対戦相手の描画 /
//       走行記録の保存とオンライン共有の取り込み。
//       (Game の本体 game.cpp が長くなりすぎないよう、ここに分けてある)
// ============================================================================

#include "game.hpp"

#include "carmodel.hpp"  // rivalColor / carModelMatrix

#include <algorithm>
#include <cmath>

namespace {

constexpr float kGridRow = 8.0f;    // スタートグリッドの前後の間隔 (m)
constexpr float kGridSide = 2.4f;   // グリッドの左右のずれ (m)

// オンラインの記録が無いときに使う対戦相手の名前。
constexpr const char* kDefaultRivalNames[] = {"AKAGI", "HARUNA", "USUI", "IROHA", "MYOGI", "TSUKUBA"};
constexpr int kDefaultRivalNameCount = 6;

}  // namespace

// ============================================================================
// 対戦相手の準備
// ============================================================================
void Game::setupRivals() {
    rivals_.clear();
    raceSeed_ = raceSeed_ * 1664525u + 1013904223u;  // レースごとに名前とぶれを変える
    if (autopilot_ && track_.samples.size() >= 4) {
        autopilotDriver_.prepare(track_, aiSkillFor(AiLevel::Hard, 0.0f), recordBook_.learnedProfile(courseStem_), 99u);
    }
    if (rivalCount_ <= 0 || track_.samples.size() < 4) {
        return;
    }
    const std::string self = playerName();
    // スタートグリッド: 先頭はスタート地点から対戦相手の台数ぶん前、ただし
    // スタートラインの 6m 手前まで (ラインの向こうに置くと周回の数え方がずれる)。
    // そこから後ろへ 8m 間隔で対戦相手を並べ、プレイヤーは最後尾から追い上げる。
    const float spawnS = spawnDistance(track_);
    const float gridFront = std::min(spawnS + kGridRow * float(rivalCount_), track_.length - 6.0f);

    // 対戦相手のモデル: オンラインで共有された他の人の記録 (自分に近い速さの人)。
    // 足りない分は既定の名前の AI が、全員の記録から学習した速度で走る。
    const std::vector<RaceRecord> models = recordBook_.pickRivalModels(courseStem_, self, rivalCount_);
    const std::vector<float> learned = recordBook_.learnedProfile(courseStem_);
    const AiSkill baseSkill = aiSkillFor(aiLevel_, recordBook_.paceAdjust(int(aiLevel_)));
    const CarSpec& spec = kCarSpecs[carSpec_];

    for (int i = 0; i < rivalCount_; ++i) {
        Rival rival;
        rival.color = i;
        AiSkill skill = baseSkill;
        const std::vector<float>* profile = &learned;
        if (i < static_cast<int>(models.size())) {
            rival.name = models[size_t(i)].name;
            rival.fromRecord = true;
            profile = &models[size_t(i)].profile;
            skill.learnWeight = std::max(skill.learnWeight, 0.8f);  // その人の走りをよく真似る
        } else {
            // 既定の名前は、プレイヤー名やほかの対戦相手と重ならないものを選ぶ。
            for (int k = 0; k < kDefaultRivalNameCount; ++k) {
                const std::string name = kDefaultRivalNames[(raceSeed_ + uint32_t(i + k)) % kDefaultRivalNameCount];
                bool used = name == self;
                for (const Rival& r : rivals_) used = used || r.name == name;
                if (!used) {
                    rival.name = name;
                    break;
                }
            }
        }
        // グリッド: 左右交互に並べる。
        const float s = gridFront - kGridRow * float(i);
        placeCarOnTrack(rival.car, track_, s, (i % 2 == 0) ? -kGridSide : kGridSide);
        rival.car.powerMul = spec.powerMul;  // 車の性能はプレイヤーと同じ (腕だけの勝負)
        rival.car.topSpeedMul = spec.topSpeedMul;
        rival.car.steerMul = spec.steerMul;
        rival.driver.prepare(track_, skill, *profile, raceSeed_ + uint32_t(i) * 7919u);
        rival.race.reset();
        rival.race.startCountdown();
        rival.prevX = rival.viewX = rival.car.x;
        rival.prevY = rival.viewY = rival.car.y;
        rival.prevZ = rival.viewZ = rival.car.z;
        rival.prevYaw = rival.viewYaw = rival.car.yaw;
        rivals_.push_back(std::move(rival));
    }
    // プレイヤーは最後尾の中央へ (Car::reset は車種の性能値に触れないのでそのまま残る)。
    const float playerS = gridFront - kGridRow * float(rivalCount_);
    if (std::fabs(playerS - spawnS) > 0.5f) {
        placeCarOnTrack(car_, track_, playerS, 0.0f);
    }
}

// プレイヤーの車の自動運転 (動作確認・デモ用)。速度計画はコースごとに作り直す。
DriveInput Game::autopilotInput(float dt) {
    std::vector<AiTraffic> traffic;
    for (const Rival& r : rivals_) traffic.push_back({r.car.x, r.car.z, r.car.ground.s});
    return autopilotDriver_.drive(car_, track_, dt, traffic);
}

// ============================================================================
// 照明 (夜)
// ============================================================================
void Game::applyLighting() {
    if (!renderer_) {
        return;
    }
    SceneLighting lighting;
    if (night_) {
        lighting.night = 1.0f;
        // バックミラーは暗いと何も見えないので、明るさを上げて後ろの車を見やすくする。
        lighting.exposure = inMirror_ ? 2.4f : 1.0f;
        // ゴースト (過去の自分) は夜は青白く光らせる。ミラーではさらに強く。
        lighting.ghostGlow = inMirror_ ? Vec3{0.20f, 0.55f, 0.70f} : Vec3{0.10f, 0.32f, 0.42f};
        // ヘッドライト: プレイヤーと対戦相手の各車の先端から、前方やや下向きに照らす。
        auto addHeadlight = [&](Vec3 pos, float yaw) {
            if (lighting.lightCount >= SceneLighting::kMaxLights) return;
            const Vec3 fwd{std::sin(yaw), 0.0f, std::cos(yaw)};
            SpotLight& light = lighting.lights[lighting.lightCount++];
            light.pos = pos + fwd * 2.4f + Vec3{0.0f, 0.65f, 0.0f};
            light.dir = normalize(fwd + Vec3{0.0f, -0.07f, 0.0f});
            light.color = Vec3{2.0f, 1.9f, 1.65f};
        };
        addHeadlight(viewPoseValid_ ? viewPos_ : Vec3{car_.x, car_.y, car_.z},
                     viewPoseValid_ ? viewYaw_ : car_.yaw);
        for (const Rival& rival : rivals_) {
            addHeadlight({rival.viewX, rival.viewY, rival.viewZ}, rival.viewYaw);
        }
    }
    renderer_->setLighting(lighting);
}

std::string Game::rankingKey(const std::string& stem, int laps) const {
    return laps == 3 ? stem : stem + "-" + std::to_string(laps);
}

// ============================================================================
// 順位
// ============================================================================
std::vector<Standing> Game::standings() const {
    std::vector<Standing> out;
    Standing me;
    me.name = playerName();
    me.player = true;
    me.progress = raceProgress(race_, car_.ground.s, track_.length);
    out.push_back(me);
    for (const Rival& rival : rivals_) {
        Standing st;
        st.name = rival.name;
        st.color = rival.color;
        st.progress = raceProgress(rival.race, rival.car.ground.s, track_.length);
        out.push_back(st);
    }
    // 前にいる順。同じ距離ならプレイヤーを前にする (表示が揺れないように)。
    std::stable_sort(out.begin(), out.end(), [](const Standing& a, const Standing& b) {
        return a.progress > b.progress;
    });
    return out;
}

int Game::playerPosition() const {
    const std::vector<Standing> list = standings();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].player) return int(i) + 1;
    }
    return 1;
}

// ============================================================================
// 描画
// ============================================================================
void Game::drawRivals(bool updateTilt) {
    if (!renderer_) {
        return;
    }
    const float wheelX = 0.82f;
    const float wheelZ[2] = {1.42f, -1.38f};
    for (Rival& rival : rivals_) {
        const Vec3 pos{rival.viewX, rival.viewY, rival.viewZ};
        // 車体の傾き: プレイヤーと同じく前後・左右の路面高から求めて平滑化する。
        if (updateTilt) {
            const Vec3 fw{std::sin(rival.viewYaw), 0, std::cos(rival.viewYaw)};
            const Vec3 rt{fw.z, 0, -fw.x};
            const float preferS = rival.car.ground.found ? rival.car.ground.s : -1.0f;
            const float hF = surfaceHeightAt(track_, pos + fw * 3.6f, pos.y, preferS);
            const float hB = surfaceHeightAt(track_, pos - fw * 3.6f, pos.y, preferS);
            const float hR = surfaceHeightAt(track_, pos + rt * 1.3f, pos.y, preferS);
            const float hL = surfaceHeightAt(track_, pos - rt * 1.3f, pos.y, preferS);
            const float k = 1.0f - std::exp(-6.0f * lastDt_);
            rival.pitchSm += (std::atan2(hF - hB, 7.2f) - rival.pitchSm) * k;
            rival.rollSm += (std::atan2(hR - hL, 2.6f) - rival.rollSm) * k;
        }
        const Mat4 shadow = mul(translation(pos.x, pos.y + 0.08f, pos.z), rotationY(rival.viewYaw));
        renderer_->draw(shadow_, shadow);

        Car viewCar = rival.car;
        viewCar.x = pos.x;
        viewCar.y = pos.y;
        viewCar.z = pos.z;
        viewCar.yaw = rival.viewYaw;
        const Mat4 model = mul(translation(0, 0.05f, 0), carModelMatrix(viewCar, rival.pitchSm, rival.rollSm));
        const int c = std::max(0, std::min(kMaxRivals - 1, rival.color));
        renderer_->draw(rivalBody_[c], model);
        renderer_->draw((rival.car.drifting || night_) ? lampsOn_ : lampsOff_, model);
        if (night_) {
            renderer_->draw(headLamps_, model);  // 夜はヘッドランプが光る (ミラーでも目立つ)
        }
        const float steerForWheel[2] = {rival.car.steerAngle, 0};
        for (int i = 0; i < 2; ++i) {
            for (int side = -1; side <= 1; side += 2) {
                const Mat4 place = mul(translation(float(side) * wheelX, 0.34f, wheelZ[i]),
                                       mul(rotationY(steerForWheel[i]), rotationX(rival.car.wheelSpin)));
                renderer_->draw(rivalWheel_[c], mul(model, place));
            }
        }
    }
}

// ============================================================================
// 走行記録
// ============================================================================
std::string Game::playerName() const {
    return sanitizeName(playerNameSource_ ? playerNameSource_() : std::string(), "PLAYER");
}

void Game::saveRecords() {
    if (recordsSink_) {
        recordsSink_(recordBook_.serialize());
    }
}

void Game::requestSharedRecords() {
    if (recordService_) {
        recordService_->requestCourse(courseStem_);
    }
}

void Game::pollSharedRecords() {
    if (!recordService_) {
        return;
    }
    std::vector<std::string> lines;
    if (!recordService_->poll(lines)) {
        return;
    }
    int added = 0;
    const std::string self = playerName();
    for (const std::string& line : lines) {
        RaceRecord record;
        if (RaceRecord::parse(line, record) && record.name != self) {
            added += recordBook_.addShared(record);
        }
    }
    if (added > 0) {
        saveRecords();  // 次回はオフラインでも学習に使えるよう手元にも残す
    }
}
