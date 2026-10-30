// ============================================================================
// glapi.cpp ― OpenGL 関数の入手口の実装
// Windows だけ、GL 2.0 以降の関数を SDL_GL_GetProcAddress で取り寄せる。
// それ以外の環境ではヘッダ/ライブラリが直接提供するので何もしない。
// ============================================================================

#include "glapi.hpp"

#include <SDL.h>

#if defined(_WIN32) && !defined(RACEGAME_GLES)

// 関数ポインタの実体。
#define RACEGAME_GL_DEFINE(type, name) type racegame_##name = nullptr;
RACEGAME_GL_FUNCTIONS(RACEGAME_GL_DEFINE)
#undef RACEGAME_GL_DEFINE

bool loadGlFunctions() {
    bool ok = true;
    // 1 つずつ取り寄せ、取れなかった関数名はログに残す (原因の切り分け用)。
#define RACEGAME_GL_LOAD(type, name)                                          \
    racegame_##name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));   \
    if (!racegame_##name) {                                                   \
        SDL_Log("GL function not found: %s", #name);                          \
        ok = false;                                                           \
    }
    RACEGAME_GL_FUNCTIONS(RACEGAME_GL_LOAD)
#undef RACEGAME_GL_LOAD
    return ok;
}

#else

bool loadGlFunctions() {
    return true;
}

#endif
