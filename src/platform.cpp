// ============================================================================
// platform.cpp ― プラットフォーム層の実装（SDL2 が唯一の依存）
// 役割: ウィンドウ/GL の生成、ファイル読み込み、設定の保存、
//       メインループの駆動。OS や実行環境の違いはここで吸収する。
// Android への移植: SDL2 が Android でも同じ API を提供するため、
//       このファイルは原則そのまま使える（アセット読み込みは SDL_RWops）。
// 依存: SDL2 本体に加え、テクスチャ読み込み用 SDL_image と
//       音声用 SDL_mixer（どちらも SDL 公式の姉妹ライブラリで Android 対応）。
// ============================================================================

#include "platform.hpp"

#include "game.hpp"

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
#endif

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

bool createWindowImpl(Game& app) {
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
    app.window = SDL_CreateWindow(app.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!app.window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    app.gl = SDL_GL_CreateContext(app.window);
    if (!app.gl) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    if (!app.renderer.init()) {
        SDL_Log("GL init failed");
        return false;
    }
    return true;
}


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
    return createWindowImpl(app);
}

void destroyWindow(Game& app) {
    Mix_CloseAudio();
    Mix_Quit();
    IMG_Quit();
    if (app.gl) {
        SDL_GL_DeleteContext(app.gl);
        app.gl = nullptr;
    }
    if (app.window) {
        SDL_DestroyWindow(app.window);
        app.window = nullptr;
    }
    SDL_Quit();
}

namespace {
void loopCallback(void* userdata) {
    gameFrame(*static_cast<Game*>(userdata));
}
}  // namespace

void runMainLoop(Game& app) {
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(loopCallback, &app, 0, 1);
#else
    while (app.running) {
        Uint64 frameStart = SDL_GetPerformanceCounter();
        gameFrame(app);
        double frameMs = double(SDL_GetPerformanceCounter() - frameStart) /
                         double(SDL_GetPerformanceFrequency()) * 1000.0;
        if (frameMs < 14.0) {
            SDL_Delay(static_cast<Uint32>(14.0 - frameMs));
        }
    }
#endif
}

}  // namespace platform
