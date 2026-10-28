#pragma once
// ============================================================================
// camera.hpp ― カメラ層の宣言
// 役割: 追跡/ドライバーズアイ等のカメラ位置決めと、路面に車体を這わせる
//       ための路面高サンプリング。
// ============================================================================

#include "geom.hpp"

struct Game;

// カメラの状態（注視点の平滑化とモード）。
struct Orbit {
    Vec3 eye{0, 8, -12};
    Vec3 look{0, 0, 0};
    bool ready = false;
    int mode = 0;  // 0: CHASE / 1: CLOSE / 2: FAR / 3: DRIVER
};

void updateCamera(Game& app, float dt);
// 指定位置の路面高（中心線の高さ＋横位置×バンク）。
float surfaceHeightAt(const Game& app, Vec3 p);
float carPitch(const Game& app);
