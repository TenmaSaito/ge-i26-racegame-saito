#pragma once
// ============================================================================
// effects.hpp ― 演出層の宣言（タイヤ煙とテールランプの光跡）
// 役割: 物理の状態から生まれる一時的な視覚効果だけを担当する。
//       ゲームの勝敗や物理には影響しない。
// ============================================================================

#include "geom.hpp"

#include <vector>

struct Game;

// タイヤ煙。ドリフトで後輪が滑っている間だけ生まれ、広がりながら
// 薄くなって消える（色を空の霞色へ寄せるフェードで表現する）。
struct SmokePuff {
    Vec3 pos;
    Vec3 vel;
    float age = 0;
    float life = 1;
};

// 光跡の 1 点（ランプ位置と経過時間）。
struct TrailPoint {
    Vec3 pos;
    float age;
};

void updateSmoke(Game& app, float dt);
void drawSmoke(Game& app);
void updateTrails(Game& app, float dt);
void drawTrails(Game& app);
