#pragma once
// ============================================================================
// rival.hpp ― 対戦相手 (Rival) と順位表 (Standing)
// 役割: AI が運転する 1 台分の状態をまとめる。物理はプレイヤーと同じ Car、
//       運転は AiDriver、周回の数え方はプレイヤーと同じ Race を使う。
// ============================================================================

#include "aidriver.hpp"
#include "car.hpp"
#include "race.hpp"

#include <string>

// 対戦相手 1 台。
struct Rival {
    std::string name;         // 表示名 (オンライン記録の持ち主の名前、または既定の名前)
    bool fromRecord = false;  // 誰かの走行記録をモデルにしているか
    int color = 0;            // ボディ色の番号 (rivalColor)
    Car car;                  // 物理 (プレイヤーと同じもの)
    AiDriver driver;          // 運転
    Race race;                // 周回・計時

    // 描画用の補間ポーズ (プレイヤーと同じく、1 ステップ前と現在をつなぐ)。
    float prevX = 0, prevY = 0, prevZ = 0, prevYaw = 0;
    float viewX = 0, viewY = 0, viewZ = 0, viewYaw = 0;
    float pitchSm = 0, rollSm = 0;  // 車体の傾き (平滑化済み)
};

// 順位表の 1 行。
struct Standing {
    std::string name;
    bool player = false;   // プレイヤー本人の行か
    int color = -1;        // 対戦相手の色番号 (プレイヤーは -1)
    float progress = 0;    // 進んだ距離 (周回 × 全長 + 周内の位置)。大きいほど前
};

// 周回数と周内の位置から「進んだ距離」を求める (順位の比較用)。
// 最初にスタートラインを越えるまで (timing 前) は、ラインの手前の負の距離になる。
inline float raceProgress(const Race& race, float s, float trackLength) {
    return float(race.completed()) * trackLength + (race.timing() ? s : s - trackLength);
}
