# Android Studio への移植手順

このコードベースは、Android へ持っていく前提で層を分けてある。
依存ライブラリは **SDL2 のみ**。画像・音声ライブラリ（SDL_image / SDL_mixer）は使っていない。

## 構成の考え方

| 層 | ファイル | Android での扱い |
|---|---|---|
| エントリ | `src/main.cpp` | 無変更（SDL が `main` を受け取る） |
| プラットフォーム層 | `src/platform.cpp/.hpp` | SDL2 が窓口になるので原則無変更 |
| ゲーム進行 | `src/game.cpp` ほか | 無変更 |
| ロジック層 | `car / track / scenery` | SDL・GL 非依存。無変更 |
| 描画 | `src/render.cpp` | `__ANDROID__` のとき自動で OpenGL ES 3 のヘッダとシェーダを使う |

環境差は `platform.cpp` に閉じ込めてある:

- ファイル読み込み: `platform::readTextFile` が Android では `SDL_RWops` 経由で
  APK 内アセットを読む（通常の `ifstream` では読めないため）。ゲーム側は
  「パスを渡して文字列を受け取る」だけなので、呼び出し側の変更は不要。
- 設定の保存: `platform::saveSetting / loadSetting` が Android では
  `SDL_GetPrefPath` の内部ストレージ領域に書く。
- 描画 API: OpenGL ES 3.0（Android の標準）。デスクトップ GL との差は
  `render.cpp` の `#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)` で吸収済み。

## 手順（Android Studio）

1. **プロジェクト作成**: 「Native C++」テンプレートで新規プロジェクトを作る
   （パッケージ名は任意。ツールチェーンは既定の CMake / NDK でよい）。
2. **SDL2 の配置**: SDL2 のソース一式（公式配布の `SDL2-2.x.x`）を
   `app/src/main/cpp/SDL` に置き、`app/src/main/cpp/CMakeLists.txt` で
   `add_subdirectory(SDL)` する。SDL の公式 android-project テンプレートを
   土台にする方法でもよい。
3. **ゲーム本体の配置**: このリポジトリの `src/` を
   `app/src/main/cpp/game/` に、`assets/` を
   `app/src/main/assets/` にコピーする。
   - アセットは `assets/track/*.txt` の相対パスのまま読める
     （`platform::readTextFile` が SDL_RWops で APK 内を読む）。
4. **CMakeLists の書き換え**: `app/src/main/cpp/CMakeLists.txt` の
   `add_library` の対象を、リポジトリ側 `CMakeLists.txt` のソース一覧と
   同じファイル群にする。共有ライブラリ名は SDL の規約に合わせて `main`:
   ```cmake
   add_library(main SHARED
       game/main.cpp game/platform.cpp game/game.cpp game/input.cpp
       game/camera.cpp game/effects.cpp game/hud.cpp game/tuning.cpp
       game/carmodel.cpp game/track.cpp game/scenery.cpp game/car.cpp
       game/render.cpp game/selftest.cpp)
   target_link_libraries(main SDL2 GLESv3 log)
   ```
   ※ `selftest.cpp` は製品に不要なら外してよい（依存は car/track のみ）。
5. **Activity**: SDL 付属の `SDLActivity` を使う。テンプレート側の
   `MainActivity` を `org.libsdl.app.SDLActivity` の継承にするか、
   マニフェストで SDLActivity を直接指定する。ライブラリ名は
   SDLActivity の `getLibraries()` / マニフェストの `android.app.lib_name`
   で `main`（と内部で `SDL2`）に合わせる。
6. **ビルドと実行**: ABI は `arm64-v8a` を主対象にする（近年の実機）。
   エミュレータでも OpenGL ES 3.0 が有効なら動作する。

## 移植時に確認すること

- 画面の向き: 横向き固定が前提のゲームなので、マニフェストで
  `android:screenOrientation="landscape"` を指定する。
- 入力: タッチ操作は未実装。Android では SDL のゲームパッド API が
  そのまま使えるので、まず Bluetooth パッドでの動作確認を推奨。
  タッチ対応をする場合は `input.cpp` に SDL のタッチイベントを
  DriveInput へ変換する経路を足すのが分担として自然。
- 復帰時: SDL が `SDL_APP_WILLENTERBACKGROUND` 等を送る。現状は
  イベントループで無視しているが、実害が出たら `game.cpp` の
  イベント処理でポーズに落とす。
- 音: 現在は無音（音声ライブラリ自体を使っていない）。足す場合も
  SDL 本体の音声 API か OpenSL ES を platform 層に足す方針にする。
