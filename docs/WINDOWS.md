# Windows への移植手順

ソースは Windows でもそのままビルドできるように書いてある。
環境ごとの違いは次の 2 か所だけに閉じている。

| 違い | 吸収している場所 | 内容 |
|---|---|---|
| OpenGL 3.3 の関数 | `src/platform/render/glapi.hpp/.cpp` | Windows の opengl32.dll は GL 1.1 までしか公開しないため、残りは `SDL_GL_GetProcAddress` で起動時に取り寄せる |
| SDL2 の見つけ方 | `CMakeLists.txt` | まず CMake パッケージ（vcpkg / 公式 devel 版）を探し、無ければ pkg-config |

音（`src/domain/sound/` のミキサー＋ `src/platform/audio/audio.cpp`）は
SDL2 本体の音声 API だけを使うので、Windows では SDL が WASAPI を仲介する。

## 手順（Visual Studio 2022 + vcpkg）

1. vcpkg で SDL2 と SDL2_image を入れる:
   ```bat
   vcpkg install sdl2 sdl2-image --triplet x64-windows
   ```
2. CMake でビルドする（vcpkg のツールチェーンを渡す）:
   ```bat
   cmake -B build-win -G "Visual Studio 17 2022" -A x64 ^
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build build-win --config Release
   ```
   `assets/` はビルド後に実行ファイルの隣へ自動でコピーされる。
3. 実行: `build-win\Release\racegame.exe`
   （vcpkg の x64-windows は SDL2.dll などを出力先へ自動で配置する）。

vcpkg を使わず SDL 公式の `SDL2-devel-2.x.x-VC.zip` を使う場合は、
`-DSDL2_DIR=<展開先>/cmake` と `-DSDL2_image_DIR=<展開先>/cmake` を渡す。

## 確認すること

- 設定の保存先は `SDL_GetPrefPath` が返す `%APPDATA%\racegame\`。
- 起動時に「GL function not found」と出る場合は、GPU ドライバが
  OpenGL 3.3 に対応していない（古い/仮想環境）。ドライバを更新する。
