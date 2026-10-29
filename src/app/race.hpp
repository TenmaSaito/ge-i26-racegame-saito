#pragma once
// ============================================================================
// race.hpp ― レース進行クラス (Race) の宣言
// 役割: 周回数・ラップタイム・ベストラップの管理だけを持つ小さなクラス。
//       車がコースの s 座標をどう進んだかを受け取り、巻き戻り (スタート
//       ライン通過) を検出して数える。描画や物理は知らない。
// ============================================================================

class Race {
public:
    // 全て初期状態に戻す (レースリセット時)。
    void reset();
    // 1 物理ステップ分進める。s は車のコース上位置、length は全長、dt は秒。
    void tick(float s, float length, float dt);

    int completed() const { return completed_; }  // 完了した周回数
    bool timing() const { return timing_; }       // 計測中か (1 周目は OUT LAP)
    float lap() const { return lap_; }            // 現在の周回タイム (秒)
    float best() const { return best_; }          // ベストラップ (未記録は -1)
    float lastLap() const { return lastLap_; }    // 直近に完了した周のタイム
    float total() const { return total_; }        // 完了周回の合計タイム (リザルト用)

private:
    int completed_ = 0;    // 完了周回数
    bool timing_ = false;  // スタートラインを初めて越えたら計測開始
    float lap_ = 0;        // 現在周の経過秒
    float best_ = -1;      // ベストラップ秒 (-1 は未記録)
    float lastLap_ = -1;   // 直近の完了ラップ秒
    float total_ = 0;      // 完了ラップの合計秒
    float prevS_ = -1;     // 前ステップの s (巻き戻り検出用、-1 は未設定)
};
