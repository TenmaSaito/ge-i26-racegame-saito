#pragma once
// ============================================================================
// game.hpp ― ゲームクラス (Game) の宣言 (アプリケーション層)
// 役割: ゲーム全体の状態と進行 (init / frame) を 1 つのクラスに閉じる。
//       状態はすべて private に持ち、外からはメソッドと参照アクセサでのみ
//       触れる。初期化や 1 フレームの段取りはクラスの内側の仕事であり、
//       呼び出し側 (main / platform) は手順を知らなくてよい。
// レイヤー方針: この層は SDL / OpenGL を直接知らない。描画は render.hpp の
//       ポート (IRenderer)、入力は input.hpp のポート経由。外側の能力が
//       要るときは、下の「差し込み口」に外側が実装を登録する (依存性逆転)。
// ============================================================================

#include "camera.hpp"   // Orbit (カメラ)
#include "car.hpp"      // Car, DriveInput
#include "effects.hpp"  // Effects (煙・光跡・タイヤ痕)
#include "race.hpp"     // Race (周回・計時)
#include "render.hpp"   // IRenderer (ポート), GpuMesh
#include "object.hpp"   // ObjectManager (オブジェクト一括管理)
#include "ghost.hpp"    // Ghost (前回の自分のゴースト)
#include "ranking.hpp"  // Ranking (ローカルランキング)

#include <functional>
#include "scene.hpp"    // IScene / SceneId
#include "track.hpp"    // Track

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Hud;     // HUD 描画器 (hud.hpp)。Game が 1 つ保持する
class IScene;  // 現在のシーン (scene.hpp の基底)

class Game {
public:
    // title はウィンドウタイトル (コース名)、track は読み込み済みコース。
    // initialStem は最初に読み込むコース (例: "suzuka")。
    // コース実体の読み込みは setTrackLoader で差し込まれた外側の仕事。
    explicit Game(std::string initialStem);
    ~Game();

    // --- 外側からの差し込み口 (platform が起動時に登録する) ----------------
    // 描画実装を差し込む (所有は platform 側。Game は借りるだけ)。
    void installRenderer(IRenderer* renderer) { renderer_ = renderer; }
    IRenderer* renderer() const { return renderer_; }
    // ブラウザのページ側デバッグ欄への送信口 (ネイティブでは未登録でよい)。
    void setDebugSink(std::function<void(const char*)> sink) { debugSink_ = std::move(sink); }
    // チューニング保存の実行口 (引数は serializeTuning() の文字列)。
    void setSaveTuningSink(std::function<void(const std::string&)> sink) { saveSink_ = std::move(sink); }
    // デバッグ文字列を差し込み先へ送る (未登録なら何もしない)。
    void sendDebug(const char* text) const {
        if (debugSink_) debugSink_(text);
    }
    bool hasDebugSink() const { return static_cast<bool>(debugSink_); }

    // --- 進行 --------------------------------------------------------------
    const std::string& title() const { return title_; }
    // 初期化: メッシュ構築と GPU アップロード、車両リセット。
    // renderer の差し込みが済んでいる前提で呼ぶ。最後にタイトルシーンへ入る。
    void init();
    // 1 フレームの進行。実処理は現在のシーン (IScene) が行い、Game は
    // シーン切り替えの予約処理だけをして委譲する。
    // 経過時間は外側 (platform のループ) が測って渡す。
    void frame(double dtSeconds);
    bool running() const { return running_; }  // 終了要求が出たら false
    void requestQuit() { running_ = false; }   // シーンからの終了要求
    // シーン切り替えの予約 (実際の切り替えは次の frame の頭で安全に行う)。
    void requestScene(SceneId id) {
        pendingScene_ = id;
        hasPendingScene_ = true;
    }

    // コース読み込みの差し込み口 (platform が実装を渡す)。
    using TrackLoader = std::function<bool(const std::string& stem, Track& out)>;
    void setTrackLoader(TrackLoader loader) { trackLoader_ = std::move(loader); }
    // コースを読み直す (路面・景観・オブジェクトを作り直し、車を置き直す)。
    // セレクト画面からの切り替えと、起動時の初期読み込みの両方で使う。
    bool loadCourse(const std::string& stem);
    const std::string& courseStem() const { return courseStem_; }
    // ランキング保存の差し込み口と、起動時の復元。
    void setRankingSink(std::function<void(const std::string&)> sink) { rankingSink_ = std::move(sink); }
    void setRankingText(const std::string& text) { ranking_.deserialize(text); }
    Ranking& ranking() { return ranking_; }
    const Ranking& ranking() const { return ranking_; }
    // ゴール結果をランキングへ登録して保存する。順位 (0 始まり) か圏外 -1。
    int recordResult();

    // --- シーンから使う共通サービス -----------------------------------------
    Hud& hud() { return *hud_; }              // HUD 描画器
    Effects& effects() { return effects_; }   // 演出 (煙・光跡・タイヤ痕)
    Ghost& ghost() { return ghost_; }         // ゴースト (記録と再生)
    const Ghost& ghost() const { return ghost_; }
    // ゴーストを半透明で描く (計測中で保存済みがあるときだけ実働)。
    void drawGhost();
    // オブジェクトの一括管理 (景観インスタンスと Player が登録される)。
    ObjectManager& objects() { return objects_; }
    const ObjectManager& objects() const { return objects_; }
    // レースを初期状態に戻す (車・計時・演出・カメラをまとめて)。
    void resetRace();
    // 現在のチューニング値を差し込み口経由で保存する (シーンからの依頼用)。
    void saveTuningNow();
    // FPS 表示の平滑化 (シーンが毎フレーム知らせる)。
    void noteFps(double elapsed);
    // ワールド描画の開始 (画面消去・空・投影と視点の設定)。
    void beginWorldDraw();
    // ワールド本体の描画 (背景・景観・タイヤ痕・影・車体)。
    // carPos/carYaw は描画用の車ポーズ (レースでは補間ポーズを渡す)。
    void drawWorld(Vec3 carPos, float carYaw);
    // 煙と光跡の描画 (ワールド描画の後、HUD の前)。
    void drawEffects();

    // --- セッション状態の設定 (シーンが操作する表示・進行フラグ) -------------
    void setPaused(bool v) { paused_ = v; }
    void setShowHelp(bool v) { showHelp_ = v; }
    void setShowDriftDebug(bool v) { showDriftDebug_ = v; }
    void setShowTuning(bool v) { showTuning_ = v; }
    void setTuneIndex(int v) { tuneIndex_ = v; }
    void setLastInput(const DriveInput& in) { lastInput_ = in; }

    // --- 協働モジュール向けの参照 -------------------------------------------
    Car& car() { return car_; }
    const Car& car() const { return car_; }
    const Track& track() const { return track_; }
    Race& race() { return race_; }
    const Race& race() const { return race_; }
    Orbit& camera() { return camera_; }
    const Orbit& camera() const { return camera_; }
    const DriveInput& lastInput() const { return lastInput_; }  // 直近の運転入力 (表示用)
    bool paused() const { return paused_; }
    bool showHelp() const { return showHelp_; }
    bool showDriftDebug() const { return showDriftDebug_; }
    bool showTuning() const { return showTuning_; }
    int tuneIndex() const { return tuneIndex_; }
    int carSpec() const { return carSpec_; }
    float fps() const { return fpsSm_; }

    // 車種を切り替える (性能倍率＋ドリフト調整値の差し替えと保存)。
    void applyCarSpec(int index);

private:
    // シーンを実際に切り替える (exit → 生成 → enter)。
    void switchScene(SceneId id);

    // --- 基本情報 ---
    std::string title_;        // コース表示名 (loadCourse で更新される)
    std::string courseStem_;   // 現在のコース stem
    std::string initialStem_;  // 起動時に読み込むコース stem
    TrackLoader trackLoader_;  // コース読み込みの差し込み口
    Ranking ranking_;          // ローカルランキング
    Ghost ghost_;              // ゴースト (最速ラップの記録と再生)
    std::function<void(const std::string&)> rankingSink_;  // ランキング保存口
    Track track_;        // コースデータ
    Car car_;            // 自車
    Race race_;          // 周回・計時
    Orbit camera_;       // カメラ状態
    Effects effects_;    // 煙・光跡・タイヤ痕
    ObjectManager objects_;         // 全オブジェクトの一括管理 (景観・Player)
    std::unique_ptr<Hud> hud_;      // HUD 描画器 (ミニマップ等の表示状態を持つ)
    std::unique_ptr<IScene> scene_;  // 現在のシーン (タイトル／レース／リザルト)
    SceneId pendingScene_ = SceneId::Title;  // 切り替え予約
    bool hasPendingScene_ = false;           // 予約があるか

    // --- ポートと差し込み (実物は外側が持つ) ---
    IRenderer* renderer_ = nullptr;  // 描画実装 (借用)
    std::function<void(const char*)> debugSink_;              // ページ側デバッグ送信
    std::function<void(const std::string&)> saveSink_;        // チューニング保存

    // --- GPU メッシュ (renderer_ にアップロード済みの形状) ---
    GpuMesh body_;        // 車体
    GpuMesh wheel_;       // ホイール
    GpuMesh shadow_;      // 車の影 (平面クワッド)
    GpuMesh backdrop_;    // 背景 (地面＋山並み)
    GpuMesh nose_;        // ドライバーズアイ用の鼻先
    GpuMesh lampsOff_;    // テールランプ (消灯)
    GpuMesh lampsOn_;     // テールランプ (ドリフト中)
    GpuMesh lampsBright_; // テールランプ (ブレーキ)
    std::vector<GpuMesh> scenery_;        // 景観チャンク (距離カリングして描く)
    std::vector<Vec3> sceneryCenters_;    // 各チャンクの中心 (カリング用)

    // --- 進行状態 ---
    bool running_ = true;         // メインループの継続フラグ
    bool paused_ = false;         // ポーズ中か (P キー)
    bool showHelp_ = true;        // キー説明の表示 (H キー、ネイティブのみ)
    bool showDriftDebug_ = true;  // ドリフトデバッグ表示 (B キー)
    bool showTuning_ = false;     // チューニングパネル表示 (T キー)
    int tuneIndex_ = 0;           // パネルで選択中の項目
    int carSpec_ = 0;             // 選択中の車種 (1/2/3 キー)
    DriveInput lastInput_;        // 直近の運転入力 (デバッグ表示用に保持)
    float fpsSm_ = 60.0f;         // FPS 表示 (平滑化済み)

    // 車体の傾き (drawWorld が路面平面から求めて平滑化する)。
    // 補間ポーズや物理のアキュムレータは RaceScene 側の状態になった。
    float bodyPitchSm_ = 0;       // 車体の前後傾き (平滑化済み)
    float bodyRollSm_ = 0;        // 車体の左右傾き (平滑化済み)
};
