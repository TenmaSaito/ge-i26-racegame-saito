#pragma once

#include "car.hpp"
#include "soundparams.hpp"

#include <algorithm>
#include <cmath>

// タイヤ音はドリフト操作ではなく、車体に対する実際の横滑りから決める。
// 毎描画フレームに一度呼ぶ。短い切り返しは同じ滑りとして継続時間を保つ。
class TireSoundState {
public:
    void reset() { slideTime_ = 0.0f; slideGap_ = 0.0f; }

    // ポーズ中は音量だけを落とし、停止したゲーム内の車速と音色を保つ。
    TireSound paused(const Car& car) const {
        TireSound tire;
        tire.speed = std::sqrt(car.velX * car.velX + car.velZ * car.velZ);
        tire.duration = slideTime_;
        return tire;
    }

    TireSound update(const Car& car, float elapsed, bool counting) {
        const float speed = std::sqrt(car.velX * car.velX + car.velZ * car.velZ);
        const auto unit = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
        const float lateral = std::fabs(car.lateralSpeed);
        // 通常のグリップ旋回でも車体には小さな横速度が残る。速度だけで
        // 判断すると高速コーナーで鳴き続けるため、実際のスリップ角が
        // 約 5〜11 度の間で滑らかに鳴きへ移行する。操作フラグには依存せず、
        // カウンターやグリップ回復中も実際の横滑りに追随する。
        const float slip = unit((std::fabs(car.slipAngle) - 0.085f) / 0.115f);
        const float slipFade = slip * slip * (3.0f - 2.0f * slip);
        float depth = unit((lateral - 0.7f) / 7.0f) * unit((speed - 2.0f) / 5.0f) * slipFade;
        if (!car.onAsphalt) depth *= 0.25f;
        if (counting) depth = 0.0f;

        if (depth > 0.12f) {
            slideTime_ += std::max(0.0f, elapsed);
            slideGap_ = 0.0f;
        } else {
            slideGap_ += std::max(0.0f, elapsed);
            if (slideGap_ > 0.25f) slideTime_ = 0.0f;
        }
        TireSound tire;
        tire.depth = depth;
        tire.duration = slideTime_;
        tire.speed = speed;
        return tire;
    }

private:
    float slideTime_ = 0.0f;
    float slideGap_ = 0.0f;
};
