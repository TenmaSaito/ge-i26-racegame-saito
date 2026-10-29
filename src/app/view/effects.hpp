#pragma once
// ============================================================================
// effects.hpp ― 演出クラス (Effects) の宣言
// 役割: ドリフトに付随する一時的な視覚効果 (タイヤ煙・テールランプの光跡・
//       タイヤ痕) の生成・更新・描画をひとまとめに担当する。
// 方針: 演出の状態はすべてこのクラスの中に閉じる。Game は毎フレーム
//       update() と draw 系を呼ぶだけで、中の粒や帯のことは知らない。
//       物理には一切干渉しない (読むのは Car の状態だけ)。
// ============================================================================

#include "geom.hpp"
#include "render.hpp"  // CpuMesh (タイヤ痕のキャッシュ用)

#include <vector>

class Car;          // 車の状態 (読むだけ)
struct Orbit;       // カメラ状態 (煙のビルボード用)
struct IRenderer;   // 描画ポート

// タイヤ煙の 1 粒。ドリフトで後輪が滑っている間だけ生まれ、広がりながら
// 薄くなって消える。
struct SmokePuff {
    Vec3 pos;          // 現在位置
    Vec3 vel;          // 漂う速度 (上昇＋車の後方へのなびき)
    float age = 0;     // 生まれてからの秒数
    float life = 1;    // 消えるまでの秒数
};

// 光跡の 1 点 (テールランプの位置と経過時間)。
struct TrailPoint {
    Vec3 pos;          // ランプのワールド位置
    float age;         // 記録されてからの秒数 (古い点から消える)
};

// タイヤ痕の 1 区間 (片輪が連続して滑った通過帯)。
// from → to を結ぶ帯として描く。age が寿命に近づくと路面色へ溶けて消える。
// 火花 1 粒。壁との接触点から飛び、重力で落ちて短時間で消える。
struct Spark {
    Vec3 pos;     // 現在位置
    Vec3 vel;     // 速度
    float age;    // 経過秒
    float life;   // 寿命秒 (0.3〜0.7)
};

struct SkidMark {
    Vec3 from;         // 区間の始点 (後輪の接地位置)
    Vec3 to;           // 区間の終点
    float age = 0;     // 残されてからの秒数
    float strength = 1;  // 濃さ (ドリフト量で決まる)
};

class Effects {
public:
    // 全消去 (レースリセット時に呼ぶ)。
    void clear();
    // 1 フレーム分の生成と経過 (車の状態から煙・光跡・タイヤ痕を出す)。
    void update(const Car& car, float dt);
    // 描画 (ワールド描画中、renderer の beginWorld の後に呼ぶ)。
    void drawSmoke(IRenderer& renderer, const Orbit& camera) const;
    void drawTrails(IRenderer& renderer) const;
    // ゴースト用の残像描画 (点列は Ghost::trailPointsAt の結果を変換したもの)。
    void drawGhostTrails(IRenderer& renderer, const std::vector<TrailPoint>& left,
                         const std::vector<TrailPoint>& right) const;
    void drawMarks(IRenderer& renderer) const;  // タイヤ痕 (車より先に描く)
    void drawSparks(IRenderer& renderer, const Orbit& camera) const;  // 火花
    // ベンチ用: 演出を丸ごと止める (負荷の切り分け)。
    void setEnabled(bool on) { enabled_ = on; }
    // テスト・診断用: 現在残っているタイヤ痕の区間数。
    int markCount() const { return static_cast<int>(marks_.size()); }
    int sparkCount() const { return static_cast<int>(sparks_.size()); }  // 検証用

private:
    // テールランプのワールド位置 (side は -1 左 / +1 右)。
    Vec3 lampWorldPos(const Car& car, float side) const;

    // --- タイヤ煙 ---
    std::vector<Spark> sparks_;     // 火花の粒一覧
    float sparkAcc_ = 0;            // 擦り火花の発生端数
    unsigned sparkSeed_ = 777;      // 火花の乱数種
    std::vector<SmokePuff> smoke_;  // 生きている煙の粒
    float smokeAcc_ = 0;            // 発生レートの端数アキュムレータ
    unsigned smokeSeed_ = 12345;    // 煙専用の乱数 (他と干渉させない)
    // --- 光跡 ---
    std::vector<TrailPoint> trailL_;  // 左ランプの履歴
    std::vector<TrailPoint> trailR_;  // 右ランプの履歴
    // --- タイヤ痕 ---
    std::vector<SkidMark> marks_;   // 残っている痕の区間列
    // 痕の描画メッシュのキャッシュ。毎フレーム全区間を作り直すと重いので、
    // 変化があった時と色フェードの更新 (約 0.15 秒ごと) だけ作り直す。
    mutable CpuMesh marksMesh_;       // 作り置きの描画メッシュ
    mutable bool marksDirty_ = true;  // 作り直しが必要か
    mutable int marksFrames_ = 0;     // 前回作り直してからのフレーム数
    Vec3 lastMarkL_{0, 0, 0};       // 左後輪の前回記録位置 (区間をつなぐ)
    Vec3 lastMarkR_{0, 0, 0};       // 右後輪の前回記録位置
    bool markChain_ = false;        // 前フレームも痕を出していたか (連続判定)
    bool enabled_ = true;           // 演出全体の有効/無効 (ベンチ用)
};
