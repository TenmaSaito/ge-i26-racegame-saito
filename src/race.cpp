#include "race.hpp"

// tick の本体。スタートライン通過 (s の巻き戻り) を検出して周回を数える。
void Race::reset() {
    completed_ = 0;
    timing_ = false;
    lap_ = 0;
    best_ = -1;
    prevS_ = -1;
}

void Race::tick(float s, float length, float dt) {
    if (prevS_ >= 0) {
        float gap = (length - prevS_) + s;
        // 後半→前半へ大きく巻き戻り、かつ移動量が小さいときだけ周回とみなす
        // (リセットでスタートへ飛んだ場合を誤検出しないため)。
        if (prevS_ > length * 0.5f && s < length * 0.5f && gap < 160.0f) {
            if (timing_) {
                if (best_ < 0 || lap_ < best_) {
                    best_ = lap_;
                }
                completed_ += 1;
            }
            timing_ = true;
            lap_ = 0;
        }
    }
    prevS_ = s;
    lap_ += dt;
}
