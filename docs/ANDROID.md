# Android Studio への移植手順

このコードベースは、Android へ持っていく前提で層を分けてある。
依存ライブラリは **SDL 系のみ**（SDL2 本体＋テクスチャ用 SDL_image）。
どちらも SDL 公式のライブラリで、Android でも同じ手順で取り込める。
音は SDL2 本体の音声デバイスへ自前のミキサー（`src/domain/sound/`）で書くため、
SDL_mixer は不要（Android では SDL が AAudio / OpenSL ES を仲介する）。

## 構成の考え方 (レイヤードアーキテクチャ)

依存は **外側 → 内側の一方向のみ**。内側のコードは外側の実装を直接知らない。
外側の能力が要る場所では、内側に「ポート (抽象)」を宣言し、外側が実装を
差し込む (依存性逆転)。

| 層 | フォルダ | 知ってよいもの / Android での扱い |
|---|---|---|
| ドメイン (最内) | `src/domain/`（vehicle / course / scenery / tuning / math / **sound**） | 標準ライブラリのみ。SDL・GL・OS を知らない。無変更 |
| アプリケーション | `src/app/`（core: Game / scenes: Title・Select・Race・Result / view: Hud・Camera・Effects / race / vehicle: carmodel・**EngineSim**） | ドメイン＋ポートだけを見る。無変更 |
| ポート宣言 | `src/ports/`（render.hpp: IRenderer / input.hpp / **audio.hpp: IAudio**） | SDL を含まない宣言だけ |
| アダプタ (外側) | `src/platform/`（render: OpenGL / input: SDL・Gamepad / audio: SDL 音声デバイス / system: 窓・保存・ループ） | SDL / GL / Emscripten はここだけに現れる |
| 合成ルート | `src/platform/entry/main.cpp` | 全部品の配線だけ |

具体的な差し込み:
- 描画: platform が `createRenderer()` で GlRenderer を作り Game へ差し込む。
  内側は `IRenderer*` しか見ない。gl 関数の入手は `glapi.hpp` が吸収し、
  Android では `__ANDROID__` のとき自動で OpenGL ES 3 のヘッダとシェーダになる。
- 音: platform が `createAudio()` で SdlAudio を作り Game へ差し込む。
  ゲームは `EngineSound` / `TireSound` / `SoundCue`（`soundparams.hpp`）を
  渡すだけで、波形の合成は SoundMixer（ドメイン層・純 C++）が行う。
  SdlAudio は「SDL の音声デバイスを開いて、要求されたら SoundMixer に
  書かせる」だけの薄い層。
- 入力: SDL イベントは input.cpp が「意図」(UiActions) に翻訳して渡す。
- 時間と表示: 経過時間の計測・画面サイズ反映・Swap は platform のループが担当。
- 保存とファイル: `platform::readTextFile` が Android では `SDL_RWops` 経由で
  APK 内アセットを読む（コースも音素材も同じ口）。設定の保存先は
  `SDL_GetPrefPath` の内部ストレージ。

## 手順（Android Studio）

1. **プロジェクト作成**: SDL 公式の `android-project` テンプレート
   （SDL2 ソースの `android-project/`）を土台にするのが最短。
   「Native C++」テンプレートから作る場合は手順 5 の Activity 設定が要る。
2. **SDL2 の配置**: SDL2 と SDL2_image のソース一式を
   `app/jni/SDL`、`app/jni/SDL_image` に置き、CMake で `add_subdirectory` する。
3. **ゲーム本体の配置**: このリポジトリの `src/` を `app/jni/src/game/` に、
   `assets/` を `app/src/main/assets/` にコピーする。
   アセットは `assets/track/*.txt`・`assets/sound/*.wav` の相対パスのまま読める。
4. **CMakeLists**: リポジトリ側 `CMakeLists.txt` の `add_executable(racegame ...)`
   と同じソース一覧・同じ include ディレクトリで、共有ライブラリ `main` を作る:
   ```cmake
   add_library(main SHARED ${RACEGAME_SOURCES})   # リポジトリ側と同じ一覧
   target_include_directories(main PRIVATE ${RACEGAME_INCLUDE_DIRS})
   target_link_libraries(main SDL2 SDL2main SDL2_image GLESv3 log)
   ```
   `selftest.cpp` / `bench.cpp` は製品に不要なら外してよい（main.cpp の
   `--self-test` / `--bench` 分岐も合わせて外す）。
5. **Activity**: SDL 付属の `SDLActivity` を使う。マニフェストで
   `android.app.lib_name` を `main` にする。
6. **ビルドと実行**: ABI は `arm64-v8a` を主対象にする。
   エミュレータでも OpenGL ES 3.0 が有効なら動作する。

## 移植時に確認すること

- 画面の向き: 横向き固定が前提なので、マニフェストで
  `android:screenOrientation="landscape"` を指定する。
- 入力: タッチ操作は未実装。SDL のゲームパッド API がそのまま使えるので、
  まず Bluetooth パッドでの動作確認を推奨。タッチ対応は `input.cpp` に
  SDL のタッチイベントを DriveInput へ変換する経路を足すのが自然。
- 復帰時: SDL はアプリが裏に回ると音声デバイスを自動で一時停止する。
  ゲームのポーズまで連動させたい場合は `SDL_APP_WILLENTERBACKGROUND` を
  input.cpp で受けて UiActions のポーズへ流す。
- 音の遅延: Android 端末は音声バッファが大きめのことがある。`audio.cpp` の
  `want.samples`（既定 1024）を端末に合わせて調整できる。
