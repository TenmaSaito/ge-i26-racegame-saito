// ============================================================================
// platform.cpp ― プラットフォーム層の実装 (最も外側の層)
// 役割: ウィンドウ/GL の生成と所有、描画実装の生成と Game への差し込み、
//       入力アダプタへのウィンドウ引き渡し、ファイル読み込み、設定の保存、
//       メインループの駆動 (時間計測・画面サイズ反映・画面表示まで)。
//       OS や実行環境の違いはすべてここで吸収する。
// レイヤー方針: 依存は「外側 → 内側」のみ。この層は内側 (game / tuning 等)
//       を知ってよいが、内側はこの層を直接知らない (ポート経由)。
// Android への移植: SDL が Android でも同じ API を提供するため、
//       このファイルは原則そのまま使える（アセット読み込みは SDL_RWops）。
// 依存: SDL2 本体＋テクスチャ用 SDL_image＋音声用 SDL_mixer
//       (いずれも SDL 公式の姉妹ライブラリで Android 対応)。
// ============================================================================

#include "platform.hpp"

#include "game.hpp"    // Game (状態の置き場)
#include "input.hpp"   // input::attachWindow / detach
#include "render.hpp"  // createRenderer / destroyRenderer
#include "tuning.hpp"  // parseTuning / kTuningStorageKey (外部変更の反映用)
#include "ranking.hpp"  // kRankingStorageKey

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdlib>

// ブラウザ版の設定保存先は localStorage（ファイルは永続しないため）。
EM_JS(void, settingSaveJs, (const char* key, const char* data), {
    try { localStorage.setItem(UTF8ToString(key), UTF8ToString(data)); } catch (e) {}
});
EM_JS(char*, settingLoadJs, (const char* key), {
    try {
        const s = localStorage.getItem(UTF8ToString(key));
        if (!s) return 0;
        const n = lengthBytesUTF8(s) + 1;
        const p = _malloc(n);
        stringToUTF8(s, p, n);
        return p;
    } catch (e) { return 0; }
});
// コース選択などは URL の ?track=<名前> で受ける。
EM_JS(char*, queryParamJs, (const char* key), {
    try {
        const s = new URLSearchParams(location.search).get(UTF8ToString(key));
        if (!s) return 0;
        const n = lengthBytesUTF8(s) + 1;
        const p = _malloc(n);
        stringToUTF8(s, p, n);
        return p;
    } catch (e) { return 0; }
});
// ページ側のデバッグ欄 (window.raceSetDebug) へ文字列を渡す。
EM_JS(void, debugToPageJs, (const char* text), {
    if (window.raceSetDebug) window.raceSetDebug(UTF8ToString(text));
});
#endif

namespace {

// この層が所有する実物。Game はウィンドウや GL を知らない。
SDL_Window* gWindow = nullptr;
SDL_GLContext gGl = nullptr;
IRenderer* gRenderer = nullptr;  // Game へ差し込んだ描画実装 (所有はこの層)

}  // namespace

namespace platform {

bool readTextFile(const std::string& path, std::string& out) {
#ifdef __ANDROID__
    // Android では通常のファイル API で APK 内アセットを読めないため、
    // SDL_RWops 経由で読む（SDL がアセットマネージャを仲介する）。
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) {
        return false;
    }
    Sint64 size = SDL_RWsize(rw);
    if (size < 0) {
        SDL_RWclose(rw);
        return false;
    }
    out.resize(static_cast<size_t>(size));
    size_t read = SDL_RWread(rw, out.data(), 1, static_cast<size_t>(size));
    SDL_RWclose(rw);
    return read == static_cast<size_t>(size);
#else
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
#endif
}

namespace {
// 設定ファイルの場所。OS のアプリ用フォルダが取れなければカレントに置く。
std::string settingPath(const std::string& key) {
    char* pref = SDL_GetPrefPath("", "racegame");
    if (pref) {
        std::string path = std::string(pref) + key + ".txt";
        SDL_free(pref);
        return path;
    }
    return key + ".txt";
}
}  // namespace

bool saveSetting(const std::string& key, const std::string& value) {
#ifdef __EMSCRIPTEN__
    settingSaveJs(key.c_str(), value.c_str());
    return true;
#else
    std::ofstream out(settingPath(key), std::ios::trunc);
    if (!out) {
        return false;
    }
    out << value << "\n";
    return true;
#endif
}

bool loadSetting(const std::string& key, std::string& out) {
#ifdef __EMSCRIPTEN__
    char* p = settingLoadJs(key.c_str());
    if (!p) {
        return false;
    }
    out = p;
    std::free(p);
    return true;
#else
    return readTextFile(settingPath(key), out);
#endif
}

std::string urlQueryParam(const char* key) {
#ifdef __EMSCRIPTEN__
    char* p = queryParamJs(key);
    if (!p) {
        return "";
    }
    std::string value = p;
    std::free(p);
    return value;
#else
    (void)key;
    return "";
#endif
}

// ページ側デバッグ欄への送信 (ブラウザのみ実働、他では何もしない)。
// Game の debugSink にはこの関数が差し込まれる。
void sendDebugText(const char* text) {
#ifdef __EMSCRIPTEN__
    debugToPageJs(text);
#else
    (void)text;
#endif
}

// フルスクリーン切替 (F キー)。入力アダプタから呼ばれる表示系の操作。
// ページ側から強制するとキャンバスと描画バッファがずれて暗転するため、
// 必ず SDL 経由で切り替える。
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
        if (readTextFile(path, text)) {
            return track.loadFromText(text);
        }
    }
    return false;
}

void toggleFullscreen() {
    if (!gWindow) {
        return;
    }
    Uint32 flags = SDL_GetWindowFlags(gWindow);
    SDL_SetWindowFullscreen(gWindow, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

}  // namespace platform

namespace platform {

bool createWindow(Game& app) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    // テクスチャ (PNG) と音声 (OGG) の土台だけ先に用意しておく。
    // 読み込み自体は今後の実装で platform の読み込み口から行う。
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        SDL_Log("IMG_Init failed: %s", IMG_GetError());
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
    }
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    gWindow = SDL_CreateWindow(app.title().c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!gWindow) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    gGl = SDL_GL_CreateContext(gWindow);
    if (!gGl) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    // 描画実装を生成して Game へ差し込む (内側は IRenderer だけを見る)。
    // 実物の所有はこの層が持ち、destroyWindow で破棄する。
    gRenderer = createRenderer();
    app.installRenderer(gRenderer);
    if (!gRenderer || !gRenderer->init()) {
        SDL_Log("GL init failed");
        return false;
    }
    // 入力アダプタへウィンドウを引き渡す (パッドの所有も入力側)。
    input::attachWindow(gWindow);
    // Game の差し込み口に、この層の実装を登録する。
    app.setDebugSink([](const char* text) { sendDebugText(text); });
    // コース読み込みとランキング保存の差し込み口もここで配線する。
    app.setTrackLoader([](const std::string& stem, Track& track) { return loadTrack(stem, track); });
    app.setRankingSink([](const std::string& text) { saveSetting(kRankingStorageKey, text); });
    {
        std::string savedRanking;
        if (loadSetting(kRankingStorageKey, savedRanking)) {
            app.setRankingText(savedRanking);
        }
    }
    app.setSaveTuningSink([](const std::string& text) {
        saveSetting(kTuningStorageKey, text);
    });
    return true;
}

void destroyWindow(Game& app) {
    input::detach();
    if (gRenderer) {
        destroyRenderer(gRenderer);
        gRenderer = nullptr;
        app.installRenderer(nullptr);
    }
    Mix_CloseAudio();
    Mix_Quit();
    IMG_Quit();
    if (gGl) {
        SDL_GL_DeleteContext(gGl);
        gGl = nullptr;
    }
    if (gWindow) {
        SDL_DestroyWindow(gWindow);
        gWindow = nullptr;
    }
    SDL_Quit();
}

namespace {

// ブラウザ版のみ: 外部ツール (Chrome 拡張など) が保存値を書き換えたら
// 走行へライブ反映する。監視と読み取りは外側の仕事なのでこの層で行い、
// 内側 (tuning) には parse だけをさせる。約 0.5 秒に 1 回見る。
void pollExternalTuningChange() {
#ifdef __EMSCRIPTEN__
    static int tick = 0;
    static std::string lastSeen;
    static bool first = true;
    if (++tick < 30) {
        return;
    }
    tick = 0;
    std::string current;
    if (!loadSetting(kTuningStorageKey, current)) {
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
#endif
}

// 1 フレーム分の外側の仕事: 時間を測る → サイズを反映 → 内側を進める → 表示。
void frameOnce(Game& app) {
    static Uint64 lastCounter = 0;
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = lastCounter == 0
                    ? 0.0
                    : double(now - lastCounter) / double(SDL_GetPerformanceFrequency());
    lastCounter = now;

    pollExternalTuningChange();

    int drawableW = 0;
    int drawableH = 0;
    SDL_GL_GetDrawableSize(gWindow, &drawableW, &drawableH);
    if (IRenderer* renderer = app.renderer()) {
        renderer->resize(drawableW, drawableH);
    }
    app.frame(dt);
    SDL_GL_SwapWindow(gWindow);
}

void loopCallback(void* userdata) {
    frameOnce(*static_cast<Game*>(userdata));
}

}  // namespace

void runMainLoop(Game& app) {
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(loopCallback, &app, 0, 1);
#else
    while (app.running()) {
        Uint64 frameStart = SDL_GetPerformanceCounter();
        frameOnce(app);
        double frameMs = double(SDL_GetPerformanceCounter() - frameStart) /
                         double(SDL_GetPerformanceFrequency()) * 1000.0;
        if (frameMs < 14.0) {
            SDL_Delay(static_cast<Uint32>(14.0 - frameMs));
        }
    }
#endif
}

}  // namespace platform
