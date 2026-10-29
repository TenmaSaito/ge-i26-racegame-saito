// ============================================================================
// carmodel.cpp ― 車体モデル（追加形状）
// 役割: ノーズやテールランプなど、車体の追加形状の構築だけを担当する。
//       車種切替は Game::applyCarSpec の仕事。
// ============================================================================

#include "carmodel.hpp"


void buildCarNose(CpuMesh& mesh) {
    const Vec3 red{0.72f, 0.08f, 0.09f};
    const Vec3 carbon{0.12f, 0.12f, 0.13f};
    mesh.addBox({-0.7f, 0.32f, 1.7f}, {0.7f, 0.5f, 2.25f}, red);
    mesh.addBox({-0.95f, 0.26f, 1.95f}, {0.95f, 0.36f, 2.35f}, carbon);
}

// テールランプ（車体後端の小箱 2 つ）。色で点灯状態を切り替える。
void buildTailLamps(CpuMesh& mesh, Vec3 color) {
    for (int s = -1; s <= 1; s += 2) {
        float cx = float(s) * 0.55f;
        mesh.addBox({cx - 0.17f, 0.40f, -2.10f}, {cx + 0.17f, 0.53f, -2.04f}, color);
    }
}


