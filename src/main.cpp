// ============================================================================
// main.cpp ― エントリポイント
// 役割: 起動の段取りだけを持つ（コース決定→読み込み→初期化→ループ→終了）。
//       ゲームの処理は game / input / camera / effects / hud / tuning、
//       環境依存は platform に分担してある。移植時は原則このファイルを
//       触らず、platform 層の差し替えで対応する。
// ============================================================================

#include "game.hpp"
#include "platform.hpp"
#include "selftest.hpp"
#include "track.hpp"
#include "tuning.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace {

// コース名はパストラバーサル防止に英数字と _- のみ許可する。
std::string sanitizeTrackStem(const char* raw) {
    std::string out;
    if (!raw) {
        return out;
    }
    for (const char* p = raw; *p && out.size() < 32; ++p) {
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out += c;
        }
    }
    return out;
}

// コース選択: ネイティブは --track <名前>、ブラウザは ?track=<名前>。既定は suzuka。
std::string pickTrackStem(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--track") == 0) {
            std::string stem = sanitizeTrackStem(argv[i + 1]);
            if (!stem.empty()) {
                return stem;
            }
        }
    }
    std::string stem = sanitizeTrackStem(platform::urlQueryParam("track").c_str());
    if (!stem.empty()) {
        return stem;
    }
    return "suzuka";
}

// コースファイルを探して読み込む（実行場所によって基点が違うため候補を順に試す）。
bool loadTrack(const std::string& stem, Track& track) {
    const std::string file = stem + ".txt";
    const std::string paths[] = {
        "assets/track/" + file,
        "../assets/track/" + file,
        "/assets/track/" + file,
    };
    for (const std::string& path : paths) {
        std::string text;
        if (platform::readTextFile(path, text)) {
            return track.loadFromText(text);
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    bool test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-test") == 0) {
            test = true;
        }
    }
    std::string stem = pickTrackStem(argc, argv);
    Track track;
    if (!loadTrack(stem, track)) {
        if (stem == "suzuka") {
            std::fprintf(stderr, "コースを読めません: %s\n", stem.c_str());
            return 1;
        }
        // 指定コースが無ければ既定コースに落とす（ブラウザのタイプミス対策）。
        stem = "suzuka";
        if (!loadTrack(stem, track)) {
            std::fprintf(stderr, "コースを読めません: %s\n", stem.c_str());
            return 1;
        }
    }
    if (test) {
        // 鈴鹿は従来どおり厳密な全長チェック、その他のコースは緩い範囲で見る。
        return stem == "suzuka" ? runSelfTest(track, 5200.0f, 6500.0f)
                                : runSelfTest(track, 1500.0f, 15000.0f);
    }
    // 保存済みの調整値を読む (読み取りは platform、解釈は tuning の担当)。
    {
        std::string saved;
        if (platform::loadSetting(kTuningStorageKey, saved)) {
            parseTuning(saved.c_str());
        }
    }

    Game app;
    if (stem == "flow") {
        app.title = "Flow Circuit";
    }
    if (stem == "coast") {
        app.title = "Coast Circuit";
    }
    app.track = std::move(track);
    if (!platform::createWindow(app)) {
        return 1;
    }
    gameInit(app);
    platform::runMainLoop(app);
    platform::destroyWindow(app);  // 終了処理はすべて外側 (platform) の担当
    return 0;
}
