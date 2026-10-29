// ============================================================================
// ghost.cpp ― Ghost の実装 (ghost.hpp の宣言に対応)
// ============================================================================

#include "ghost.hpp"

#include <algorithm>
#include <cmath>

#include "car.hpp"  // Car (ポーズの読み取り)

#include <cmath>

void Ghost::beginLap() {
    recording_.clear();
    active_ = true;
}

void Ghost::recordPose(const Car& car) {
    if (!active_) {
        return;
    }
    Sample s;
    s.x = car.x; s.y = car.y; s.z = car.z; s.yaw = car.yaw;
    // プレイヤー側の光跡と同じ条件 (ドリフト中・量 0.3 超) で記録する。
    s.trail = car.drifting && car.driftAmount > 0.3f;
    recording_.push_back(s);
}

void Ghost::finishLap(float lapTime) {
    active_ = false;
    // 初記録か自己ベスト更新のときだけ、記録をゴーストとして残す。
    if (!recording_.empty() && (bestTime_ < 0 || lapTime < bestTime_)) {
        best_ = recording_;
        bestTime_ = lapTime;
    }
    recording_.clear();
}

bool Ghost::trailPointsAt(float t, std::vector<TrailPt>& left, std::vector<TrailPt>& right) const {
    left.clear();
    right.clear();
    if (best_.empty() || t < 0) {
        return false;
    }
    // 残像の寿命はプレイヤー側と同じ 1.15 秒。その窓の中にある
    // 「光跡が出ていた」サンプルから、ランプ位置を復元する。
    const float kWindow = 1.15f;
    size_t i1 = size_t(std::max(0.0f, t - kWindow) / kStepSec);
    size_t i2 = std::min(size_t(t / kStepSec), best_.size() - 1);
    bool prevTrail = false;  // 直前のサンプルも光跡が出ていたか (帯の連続判定)
    for (size_t i = i1; i <= i2; ++i) {
        const Sample& s = best_[i];
        if (!s.trail) {
            prevTrail = false;
            continue;
        }
        // ランプ位置は Effects::lampWorldPos と同じ式 (後端 -2.1m、±0.55m、高さ 0.47m)。
        Vec3 fwd{std::sin(s.yaw), 0, std::cos(s.yaw)};
        Vec3 rightv{fwd.z, 0, -fwd.x};
        Vec3 base{s.x, s.y + 0.47f, s.z};
        float age = t - float(i) * kStepSec;
        std::vector<TrailPt>* outs[2] = {&left, &right};
        const float sides[2] = {-1.0f, 1.0f};
        for (int k = 0; k < 2; ++k) {
            Vec3 p = base + rightv * (sides[k] * 0.55f) + fwd * -2.1f;
            if (!prevTrail && !outs[k]->empty()) {
                outs[k]->push_back({p, -1.0f});  // 帯の区切り
            }
            outs[k]->push_back({p, age});
        }
        prevTrail = true;
    }
    return !left.empty() || !right.empty();
}

void Ghost::discard() {
    recording_.clear();
    active_ = false;
}

void Ghost::clear() {
    discard();
    best_.clear();
    bestTime_ = -1;
}

bool Ghost::poseAt(float t, Vec3& pos, float& yaw) const {
    if (best_.empty() || t < 0) {
        return false;
    }
    // 添字 = 経過ステップ数。前後のサンプルを端数で補間する。
    float idx = t / kStepSec;
    size_t i = size_t(idx);
    if (i >= best_.size()) {
        return false;  // ゴーストはもうゴール済み (これ以上は出さない)
    }
    const Sample& a = best_[i];
    const Sample& b = best_[std::min(i + 1, best_.size() - 1)];
    float f = idx - float(i);
    pos = {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f};
    float dyaw = b.yaw - a.yaw;
    while (dyaw > 3.14159265f) dyaw -= 6.2831853f;
    while (dyaw < -3.14159265f) dyaw += 6.2831853f;
    yaw = a.yaw + dyaw * f;
    return true;
}
