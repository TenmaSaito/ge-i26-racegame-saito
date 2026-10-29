#pragma once
// ============================================================================
// camera.hpp ― カメラ (Orbit) の宣言
// 役割: 追跡カメラ／ドライバーズアイの位置決めを、カメラ自身の状態クラス
//       として持つ。Game からは「車の補間ポーズ」を渡してもらうだけで、
//       Game の他の内部状態には触れない。
// ============================================================================

#include "geom.hpp"

class Track;  // 路面クエリ用 (前方宣言)

// カメラの状態と位置決め。eye (目の位置) と look (注視点) を毎フレーム決める。
struct Orbit {
    Vec3 eye{0, 8, -12};   // 現在の目の位置 (平滑化済み)
    Vec3 look{0, 0, 0};    // 現在の注視点 (平滑化済み)
    bool ready = false;    // 初期配置が済んだか (最初はブレンドせず即置く)
    int mode = 0;          // 0: CHASE / 1: CLOSE / 2: FAR / 3: DRIVER

    // カメラを 1 フレーム進める。
    // carPos/carYaw/carSlip は描画用に補間された車のポーズ、
    // driftAmount はドリフトの深さ (視線を進行方向へ逃がす量の計算用)、
    // track と groundS はカメラが地面にめり込まないための路面クエリ用。
    void update(Vec3 carPos, float carYaw, float carSlip, float driftAmount,
                const Track& track, float groundS, float dt);
    // モードを次へ (mod は循環の段数。呼び出し側の仕様で 4 / 3 が混在する)。
    void cycle(int mod) { mode = (mode + 1) % mod; }
};

// 指定位置の路面高 (中心線の高さ＋横位置×バンク)。車体を路面平面に
// 這わせる計算などで使う。見つからなければ fallbackY を返す。
float surfaceHeightAt(const Track& track, Vec3 p, float fallbackY, float preferS);
