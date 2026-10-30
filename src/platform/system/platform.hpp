#pragma once
// ============================================================================
// platform.hpp ― プラットフォーム層の宣言 (最も外側の層)
// 役割: ウィンドウと GL の生成と所有、描画実装の生成と Game への差し込み、
//       ファイル読み込み、設定の保存、メインループの駆動だけを担当する。
//       ゲームのルールはここに書かない。
// 実装は platform.cpp。依存は SDL 系ライブラリのみで、Android でも
// SDL が窓口になる。createWindow で画像・音声の下地も初期化する。
// ============================================================================

#include <functional>
#include <string>
#include <vector>

class Game;
struct Track;  // コース実体 (track.hpp)。loadTrack の引数用

namespace platform {

// コースファイルを探して読み込む (候補パスを順に試す)。
// Game の TrackLoader 差し込み口の実装で、セレクト画面での切替にも使う。
bool loadTrack(const std::string& stem, Track& track);

// テキストファイルを丸ごと読む。Android では APK 内アセットもこれで読める
// （裏側が SDL_RWops のため）。失敗したら false。
bool readTextFile(const std::string& path, std::string& out);

// 設定の保存と読み込み。保存先は環境ごとに platform が決める:
// ネイティブ/Android = OS のアプリ用フォルダ、ブラウザ = localStorage。
bool saveSetting(const std::string& key, const std::string& value);
bool loadSetting(const std::string& key, std::string& out);

// URL クエリパラメータ（ブラウザ版のみ。それ以外は空文字）。
std::string urlQueryParam(const char* key);

// ページ側デバッグ欄への送信 (ブラウザのみ実働)。Game の debugSink に
// 差し込まれる実装。
void sendDebugText(const char* text);

// フルスクリーン切替。入力アダプタ (F キー) から呼ばれる。
void toggleFullscreen();

// ウィンドウと GL コンテキストを作り、描画実装 (IRenderer) を生成して
// Game へ差し込む。入力アダプタへのウィンドウ引き渡しもここで行う。
bool createWindow(Game& app);
void destroyWindow(Game& app);

// メインループを駆動する（ネイティブは while、ブラウザは
// emscripten_set_main_loop）。Game の running が false になったら戻る。
void runMainLoop(Game& app);

// フレームレート上限 (60fps) の有効/無効。ベンチマークでは切る。
void setFrameCapEnabled(bool enabled);
// ベンチ用: beforeFrame(i) を呼んでから 1 フレーム回す、を frames 回。
// 各フレームの所要時間 (ms) を返す。
std::vector<double> runFrames(Game& app, int frames, const std::function<void(int)>& beforeFrame);

}  // namespace platform
