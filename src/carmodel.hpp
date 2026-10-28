#pragma once
// ============================================================================
// carmodel.hpp ― 車体モデル（形状と車種）の宣言
// 役割: ドライバーズアイ用のノーズ、テールランプ等の追加形状の構築と、
//       車種 (1/2/3) の性能・ドリフト特性の適用。
// 形状の本体 (buildCarBody 等) は car.cpp 側にある。
// ============================================================================

#include "geom.hpp"
#include "meshbuild.hpp"

struct Game;

void buildCarNose(CpuMesh& mesh);
void buildTailLamps(CpuMesh& mesh, Vec3 color);
// 車種を切り替える（性能倍率＋ドリフト調整値の差し替えと保存）。
void applyCarSpec(Game& app, int index);
