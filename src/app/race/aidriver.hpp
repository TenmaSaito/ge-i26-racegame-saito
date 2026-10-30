#pragma once
// ============================================================================
// aidriver.hpp ― AI ドライバー (対戦相手の運転)
// 役割: プレイヤーと同じ物理 (Car::update) に渡す DriveInput を作る。
//       AI だけ特別な速さで動くことはなく、操作だけで勝負する。
//
// 運転のしかた:
//   1. 速度計画: コースの曲がり具合 (曲率) から「その地点で出せる速度」を
//      求め、手前から逆算してブレーキが間に合う速度に整える (速度プロファイル)。
//      走行記録から学習したプロファイル (learned) があれば混ぜる。
//   2. ステア: 速度に応じた先の目標点を狙う「純追跡 (pure pursuit)」。
//   3. 前に車がいれば、空いている側へ走行ラインをずらして抜きにかかる。
//   4. 難易度: 速度の上限・ブレーキの深さ・ステアのぶれで強さを変える。
// 依存: car / track (ドメイン) のみ。描画も入力デバイスも知らない。
// ============================================================================

#include "car.hpp"
#include "track.hpp"

#include <cstdint>
#include <vector>

// AI の強さ (選択画面で選ぶ)。
enum class AiLevel {
    Easy = 0,
    Normal = 1,
    Hard = 2,
};
inline constexpr int kAiLevelCount = 3;
const char* aiLevelName(AiLevel level);  // 表示名 ("EASY" など)

// 強さを決める数値。aiSkillFor() が難易度ごとの値を返す。
struct AiSkill {
    float pace = 0.9f;        // 速度プロファイルに掛ける倍率 (1 で限界いっぱい)
    float latAccel = 26.0f;   // コーナーで許す横加速度 (m/s^2)。大きいほど速く曲がる
    float brakeDecel = 20.0f; // 速度計画で見込む減速度 (m/s^2)。大きいほど奥で止まる
    float steerNoise = 0.0f;  // ステアのぶれ (0..1)。人間らしいミスの量
    float learnWeight = 0.0f; // 学習プロファイルをどれだけ信じるか (0..1)
};
AiSkill aiSkillFor(AiLevel level, float paceAdjust);

// 他の車の位置 (追い越し判断用)。
struct AiTraffic {
    float x = 0, z = 0;  // ワールド位置
    float s = 0;         // コース上の位置 (m)
};

class AiDriver {
public:
    // コースと強さから速度プロファイルを作る。learned は走行記録から学習した
    // 区間速度 (コース全長を等分した各区間の速度 m/s、空なら使わない)。
    void prepare(const Track& track, const AiSkill& skill, const std::vector<float>& learned, uint32_t seed);

    // 1 物理ステップ分の操作を決める。others は自分以外の車。
    DriveInput drive(const Car& car, const Track& track, float dt, const std::vector<AiTraffic>& others);

    // 地点 s (m) で狙う速度 (m/s)。テストとデバッグ表示用。
    float targetSpeedAt(float s) const;

private:
    // s に最も近いサンプルの添字 (サンプルは s の昇順に並んでいる)。
    int sampleIndexAt(const Track& track, float s) const;
    // 中心線上の地点 s から横に lateral (右が正) ずらした点。
    void pointAt(const Track& track, float s, float lateral, float& x, float& z) const;

    AiSkill skill_;
    std::vector<float> targetSpeed_;  // サンプルごとの狙う速度 (m/s)
    float length_ = 1.0f;             // コース全長 (m)
    float lineOffset_ = 0.0f;         // 走行ラインの横ずれ (追い越し用、平滑化済み)
    float noise_ = 0.0f;              // ステアのぶれ (ゆっくり変わる)
    float noiseTarget_ = 0.0f;
    float noiseTimer_ = 0.0f;
    float stuckTime_ = 0.0f;          // 止まって動けない時間 (壁に刺さった等)
    float recoverTime_ = 0.0f;        // 後退して抜け出す残り時間
    uint32_t rng_ = 1;                // 乱数の状態 (車ごとに違う種)
};

// コース上の地点 s・横位置 lateral に車を置く (スタートグリッド用)。
void placeCarOnTrack(Car& car, const Track& track, float s, float lateral);
// スタート地点 (spawn) のコース上の位置 s (m)。
float spawnDistance(const Track& track);
