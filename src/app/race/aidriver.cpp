// ============================================================================
// aidriver.cpp ― AI ドライバーの実装
// 強さの数値は aiSkillFor() に、運転の癖 (先読み距離・アクセルの踏み方) は
// drive() の先頭の定数にまとめてある。
// ============================================================================

#include "aidriver.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kWheelbase = 3.05f;  // car.cpp と同じホイールベース (ステア量の換算用)
constexpr float kMinCornerSpeed = 9.0f;  // どんなにきつい所でもこれ以下には落とさない

float wrapAngle(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// コース上の距離の差 (b - a) を -L/2..L/2 に畳む (周回の継ぎ目をまたいでも正しく)。
float wrapDistance(float d, float length) {
    while (d > length * 0.5f) d -= length;
    while (d < -length * 0.5f) d += length;
    return d;
}

float wrapS(float s, float length) {
    s = std::fmod(s, length);
    return s < 0 ? s + length : s;
}

// 0..1 の疑似乱数 (xorshift32)。
float nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(state & 0xFFFFFF) / float(0xFFFFFF);
}

}  // namespace

const char* aiLevelName(AiLevel level) {
    switch (level) {
    case AiLevel::Easy: return "EASY";
    case AiLevel::Hard: return "HARD";
    default: return "NORMAL";
    }
}

AiSkill aiSkillFor(AiLevel level, float paceAdjust) {
    AiSkill skill;
    switch (level) {
    // 自己テスト (--self-test) の AI 単独走行で、鈴鹿のラップがおよそ
    // EASY 132 秒 / NORMAL 111 秒 / HARD 100 秒になる値。どの強さでも壁には当たらない。
    case AiLevel::Easy:
        skill.pace = 0.82f;
        skill.latAccel = 22.0f;
        skill.brakeDecel = 16.0f;
        skill.steerNoise = 0.6f;
        skill.learnWeight = 0.3f;
        break;
    case AiLevel::Normal:
        skill.pace = 0.93f;
        skill.latAccel = 32.0f;
        skill.brakeDecel = 20.0f;
        skill.steerNoise = 0.3f;
        skill.learnWeight = 0.6f;
        break;
    case AiLevel::Hard:
        skill.pace = 1.0f;
        skill.latAccel = 46.0f;
        skill.brakeDecel = 24.0f;
        skill.steerNoise = 0.08f;
        skill.learnWeight = 0.9f;
        break;
    }
    // 勝敗から学習したペース補正 (RecordBook が持つ、±数 %)。
    skill.pace *= 1.0f + paceAdjust;
    return skill;
}

// ============================================================================
// 速度計画
// ============================================================================
void AiDriver::prepare(const Track& track, const AiSkill& skill, const std::vector<float>& learned,
                       uint32_t seed) {
    skill_ = skill;
    rng_ = seed ? seed : 1u;
    length_ = std::max(1.0f, track.length);
    lineOffset_ = 0.0f;
    noise_ = noiseTarget_ = noiseTimer_ = 0.0f;
    stuckTime_ = recoverTime_ = 0.0f;

    const int n = static_cast<int>(track.samples.size());
    targetSpeed_.assign(size_t(std::max(n, 1)), kCarMaxSpeed);
    if (n < 4) {
        return;
    }
    auto sampleAt = [&](int i) -> const TrackSample& { return track.samples[size_t((i % n + n) % n)]; };
    auto headingAt = [&](int i) {
        const TrackSample& a = sampleAt(i);
        const TrackSample& b = sampleAt(i + 1);
        return std::atan2(b.x - a.x, b.z - a.z);
    };
    auto gapAt = [&](int i) {  // サンプル i と i+1 の間の距離
        const TrackSample& a = sampleAt(i);
        const TrackSample& b = sampleAt(i + 1);
        return std::max(0.1f, std::hypot(b.x - a.x, b.z - a.z));
    };

    // --- 1. 曲率 (1m 進むあいだの向きの変化) を前後 ±4 サンプルで平均する ---
    std::vector<float> curvature(size_t(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        float turn = 0.0f, dist = 0.0f;
        for (int k = -4; k < 4; ++k) {
            turn += wrapAngle(headingAt(i + k + 1) - headingAt(i + k));
            dist += gapAt(i + k);
        }
        curvature[size_t(i)] = std::fabs(turn) / dist;
    }

    // --- 2. 曲がれる速度: 横加速度の限界とステアの切れ角の限界の小さい方 ---
    for (int i = 0; i < n; ++i) {
        const float k = std::max(curvature[size_t(i)], 1e-4f);
        const float vGrip = std::sqrt(skill.latAccel / k);
        // tan(maxSteerAt(v)) / L >= k を満たす最大の v (tan(x) ≒ x で近似)。
        const float vSteer = (0.46f / (kWheelbase * k) - 1.0f) / 0.085f;
        float v = std::min({vGrip, std::max(vSteer, kMinCornerSpeed), kCarMaxSpeed});

        // 学習プロファイル: 記録で実際に出ていた速度へ寄せる。ただし物理で
        // 求めた限界の 1.15 倍は超えない (記録の誤差やドリフトでの速さを鵜呑みにしない)。
        if (!learned.empty() && skill.learnWeight > 0.0f) {
            const float pos = sampleAt(i).s / length_ * float(learned.size());
            const int b0 = int(pos) % int(learned.size());
            const int b1 = (b0 + 1) % int(learned.size());
            const float t = pos - std::floor(pos);
            const float lv = learned[size_t(b0)] * (1.0f - t) + learned[size_t(b1)] * t;
            if (lv > 1.0f) {
                v = v + (std::min(lv, v * 1.15f) - v) * skill.learnWeight;
            }
        }
        targetSpeed_[size_t(i)] = std::max(kMinCornerSpeed, v * skill.pace);
    }

    // --- 3. ブレーキが間に合うよう、後ろ向きに速度を絞る (周回なので 2 周分) ---
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = n - 1; i >= 0; --i) {
            const float next = targetSpeed_[size_t((i + 1) % n)];
            const float reach = std::sqrt(next * next + 2.0f * skill.brakeDecel * gapAt(i));
            targetSpeed_[size_t(i)] = std::min(targetSpeed_[size_t(i)], reach);
        }
    }
}

float AiDriver::targetSpeedAt(float s) const {
    if (targetSpeed_.empty()) {
        return kCarMaxSpeed;
    }
    // サンプルは等間隔に近いので、比率で添字を引けば十分。
    const int n = static_cast<int>(targetSpeed_.size());
    const int i = std::min(n - 1, std::max(0, int(wrapS(s, length_) / length_ * float(n))));
    return targetSpeed_[size_t(i)];
}

int AiDriver::sampleIndexAt(const Track& track, float s) const {
    const auto& samples = track.samples;
    s = wrapS(s, length_);
    auto it = std::lower_bound(samples.begin(), samples.end(), s,
                               [](const TrackSample& a, float value) { return a.s < value; });
    int i = static_cast<int>(it - samples.begin());
    return i >= static_cast<int>(samples.size()) ? 0 : i;
}

void AiDriver::pointAt(const Track& track, float s, float lateral, float& x, float& z) const {
    const int n = static_cast<int>(track.samples.size());
    const int i = sampleIndexAt(track, s);
    const TrackSample& a = track.samples[size_t((i - 1 + n) % n)];
    const TrackSample& b = track.samples[size_t(i)];
    const float heading = std::atan2(b.x - a.x, b.z - a.z);
    x = b.x + std::cos(heading) * lateral;  // 右方向 = (cos, -sin)
    z = b.z - std::sin(heading) * lateral;
}

// ============================================================================
// 運転
// ============================================================================
DriveInput AiDriver::drive(const Car& car, const Track& track, float dt, const std::vector<AiTraffic>& others) {
    // 運転の癖。
    constexpr float kLookBase = 7.0f;       // 先読み距離 = kLookBase + 速度 * kLookPerSpeed
    constexpr float kLookPerSpeed = 0.5f;
    constexpr float kPassGap = 3.4f;        // 追い越すときに空ける横の間隔 (m)
    constexpr float kPassRange = 32.0f;     // この距離より前の車は気にしない (m)

    DriveInput in;
    if (track.samples.size() < 4) {
        return in;
    }
    const float s = car.ground.found ? car.ground.s : track.query(car.x, car.z, car.y).s;
    const float speed = car.speed;

    // --- 引っかかり対策: 踏んでいるのに進まなければ、少し後退してやり直す ---
    if (recoverTime_ > 0.0f) {
        recoverTime_ -= dt;
        in.brake = 1.0f;  // 止まった状態でブレーキを踏み続けると後退する
        in.steer = car.ground.lateral > 0 ? 0.6f : -0.6f;
        return in;
    }
    stuckTime_ = std::fabs(speed) < 1.5f ? stuckTime_ + dt : 0.0f;
    if (stuckTime_ > 1.6f) {
        stuckTime_ = 0.0f;
        recoverTime_ = 1.1f;
    }

    // --- 追い越し: 前の車と横位置が近ければ、空いている側へラインをずらす ---
    const float halfRoom = std::max(0.5f, track.halfWidth - 1.3f);
    float desiredOffset = 0.0f;
    float nearestAhead = kPassRange;
    for (const AiTraffic& other : others) {
        const float ahead = wrapDistance(other.s - s, length_);
        if (ahead <= 0.0f || ahead >= nearestAhead) {
            continue;
        }
        // 相手の横位置: 自分の地点の右方向ベクトルへの投影で近似する。
        float cx, cz;
        pointAt(track, other.s, 0.0f, cx, cz);
        const int i = sampleIndexAt(track, other.s);
        const TrackSample& a = track.samples[size_t((i - 1 + int(track.samples.size())) % int(track.samples.size()))];
        const TrackSample& b = track.samples[size_t(i)];
        const float heading = std::atan2(b.x - a.x, b.z - a.z);
        const float otherLat = (other.x - cx) * std::cos(heading) - (other.z - cz) * std::sin(heading);
        if (std::fabs(otherLat - lineOffset_) < kPassGap) {
            nearestAhead = ahead;
            desiredOffset = otherLat > 0.0f ? otherLat - kPassGap : otherLat + kPassGap;
        }
    }
    desiredOffset = std::max(-halfRoom, std::min(halfRoom, desiredOffset));
    const float shift = 2.0f * dt;  // ラインは 2 m/s でゆっくり移す (急な割り込みをしない)
    lineOffset_ += std::max(-shift, std::min(shift, desiredOffset - lineOffset_));

    // --- ステア: 純追跡。先の目標点へ向かう円弧の切れ角を求める ------------
    const float look = std::max(9.0f, std::min(40.0f, kLookBase + std::fabs(speed) * kLookPerSpeed));
    float tx, tz;
    pointAt(track, s + look, lineOffset_, tx, tz);
    const float dx = tx - car.x;
    const float dz = tz - car.z;
    const float localX = dx * std::cos(car.yaw) - dz * std::sin(car.yaw);  // 右が正
    const float localZ = dx * std::sin(car.yaw) + dz * std::cos(car.yaw);  // 前が正
    const float alpha = std::atan2(localX, localZ);
    const float dist = std::max(1.0f, std::hypot(dx, dz));
    const float wheel = std::atan(2.0f * kWheelbase * std::sin(alpha) / dist);
    const float steerMax = std::max(0.02f, maxSteerAt(speed) * car.steerMul);
    in.steer = wheel / steerMax;

    // 人間らしいぶれ: 0.3〜0.8 秒ごとに新しい目標を選び、ゆっくり追う。
    noiseTimer_ -= dt;
    if (noiseTimer_ <= 0.0f) {
        noiseTimer_ = 0.3f + 0.5f * nextRandom(rng_);
        noiseTarget_ = (nextRandom(rng_) * 2.0f - 1.0f) * skill_.steerNoise;
    }
    noise_ += (noiseTarget_ - noise_) * std::min(1.0f, dt * 4.0f);
    in.steer = std::max(-1.0f, std::min(1.0f, in.steer + noise_ * 0.25f));

    // --- アクセルとブレーキ: 少し先の狙い速度との差で決める ---------------
    const float target = std::min(targetSpeedAt(s), targetSpeedAt(s + std::max(0.0f, speed) * 0.35f));
    const float err = target - speed;
    if (std::fabs(alpha) > 1.2f) {
        in.throttle = 0.35f;  // 大きく向きがずれている (スピン後など): 控えめに立て直す
    } else if (err > 0.0f) {
        in.throttle = std::min(1.0f, 0.45f + err * 0.25f);
    } else if (err > -2.5f) {
        in.throttle = std::max(0.0f, 0.3f + err * 0.12f);  // 少し速い: 緩める
    } else {
        in.brake = std::min(1.0f, std::max(0.25f, -err * 0.1f));
    }
    return in;
}

// ============================================================================
// スタートグリッド
// ============================================================================
float spawnDistance(const Track& track) {
    return track.query(track.spawnX, track.spawnZ, track.spawnY).s;
}

void placeCarOnTrack(Car& car, const Track& track, float s, float lateral) {
    car.reset(track);
    const int n = static_cast<int>(track.samples.size());
    if (n < 2 || track.length <= 0.0f) {
        return;
    }
    s = wrapS(s, track.length);
    auto it = std::lower_bound(track.samples.begin(), track.samples.end(), s,
                               [](const TrackSample& a, float value) { return a.s < value; });
    const int i1 = std::min(n - 1, std::max(1, static_cast<int>(it - track.samples.begin())));
    const TrackSample& a = track.samples[size_t(i1 - 1)];
    const TrackSample& b = track.samples[size_t(i1)];
    const float span = std::max(0.01f, b.s - a.s);
    const float t = std::max(0.0f, std::min(1.0f, (s - a.s) / span));
    const float heading = std::atan2(b.x - a.x, b.z - a.z);
    car.x = a.x + (b.x - a.x) * t + std::cos(heading) * lateral;
    car.z = a.z + (b.z - a.z) * t - std::sin(heading) * lateral;
    car.y = a.y + (b.y - a.y) * t + a.cross * lateral;
    car.yaw = heading;
    car.ground = track.query(car.x, car.z, car.y, s);
    if (car.ground.found) {
        car.y = car.ground.centerY + car.ground.cross * car.ground.lateral;
    }
}
