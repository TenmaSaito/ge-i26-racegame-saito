#pragma once
// ============================================================================
// ghost.hpp ― ゴースト (前回の自分の最速ラップ) の記録と再生
// 役割: 計測ラップ中の車のポーズを物理ステップ (60Hz) ごとに記録し、
//       ラップ完了時に最速だったものだけを残す。次のラップでは同じ
//       経過時刻のポーズを取り出して半透明で描くことで、自分の走りと
//       競えるようにする。保存はメモリ上のみ (コース変更で消える)。
// ============================================================================

#include "geom.hpp"  // Vec3

#include <vector>

class Car;

class Ghost {
public:
    // 計測ラップの開始 (記録バッファを空にする)。
    void beginLap();
    // 物理ステップごとに現在ポーズを記録する (beginLap の後だけ)。
    void recordPose(const Car& car);
    // ラップ完了。最速 (または初記録) なら記録をゴーストとして保存する。
    void finishLap(float lapTime);
    // 記録中のラップを捨てる (リセット時)。保存済みゴーストは残る。
    void discard();
    // 保存済みゴーストも含めて全部消す (コース変更時)。
    void clear();

    bool hasGhost() const { return !best_.empty(); }  // 再生できるか
    float bestTime() const { return bestTime_; }      // ゴーストのラップタイム
    bool recording() const { return active_; }     // 記録中か

    // 経過 t 秒時点のポーズを補間して返す。範囲外なら false。
    bool poseAt(float t, Vec3& pos, float& yaw) const;

    // 光跡の 1 点 (再生側でリボン化する)。age が負の点は「ここで帯を切る」
    // という目印 (ドリフトが途切れた場所)。
    struct TrailPt {
        Vec3 pos;    // ランプのワールド位置
        float age;   // 再生時刻から見た古さ (秒)。負なら区切り
    };
    // 再生時刻 t の時点で残っているはずのバックランプ残像 (過去 1.15 秒の
    // ドリフト区間) を、左右ランプ別に点列で返す。点があれば true。
    bool trailPointsAt(float t, std::vector<TrailPt>& left, std::vector<TrailPt>& right) const;

private:
    // 記録 1 点分。物理と同じ 60Hz で刻むので時刻は添字から復元できる。
    struct Sample {
        float x, y, z;   // 位置
        float yaw;       // 向き
        bool trail = false;  // この瞬間ドリフト中で光跡が出ていたか
    };
    static constexpr float kStepSec = 1.0f / 60.0f;  // 記録間隔 (物理ステップ)

    std::vector<Sample> recording_;  // 現在のラップの記録
    std::vector<Sample> best_;       // 保存された最速ラップ
    float bestTime_ = -1;            // 最速ラップの秒数 (-1 は未記録)
    bool active_ = false;            // 記録中か
};
