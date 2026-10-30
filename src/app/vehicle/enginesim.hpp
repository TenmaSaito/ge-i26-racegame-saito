#pragma once
// ============================================================================
// enginesim.hpp ― エンジン回転数のシミュレーション (EngineSim)
// 役割: 車速とアクセルから「いま何速で何回転か」を求める。エンジン音の
//       音程と、HUD のギア・タコメーター表示の元になる。
//       車の挙動 (car.cpp) には影響しない、音と表示のためだけのモデル。
//
// 再現していること:
//   - 6 速オートマ: 上限付近でシフトアップ、回転が落ちたらシフトダウン。
//     シフト中は一瞬アクセルが抜け (負荷が下がり)、回転がストンと落ちる。
//   - 空ぶかし (カウントダウン中・停止中): アクセルで回転が素早く上がり、
//     離すとゆっくり下がる。
//   - レブリミッター: レッドゾーンに当たると燃料カットで回転が小刻みに跳ねる。
//   - ホイールスピン: ドリフト中にアクセルを踏むと、車速以上に回転が上がる。
// 依存: soundparams.hpp (出力の型) のみ。
// ============================================================================

#include "soundparams.hpp"

class EngineSim {
public:
    static constexpr float kIdleRpm = 900.0f;   // アイドル回転数
    static constexpr float kRedRpm = 8000.0f;   // レッドゾーン (リミッター)
    static constexpr int kGearCount = 6;        // 前進ギアの数

    // 状態を初期化する (レースのリセット時)。topSpeed は車の最高速 (m/s)。
    void reset(float topSpeed);

    // 1 フレーム進める。
    //   speed    前進方向の速度 (m/s、負は後退)
    //   throttle アクセル 0..1
    //   wheelSlip 駆動輪の空転の度合い 0..1 (ドリフトの深さなど)
    //   neutral  true なら駆動系を切り離す (カウントダウン中の空ぶかし)
    void update(float dt, float speed, float throttle, float wheelSlip, bool neutral);

    float rpm() const { return rpm_; }
    // 0 (アイドル) 〜 1 (レッドゾーン)
    float rpmNorm() const { return (rpm_ - kIdleRpm) / (kRedRpm - kIdleRpm); }
    // 現在のギア (1..6、後退は -1、ニュートラルは 0)
    int gear() const { return gear_; }
    // エンジン音へ渡す状態 (running は呼び出し側で決める)
    EngineSound sound(bool running) const;

private:
    // 指定ギアで車速 speed のときの回転数 (クラッチがつながっている前提)。
    float rpmAt(int gear, float speed) const;

    float topSpeed_ = 82.0f;   // 車の最高速 (ギア比の基準)
    float rpm_ = kIdleRpm;     // 現在の回転数
    int gear_ = 1;             // 現在のギア
    float throttle_ = 0.0f;    // 平滑化したアクセル
    float load_ = 0.0f;        // エンジン負荷 (シフト中は下がる)
    float shiftTimer_ = 0.0f;  // シフト操作の残り秒 (この間はアクセルが抜ける)
    float limiterTimer_ = 0.0f;  // リミッター作動中の残り秒
};
