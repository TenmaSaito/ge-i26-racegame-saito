# Android Studio への移植手順

このコードベースは、Android へ持っていく前提で層を分けてある。
依存ライブラリは **SDL 系のみ**（SDL2 本体＋テクスチャ用 SDL_image＋音声用 SDL_mixer）。
いずれも SDL 公式の姉妹ライブラリで、Android でも同じ手順で取り込める。

## 構成の考え方 (レイヤードアーキテクチャ)

依存は **外側 → 内側の一方向のみ**。内側のコードは外側の実装を直接知らない。
外側の能力が要る場所では、内側に「ポート (抽象)」を宣言し、外側が実装を
差し込む (依存性逆転)。

| 層 | ファイル | 知ってよいもの / Android での扱い |
|---|---|---|
| ドメイン (最内) | car / track / scenery / tuning / geom / meshbuild | 標準ライブラリのみ。SDL・GL・OS を知らない。無変更 |
| アプリケーション | game (Game クラス) / hud (Hud) / camera (Orbit) / effects (Effects) / race (Race) / carmodel | ドメイン＋ポートだけを見る。状態は各クラスに閉じ、Game が統括する。無変更 |
| ポート宣言 | render.hpp (IRenderer) / input.hpp (UiActions 等) | SDL を含まない宣言だけ |
| アダプタ (外側) | render.cpp (OpenGL) / input.cpp (SDL・Gamepad API) / platform.cpp (SDL・窓・保存・ループ) | SDL / GL / Emscripten はここだけに現れる |
| 合成ルート | main.cpp | 全部品の配線だけ (SDL すら直接触らない) |

具体的な差し込み:
- 描画: platform が `createRenderer()` で GlRenderer を作り `Game.renderer` へ。
  内側は `IRenderer*` しか見ない。Android では `__ANDROID__` のとき自動で
  OpenGL ES 3 のヘッダとシェーダに切り替わる。
- 入力: SDL イベントは input.cpp が「意図」(UiActions) に翻訳して渡す。
  キー割り当ての解釈は外側の責任。F キーのフルスクリーンは表示系操作なので
  入力アダプタがその場で platform に実行させる。パッドの所有も入力側。
- 時間と表示: 経過時間の計測・画面サイズ反映・Swap は platform のループが
  担当し、gameFrame には dt を渡すだけ。
- 保存: tuning は文字列化と復元だけ (純粋)。読み書きは platform が行い、
  ゲーム内からの保存は Game の差し込み口 `saveTuningText` 経由。
  ファイル読み込みは `platform::readTextFile` が Android では `SDL_RWops`
  経由で APK 内アセットを読む。設定の保存先は `SDL_GetPrefPath` の
  内部ストレージ (ブラウザでは localStorage)。ブラウザの外部変更
  (Chrome 拡張) の監視・反映も platform のループ側の仕事。
- ページ側デバッグ表示: hud は文字列を作るだけ。送信は Game の
  差し込み口 `debugSink` 経由で platform が行う。

## 手順（Android Studio）

1. **プロジェクト作成**: 「Native C++」テンプレートで新規プロジェクトを作る
   （パッケージ名は任意。ツールチェーンは既定の CMake / NDK でよい）。
2. **SDL2 の配置**: SDL2 のソース一式（公式配布の `SDL2-2.x.x`）を
   `app/src/main/cpp/SDL` に置き、`app/src/main/cpp/CMakeLists.txt` で
   `add_subdirectory(SDL)` する。SDL_image / SDL_mixer も同様に
   それぞれのフォルダへ置いて `add_subdirectory` する。
   SDL の公式 android-project テンプレートを土台にする方法でもよい。
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
   target_link_libraries(main SDL2 SDL2_image SDL2_mixer GLESv3 log)
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
- 音とテクスチャ: SDL_mixer / SDL_image の初期化は platform 層で済ませてある
  （`platform::createWindow`）。実際に鳴らす・貼る処理を足すときは、
  ファイル読み込みを `platform::readTextFile` と同様の口に寄せ、
  描画は render 層、再生は platform 層に置くのが分担として自然。
