// ============================================================================
// carmodel.cpp ― 車体モデル（追加形状）
// 役割: ノーズやテールランプなど、車体の追加形状の構築だけを担当する。
//       車種切替は Game::applyCarSpec の仕事。
// ============================================================================

#include "carmodel.hpp"

#include <cmath>


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



// 車体の作り込み。本体 (buildCarBody) の上に、ガラス・空力部品・
// 細部の箱を足して「車らしさ」を増やす。すべて軸平行の箱で構成する。
void buildCarDetails(CpuMesh& mesh) {
    const Vec3 glass{0.09f, 0.13f, 0.19f};   // キャノピーのガラス色
    const Vec3 carbon{0.12f, 0.12f, 0.13f};  // カーボン部品
    const Vec3 red{0.72f, 0.08f, 0.09f};     // ボディ色 (本体と合わせる)
    const Vec3 metal{0.55f, 0.56f, 0.58f};   // 金属色 (排気管)
    const Vec3 lamp{0.95f, 0.95f, 0.85f};    // ヘッドライト色
    // キャノピー (ガラス) と屋根のパネル。
    mesh.addBox({-0.58f, 1.00f, -0.30f}, {0.58f, 1.24f, 0.80f}, glass);
    mesh.addBox({-0.40f, 1.24f, -0.10f}, {0.40f, 1.30f, 0.60f}, red);
    // ルーフ後方のフィン (アンテナ兼スタビライザ)。
    mesh.addBox({-0.03f, 1.02f, -1.25f}, {0.03f, 1.34f, -0.95f}, red);
    // サイドポッド (車体側面の張り出し)。
    for (int s = -1; s <= 1; s += 2) {
        float x0 = s < 0 ? -1.02f : 0.88f;
        float x1 = s < 0 ? -0.88f : 1.02f;
        mesh.addBox({x0, 0.34f, -0.55f}, {x1, 0.58f, 0.95f}, carbon);
        // ミラー (支柱 + 鏡面)。キャノピー脇に小さく付ける。
        float mx = float(s) * 0.72f;
        mesh.addBox({mx - 0.02f, 1.06f, 0.72f}, {mx + 0.02f, 1.16f, 0.76f}, carbon);
        mesh.addBox({float(s) * 0.80f - 0.05f, 1.12f, 0.68f},
                    {float(s) * 0.80f + 0.05f, 1.24f, 0.80f}, red);
        // フロントウイングの翼端板。
        float wx = float(s) * 0.97f;
        mesh.addBox({wx - 0.03f, 0.28f, 1.98f}, {wx + 0.03f, 0.55f, 2.36f}, carbon);
        // ヘッドライト (ノーズ先端の細い帯)。
        mesh.addBox({float(s) * 0.45f - 0.16f, 0.40f, 2.35f},
                    {float(s) * 0.45f + 0.16f, 0.47f, 2.38f}, lamp);
    }
    // リアディフューザー (後端下部の整流板) と縦フィン。
    mesh.addBox({-0.80f, 0.16f, -2.32f}, {0.80f, 0.34f, -2.02f}, carbon);
    for (int i = -1; i <= 1; ++i) {
        float fx = float(i) * 0.4f;
        mesh.addBox({fx - 0.02f, 0.10f, -2.34f}, {fx + 0.02f, 0.32f, -2.10f}, carbon);
    }
    // 排気管 (2 本出し)。
    for (int s = -1; s <= 1; s += 2) {
        float ex = float(s) * 0.18f;
        mesh.addBox({ex - 0.06f, 0.58f, -2.16f}, {ex + 0.06f, 0.70f, -2.04f}, metal);
    }
}

// ホイールの作り込み。中心にアクセント色のハブを足す (回転対称なので
// ホイールが回っても違和感がない)。
void buildWheelDetails(CpuMesh& mesh) {
    const Vec3 accent{0.80f, 0.16f, 0.16f};
    const int seg = 10;
    const float radius = 0.10f;
    const float halfW = 0.17f;
    for (int i = 0; i < seg; ++i) {
        float a0 = float(i) / float(seg) * 6.2831853f;
        float a1 = float(i + 1) / float(seg) * 6.2831853f;
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);
        Vec3 normal = normalize(Vec3{0, c0 + c1, s0 + s1});
        Vec3 a{-halfW, c0 * radius, s0 * radius};
        Vec3 b{halfW, c0 * radius, s0 * radius};
        Vec3 cc{halfW, c1 * radius, s1 * radius};
        Vec3 d{-halfW, c1 * radius, s1 * radius};
        mesh.addQuad(a, b, cc, d, normal, accent);
    }
}
