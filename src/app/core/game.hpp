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
#include "audio.hpp"    // IAudio (音声ポート)
#include "enginesim.hpp"  // EngineSim (エンジン回転数・ギア)
#include "ghost.hpp"    // Ghost (前回の自分のゴースト)
#include "racerecord.hpp"     // RecordBook (走行記録と学習)
#include "ranking.hpp"  // Ranking (ローカルランキング)
#include "recordservice.hpp"  // IRecordService (記録のオンライン共有ポート)
#include "rival.hpp"          // Rival / Standing (対戦相手と順位)
#include "scene.hpp"    // IScene / SceneId
#include "scenery.hpp"  // SceneryBuilder (地形の高さ参照用)
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
    // ベンチ用: 景観 (地形とチャンク) の描画を止める (負荷の切り分け)。
    void setSceneryEnabled(bool on) { sceneryEnabled_ = on; }
    Ghost& ghost() { return ghost_; }         // ゴースト (記録と再生)
    // 音声実装の差し込み (platform が生成して渡す)。無ければ無音。
    void installAudio(IAudio* audio) { audio_ = audio; }
    // 揺れ切り分け用の診断フラグ (7/9 キーで切替、既定は全部 ON = 通常動作)。
    bool diagTilt() const { return diagTilt_; }
    void setDiagTilt(bool on) { diagTilt_ = on; }
    bool diagMirror() const { return diagMirror_; }
    void setDiagMirror(bool on) { diagMirror_ = on; }
    // --- 音 (実装が差し込まれていなければ何もしない) ---
    // エンジン音・タイヤ音の状態を音声実装へ伝える (シーンから毎フレーム)。
    void setEngineSound(const EngineSound& state) {
        if (audio_) audio_->setEngine(state);
    }
    void setTireSound(const TireSound& state) {
        if (audio_) audio_->setTires(state);
    }
    // エンジン音とタイヤ音を止める (フェードアウト)。レース外のシーンで使う。
    void silenceAudio() {
        setEngineSound(EngineSound{});
        setTireSound(TireSound{});
    }
    // 単発の効果音 (カウントダウンの合図など)。
    void playCue(SoundCue cue) {
        if (audio_) audio_->playCue(cue);
    }
    // ブラウザの自動再生制限で、まだ音が出せないか (画面で案内するため)。
    bool audioWaitingForGesture() const { return audio_ && audio_->waitingForUserGesture(); }
    // 音声の診断文字列 (デバッグ表示用)。
    std::string audioDiag() const { return audio_ ? audio_->diag() : std::string("AUDIO NONE"); }
    // エンジン回転数のモデル (音の音程と HUD のギア表示が共有する)。
    EngineSim& engine() { return engine_; }
    const EngineSim& engine() const { return engine_; }

    // --- 対戦相手 (AI) ---
    // 出走させる対戦相手の数 (0 = ソロ / 1〜kMaxRivals) と強さ。選択画面が設定する。
    static constexpr int kMaxRivals = 3;
    int rivalCount() const { return rivalCount_; }
    void setRivalCount(int n) { rivalCount_ = n < 0 ? 0 : (n > kMaxRivals ? kMaxRivals : n); }
    AiLevel aiLevel() const { return aiLevel_; }
    void setAiLevel(AiLevel level) { aiLevel_ = level; }
    std::vector<Rival>& rivals() { return rivals_; }
    const std::vector<Rival>& rivals() const { return rivals_; }
    // 現在の順位表 (前から順)。ソロならプレイヤー 1 行だけ。
    std::vector<Standing> standings() const;
    // プレイヤーの順位 (1 始まり)。
    int playerPosition() const;
    // 対戦相手を描く (レースシーンから)。updateTilt は 1 フレームに 1 回だけ true。
    void drawRivals(bool updateTilt);

    // --- 走行記録・学習・オンライン共有 ---
    RecordBook& records() { return recordBook_; }
    const RecordBook& records() const { return recordBook_; }
    ProfileRecorder& profileRecorder() { return profileRecorder_; }
    // 記録の保存口と、オンライン共有の実装を差し込む (platform が起動時に)。
    void setRecordsSink(std::function<void(const std::string&)> sink) { recordsSink_ = std::move(sink); }
    void setRecordsText(const std::string& text) { recordBook_.deserialize(text); }
    void installRecordService(IRecordService* service) { recordService_ = service; }
    // オンラインの状態表示 ("ONLINE" / "LOCAL ONLY" など)。
    std::string onlineStatus() const { return recordService_ ? recordService_->status() : "LOCAL ONLY"; }
    // 現在のコースの共有記録を取りに行く (選択画面に入ったときなど)。
    void requestSharedRecords();
    // プレイヤー名の取得口 (ページの入力欄・設定ファイルを読む実装を platform が渡す)。
    void setPlayerNameSource(std::function<std::string()> source) { playerNameSource_ = std::move(source); }
    std::string playerName() const;
    // 直近のレース結果 (リザルト画面用)。
    struct RaceOutcome {
        int position = 1;   // 順位 (1 始まり)
        int entrants = 1;   // 出走台数 (自分を含む)
        bool shared = false;  // 記録をオンラインへ送ったか
    };
    const RaceOutcome& lastOutcome() const { return lastOutcome_; }

    // --- 自動運転 (動作確認・デモ用) ---
    // オンにすると、プレイヤーの車も AI (HARD) が運転する。
    // ブラウザは URL の ?autopilot=1、ネイティブは --autopilot で有効になる。
    bool autopilot() const { return autopilot_; }
    void setAutopilot(bool on) { autopilot_ = on; }
    // 自動運転の操作 (1 物理ステップ分)。
    DriveInput autopilotInput(float dt);
    const Ghost& ghost() const { return ghost_; }
    // ゴーストを半透明で描く (計測中で保存済みがあるときだけ実働)。
    void drawGhost();
    // オブジェクトの一括管理 (景観インスタンスと Player が登録される)。
    ObjectManager& objects() { return objects_; }
    const ObjectManager& objects() const { return objects_; }
    // レースを初期状態に戻す (車・計時・演出・カメラをまとめて)。
    void resetRace();
    // 地形 (背景メッシュ) の高さ。タイトルカメラが斜面に埋まらないために使う。
    float terrainHeightAt(float x, float z) const { return sceneryBuilder_.terrainHeightAt(x, z); }
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
    // バックミラー: 画面上部の子画面に後方カメラの映像を描く (レース用)。
    void drawMirror(Vec3 carPos, float carYaw);

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
    // 車の見た目を現在の車種で作り直す (色・形の差し替え)。
    void rebuildCarMeshes();

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
    bool diagTilt_ = true;     // 診断: 車体の傾きを適用するか
    bool diagMirror_ = true;   // 診断: バックミラーを描くか
    IAudio* audio_ = nullptr;  // 音声実装 (所有は platform)
    std::function<void(const std::string&)> rankingSink_;  // ランキング保存口
    Track track_;        // コースデータ
    SceneryBuilder sceneryBuilder_{track_};  // 地形の高さを引くために保持する
    Car car_;            // 自車
    Race race_;          // 周回・計時
    EngineSim engine_;   // エンジン回転数とギア (音と HUD 用)

    // --- 対戦相手と記録 ---
    std::vector<Rival> rivals_;           // 対戦相手 (resetRace で並べ直す)
    int rivalCount_ = 3;                  // 出走させる対戦相手の数 (0 = ソロ)
    AiLevel aiLevel_ = AiLevel::Normal;   // 対戦相手の強さ
    uint32_t raceSeed_ = 1;               // レースごとに変える乱数の種 (名前・ぶれ)
    RecordBook recordBook_;               // 走行記録と学習結果
    ProfileRecorder profileRecorder_;     // プレイヤーの区間速度の記録
    RaceOutcome lastOutcome_;             // 直近のレース結果
    IRecordService* recordService_ = nullptr;  // オンライン共有 (所有は platform)
    std::function<void(const std::string&)> recordsSink_;  // 記録の保存口
    std::function<std::string()> playerNameSource_;        // プレイヤー名の取得口
    GpuMesh rivalBody_[kMaxRivals];       // 対戦相手のボディ (色違い)
    GpuMesh rivalWheel_[kMaxRivals];      // 対戦相手のホイール (ハブの色違い)
    bool autopilot_ = false;              // プレイヤーの車を AI が運転するか
    AiDriver autopilotDriver_;            // そのときの運転手

    // 対戦相手をスタートグリッドに並べ、運転の準備 (速度計画) をする。
    void setupRivals();
    // 記録を保存口へ流す。
    void saveRecords();
    // オンラインから届いた記録を取り込む (毎フレーム)。
    void pollSharedRecords();
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
    bool sceneryEnabled_ = true;  // 景観描画の有効/無効 (ベンチ用)
    float cullRadius_ = 900.0f;   // 景観カリング距離 (ミラー描画時は絞る)
    bool drawBackdrop_ = true;    // 地形を描くか (ミラー描画時は省く)
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
    float lastDt_ = 1.0f / 60.0f;  // 直近フレームの dt (傾き平滑化用)
    int frameNo_ = 0;              // フレーム番号 (二重更新の防止用)
    int tiltFrame_ = -1;           // 傾きを最後に更新したフレーム番号
    float bodyPitchSm_ = 0;       // 車体の前後傾き (平滑化済み)
    float bodyRollSm_ = 0;        // 車体の左右傾き (平滑化済み)
};
