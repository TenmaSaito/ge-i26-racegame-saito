#pragma once
// ============================================================================
// glapi.hpp ― OpenGL 関数の入手口 (プラットフォーム差の吸収)
// 役割: 描画実装 (render.cpp) が「どの環境でも同じ名前で gl 関数を呼べる」
//       ようにする。環境ごとの違いは次のとおり:
//   - ブラウザ (WebGL2) / Android: OpenGL ES 3 のヘッダをそのまま使う。
//   - Linux:   GL_GLEXT_PROTOTYPES でライブラリの関数を直接リンクする。
//   - Windows: opengl32.dll は GL 1.1 までしか公開しないため、GL 2.0 以降の
//              関数は SDL_GL_GetProcAddress で実行時に取り寄せる
//              (下の RACEGAME_GL_FUNCTIONS の一覧が対象)。
// 使い方: GL コンテキストを作った直後に loadGlFunctions() を 1 回呼ぶ。
// ============================================================================

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
// ブラウザ (WebGL2) と Android は OpenGL ES 3 が共通の土台。
#include <GLES3/gl3.h>
#define RACEGAME_GLES 1

#elif defined(_WIN32)
// SDL_opengl.h は GL 1.1 の宣言と、関数ポインタ型 (PFNGL...PROC) を提供する。
#include <SDL_opengl.h>

// 実行時に取り寄せる関数の一覧: X(関数ポインタ型, 関数名)
#define RACEGAME_GL_FUNCTIONS(X)                                      \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                          \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                              \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                    \
    X(PFNGLBUFFERDATAPROC, glBufferData)                              \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                        \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                        \
    X(PFNGLCREATESHADERPROC, glCreateShader)                          \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                          \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)    \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                              \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                    \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                          \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                  \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                            \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)              \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                            \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                          \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                  \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                              \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

// 関数ポインタの宣言 (実体は glapi.cpp)。名前は racegame_ を付けて衝突を避け、
// 下のマクロで本来の gl 名へ付け替える (render.cpp 側は普通に gl... と書ける)。
#define RACEGAME_GL_DECLARE(type, name) extern type racegame_##name;
RACEGAME_GL_FUNCTIONS(RACEGAME_GL_DECLARE)
#undef RACEGAME_GL_DECLARE

#define glAttachShader racegame_glAttachShader
#define glBindBuffer racegame_glBindBuffer
#define glBindVertexArray racegame_glBindVertexArray
#define glBufferData racegame_glBufferData
#define glCompileShader racegame_glCompileShader
#define glCreateProgram racegame_glCreateProgram
#define glCreateShader racegame_glCreateShader
#define glDeleteShader racegame_glDeleteShader
#define glEnableVertexAttribArray racegame_glEnableVertexAttribArray
#define glGenBuffers racegame_glGenBuffers
#define glGenVertexArrays racegame_glGenVertexArrays
#define glGetProgramInfoLog racegame_glGetProgramInfoLog
#define glGetProgramiv racegame_glGetProgramiv
#define glGetShaderInfoLog racegame_glGetShaderInfoLog
#define glGetShaderiv racegame_glGetShaderiv
#define glGetUniformLocation racegame_glGetUniformLocation
#define glLinkProgram racegame_glLinkProgram
#define glShaderSource racegame_glShaderSource
#define glUniform1f racegame_glUniform1f
#define glUniform3f racegame_glUniform3f
#define glUniformMatrix4fv racegame_glUniformMatrix4fv
#define glUseProgram racegame_glUseProgram
#define glVertexAttribPointer racegame_glVertexAttribPointer

#else
// Linux などのデスクトップ: ライブラリが GL 3.3 の関数を直接公開している。
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

// GL 関数を使える状態にする。GL コンテキストが現在のスレッドで有効な状態で
// 呼ぶこと。取り寄せに失敗したら false (Windows 以外は常に true)。
bool loadGlFunctions();
