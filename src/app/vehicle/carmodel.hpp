#pragma once
// ============================================================================
// carmodel.hpp ― 車体モデル（形状と車種）の宣言
// 役割: ドライバーズアイ用のノーズ、テールランプ等の追加形状の構築と、
//       車種 (1/2/3) の性能・ドリフト特性の適用。
// 形状の本体 (buildCarBody 等) は car.cpp 側にある。
// ============================================================================

#include "geom.hpp"
#include "meshbuild.hpp"

void buildCarNose(CpuMesh& mesh);
void buildTailLamps(CpuMesh& mesh, Vec3 color);
// 車体の作り込み (キャノピー・ミラー・サイドポッド・ディフューザー等)。
void buildCarDetails(CpuMesh& mesh, int spec, const Vec3* color = nullptr);
// 車種ごとのボディ色 (0=BALANCE 赤 / 1=DRIFT 青 / 2=SPEED 橙)。
Vec3 carSpecColor(int spec);
// 対戦相手の車のボディ色 (index 0..3: 緑・紫・黄・白)。
Vec3 rivalColor(int index);
// ホイールの作り込み (センターハブ)。
void buildWheelDetails(CpuMesh& mesh, int spec, const Vec3* color = nullptr);
// 車種の切り替え (applyCarSpec) は Game のメソッドへ移設した。
// ここは形状の構築だけを担当する。
