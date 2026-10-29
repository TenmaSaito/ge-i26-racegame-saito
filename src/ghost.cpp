// ============================================================================
// ghost.cpp ― Ghost の実装 (ghost.hpp の宣言に対応)
// ============================================================================

#include "ghost.hpp"

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
    recording_.push_back({car.x, car.y, car.z, car.yaw});
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
