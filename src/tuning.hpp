#pragma once
// ============================================================================
// tuning.hpp ― ドリフト調整値の宣言
// 役割: 調整項目の表 (名前・範囲・刻み) と、値の保存/復元の窓口。
//       実データは car.cpp の driftTuning グローバルが持ち、この表は
//       そのフィールドへのポインタでできている。
// 保存先そのものは platform 層が担当する（この層は形式だけを知る）。
// ============================================================================

#include <string>

struct TuneItem {
    const char* name;
    float* value;
    float lo;
    float hi;
    float step;
    int display;  // 0: %.2f / 1: 度 / 2: km/h / 3: %.1f
};

extern TuneItem tuneItems[];
extern const int kTuneItemCount;  // tuning.cpp で表の要素数から定義

void clampTuning();
std::string serializeTuning();
void parseTuning(const char* text);
void saveTuning();
void loadTuning();
// ブラウザ版のみ: 外部ツール（Chrome 拡張など）が保存値を書き換えたら
// 走行へライブ反映する。ネイティブでは何もしない。
void pollExternalTuning();
