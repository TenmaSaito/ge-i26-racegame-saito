#pragma once
// ============================================================================
// tuning.hpp ― ドリフト調整値の宣言
// 役割: 調整項目の表 (名前・範囲・刻み) と、値の保存/復元の窓口。
//       実データは car.cpp の driftTuning グローバルが持ち、この表は
//       そのフィールドへのポインタでできている。
// この層は「形式」だけを知る純粋なドメイン層: 保存先 (ファイルや
// localStorage) には一切触れず、文字列化と復元だけを行う。読み書きの
// 実務は外側 (platform / main / game の差し込み口) が担う。
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

// 保存に使うキー名 (旧来の localStorage キーと同名、Chrome 拡張と互換)。
// 「どのキーで保存するか」は形式の一部なのでこの層が持ち、
// 実際の読み書きは外側が行う。
extern const char* kTuningStorageKey;
