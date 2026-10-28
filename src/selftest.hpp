#pragma once
// ============================================================================
// selftest.hpp ― 自己検証の宣言
// 役割: ドリフトの成立と AI の完走を自動で確認する（--self-test）。
//       ゲーム本体からは独立しており、製品の動作には含まれない。
// ============================================================================

struct Track;

// 0 = 合格。コース全長が [minLength, maxLength] の範囲かも併せて見る。
int runSelfTest(const Track& track, float minLength, float maxLength);
