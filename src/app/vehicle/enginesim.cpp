// ============================================================================
// enginesim.cpp ― エンジン回転数のシミュレーションの実装
// ============================================================================

#include "enginesim.hpp"

#include <algorithm>
#include <cmath>

namespace {

// 各ギアで到達できる速度 (車の最高速に対する割合)。下のギアほど刻みが細かい。
constexpr float kGearTopRatio[EngineSim::kGearCount] = {0.22f, 0.36f, 0.50f, 0.65f, 0.81f, 1.0f};
constexpr float kReverseTopSpeed = 14.0f;  // 後退ギアで回り切る速度 (m/s)

// オートマの変速判断 (レッドゾーンに対する割合)。実車のオートマと同じく、
// 踏んでいるほど高回転まで引っ張り、軽く踏む・離すと早めに上げる。
// 上げ下げの境目は、シフト直後に逆の判断が起きない (行ったり来たりしない) 間隔にしてある。
constexpr float kUpshiftLight = 0.60f;     // アクセルオフ時のシフトアップ回転
constexpr float kUpshiftFull = 0.94f;      // 全開時のシフトアップ回転
constexpr float kDownshiftLight = 0.30f;   // アクセルオフ時のシフトダウン回転
constexpr float kDownshiftFull = 0.55f;    // 全開時のシフトダウン回転 (キックダウン)
constexpr float kDownshiftRatio = 0.80f;   // 1 つ下げてもこの回転以下なら下げてよい
constexpr float kUpshiftSec = 0.16f;       // シフトアップに掛かる時間 (アクセルが抜ける)
constexpr float kDownshiftSec = 0.12f;

// 回転の追従の速さ。
constexpr float kCoupledTau = 0.05f;       // クラッチがつながっている時 (車速に追従)
constexpr float kFreeRevUp = 9000.0f;      // 空ぶかしで上がる速さ (rpm/秒)
constexpr float kFreeRevDown = 3500.0f;    // 空ぶかしで下がる速さ (rpm/秒)

// レブリミッター: 当たると燃料カットで少し回転が落ち、また当たるを繰り返す。
constexpr float kLimiterDrop = 350.0f;
constexpr float kLimiterCutSec = 0.06f;

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

// 時定数 tau で target へ寄せる (フレームレートに依らない指数平滑)。
float approach(float value, float target, float dt, float tau) {
    return value + (target - value) * (1.0f - std::exp(-dt / tau));
}

}  // namespace

void EngineSim::reset(float topSpeed) {
    topSpeed_ = topSpeed > 1.0f ? topSpeed : 82.0f;
    rpm_ = kIdleRpm;
    gear_ = 1;
    throttle_ = 0.0f;
    load_ = 0.0f;
    shiftTimer_ = 0.0f;
    limiterTimer_ = 0.0f;
}

float EngineSim::rpmAt(int gear, float speed) const {
    if (gear < 1) {
        return std::fabs(speed) / kReverseTopSpeed * kRedRpm;
    }
    const float gearTop = topSpeed_ * kGearTopRatio[gear - 1] * 1.04f;
    return std::fabs(speed) / gearTop * kRedRpm;
}

void EngineSim::update(float dt, float speed, float throttle, float wheelSlip, bool neutral) {
    if (dt <= 0.0f) {
        return;
    }
    throttle_ = approach(throttle_, clamp01(throttle), dt, 0.08f);
    if (shiftTimer_ > 0.0f) shiftTimer_ -= dt;
    if (limiterTimer_ > 0.0f) limiterTimer_ -= dt;
    const float range = kRedRpm - kIdleRpm;

    if (neutral) {
        // --- 空ぶかし: 駆動系につながっていないので自由に回る ----------------
        gear_ = 0;
        // 全開ならリミッターに当たる少し上を狙う (当たって跳ねる音を出すため)。
        const float target = kIdleRpm + throttle_ * (kRedRpm * 1.03f - kIdleRpm);
        const float delta = std::min(kFreeRevUp * dt, std::max(-kFreeRevDown * dt, target - rpm_));
        rpm_ += limiterTimer_ > 0.0f ? -kFreeRevDown * dt : delta;
    } else if (speed < -0.5f) {
        // --- 後退 -------------------------------------------------------------
        gear_ = -1;
        const float target = std::max(kIdleRpm, rpmAt(-1, speed));
        rpm_ = approach(rpm_, target, dt, kCoupledTau);
    } else {
        // --- 前進: オートマの変速判断 ---------------------------------------
        if (gear_ < 1) gear_ = 1;
        if (shiftTimer_ <= 0.0f) {
            const float now = rpmAt(gear_, speed);
            const float upLimit = kRedRpm * (kUpshiftLight + (kUpshiftFull - kUpshiftLight) * throttle_);
            const float lowLimit = kRedRpm * (kDownshiftLight + (kDownshiftFull - kDownshiftLight) * throttle_);
            if (gear_ < kGearCount && now > upLimit) {
                ++gear_;
                shiftTimer_ = kUpshiftSec;
            } else if (gear_ > 1 && now < lowLimit && rpmAt(gear_ - 1, speed) < kRedRpm * kDownshiftRatio) {
                --gear_;
                shiftTimer_ = kDownshiftSec;
            }
        }

        // --- 目標回転数 -----------------------------------------------------
        float target = rpmAt(gear_, speed);
        // 発進時はクラッチを滑らせるので、車速が低くても回転は上がる。
        const float launch = 1.0f - clamp01(speed / 8.0f);
        target = std::max(target, kIdleRpm + throttle_ * 0.40f * range * launch);
        // ドリフト中に踏むと後輪が空転し、車速以上に回転が上がる。
        target += clamp01(wheelSlip) * throttle_ * 0.22f * range;
        target = std::max(target, kIdleRpm);
        rpm_ = approach(rpm_, target, dt, kCoupledTau);
    }

    // --- レブリミッター -------------------------------------------------------
    if (rpm_ >= kRedRpm) {
        rpm_ = kRedRpm - kLimiterDrop;
        limiterTimer_ = kLimiterCutSec;
    }
    rpm_ = std::max(kIdleRpm, std::min(kRedRpm, rpm_));

    // --- 負荷: アクセル開度。シフト中とリミッター作動中は燃料が抜ける -------
    float loadTarget = throttle_;
    if (shiftTimer_ > 0.0f) loadTarget = std::min(loadTarget, 0.15f);
    if (limiterTimer_ > 0.0f) loadTarget = std::min(loadTarget, 0.20f);
    load_ = approach(load_, loadTarget, dt, 0.04f);
}

EngineSound EngineSim::sound(bool running) const {
    EngineSound out;
    out.running = running;
    out.rpm = rpm_;
    out.rpmNorm = clamp01(rpmNorm());
    out.throttle = throttle_;
    out.load = load_;
    return out;
}
