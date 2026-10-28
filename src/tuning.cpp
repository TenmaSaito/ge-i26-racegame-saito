// ============================================================================
// tuning.cpp ― ドリフト調整値（項目表と変換・保存）
// 役割: 調整項目の表と、テキスト形式との相互変換、保存/復元の窓口。
//       保存先は platform 層に任せ、このファイルは形式だけを知る。
// ============================================================================

#include "tuning.hpp"

#include "car.hpp"       // driftTuning（調整値の実データ）
#include "geom.hpp"      // clampf
#include "platform.hpp"  // saveSetting / loadSetting

#include <cstdio>
#include <cstdlib>
#include <cstring>

// --- ドリフトのチューニング -------------------------------------------------
// 項目テーブル。value は driftTuning の各フィールドへのポインタ。
// 

TuneItem tuneItems[] = {
    {"DRIFT GRIP", &driftTuning.gripDrift, 0.2f, 4.0f, 0.05f, 0},
    {"INERTIA", &driftTuning.inertia, 0.4f, 2.5f, 0.05f, 0},
    {"DRIFT DECEL", &driftTuning.driftDecel, 0.0f, 1.5f, 0.05f, 0},
    {"MAX SLIP", &driftTuning.maxSlip, 0.35f, 1.25f, 0.035f, 1},
    {"MIN SPEED", &driftTuning.minSpeed, 0.0f, 25.0f, 1.0f, 2},
    {"NORMAL GRIP", &driftTuning.gripNormal, 6.0f, 30.0f, 0.5f, 3},
    {"ENTRY", &driftTuning.entrySharp, 0.0f, 1.0f, 0.05f, 3},
};
const int kTuneItemCount = static_cast<int>(sizeof(tuneItems) / sizeof(tuneItems[0]));

void clampTuning() {
    for (const TuneItem& item : tuneItems) {
        *item.value = clampf(*item.value, item.lo, item.hi);
    }
}


std::string serializeTuning() {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "gripNormal=%.3f gripDrift=%.3f inertia=%.3f driftDecel=%.3f maxSlip=%.3f minSpeed=%.3f entry=%.3f",
                  driftTuning.gripNormal, driftTuning.gripDrift, driftTuning.inertia,
                  driftTuning.driftDecel, driftTuning.maxSlip, driftTuning.minSpeed,
                  driftTuning.entrySharp);
    return buf;
}

void parseTuning(const char* text) {
    if (!text) {
        return;
    }
    auto get = [&](const char* key, float& out) {
        const char* p = std::strstr(text, key);
        if (p && p[std::strlen(key)] == '=') {
            out = std::strtof(p + std::strlen(key) + 1, nullptr);
        }
    };
    get("gripNormal", driftTuning.gripNormal);
    get("gripDrift", driftTuning.gripDrift);
    get("inertia", driftTuning.inertia);
    get("driftDecel", driftTuning.driftDecel);
    get("maxSlip", driftTuning.maxSlip);
    get("minSpeed", driftTuning.minSpeed);
    get("entry", driftTuning.entrySharp);
    clampTuning();
}

// 保存キーは旧来の localStorage キーと同名にしてある（Chrome 拡張と互換）。
const char* kTuningKey = "racegameDriftTuning";

void saveTuning() {
    platform::saveSetting(kTuningKey, serializeTuning());
}

void loadTuning() {
    std::string data;
    if (platform::loadSetting(kTuningKey, data)) {
        parseTuning(data.c_str());
    }
}

#ifdef __EMSCRIPTEN__
// ブラウザ版: 外部ツールが localStorage を書き換えたらライブ反映する。
// 最後に見た内容と違うときだけ parse し直す。
void pollExternalTuning() {
    static std::string lastSeen;
    static bool first = true;
    std::string current;
    if (!platform::loadSetting(kTuningKey, current)) {
        return;
    }
    if (first) {
        first = false;
        lastSeen = current;
        return;
    }
    if (current != lastSeen) {
        lastSeen = current;
        parseTuning(current.c_str());
    }
}
#else
void pollExternalTuning() {}
#endif
