#pragma once
// ============================================================================
// game.hpp ― ゲーム全体の状態 (Game) と進行の入口 (アプリケーション層)
// 役割: アプリの状態を 1 か所に集約し、1 フレームの進行 (gameFrame) と
//       初期化の宣言を持つ。
// レイヤー方針: この層は SDL / OpenGL を直接知らない。描画は render.hpp の
//       ポート (IRenderer) 経由、入力は input.hpp のポート経由。
//       ウィンドウや GL の実物は外側 (platform) が所有する。
//       外側の能力が要るときは、下の「ポート (差し込み口)」に外側が
//       実装を差し込む (依存性逆転)。内側から外側への直接依存は作らない。
// ============================================================================

#include "camera.hpp"   // Orbit（カメラ状態）
#include "car.hpp"      // Car, DriveInput
#include "effects.hpp"  // SmokePuff, TrailPoint
#include "render.hpp"   // IRenderer (ポート), GpuMesh
#include "track.hpp"    // Track

#include <functional>
#include <string>
#include <vector>

// レースの周回管理（ラップ・ベスト・計測状態）。
struct Race {
    int completed = 0;
    bool timing = false;
    float lap = 0;
    float best = -1;
    float prevS = -1;
};

// ゲーム全体の状態。所有は main が持ち、各モジュールは Game& を受け取って働く。
struct Game {
    std::string title = "Suzuka Circuit";  // ウィンドウタイトル（コース名）

    // --- ポート (外側が実装を差し込む口) ---
    IRenderer* renderer = nullptr;  // 描画実装 (platform::createWindow が生成)
    // ブラウザのページ側デバッグ欄へ文字列を渡す口 (ネイティブでは未設定)。
    std::function<void(const char*)> debugSink;
    // チューニング値の保存口 (引数は serializeTuning() の文字列)。
    std::function<void(const std::string&)> saveTuningText;
    Track track;
    Car car;
    GpuMesh body;
    GpuMesh wheel;
    GpuMesh shadow;
    GpuMesh backdrop;
    GpuMesh nose;
    float mmMinX = 0, mmMaxX = 0, mmMinZ = 0, mmMaxZ = 0;  // ミニマップ範囲のキャッシュ
    bool mmReady = false;
    GpuMesh lampsOff;
    GpuMesh lampsOn;
    GpuMesh lampsBright;
    std::vector<GpuMesh> scenery;      // 景観チャンク（距離カリングして描く）
    std::vector<Vec3> sceneryCenters;
    int carSpec = 0;                   // 選択中の車種（1/2/3 キー）
    float bodyRollSm = 0;              // 車体の傾き（平滑化済み）
    float fpsSm = 60.0f;               // FPS 表示（平滑化済み）
    // 描画用の補間ポーズ。物理は固定 60Hz で進むため、そのまま描くと
    // 表示タイミングとのずれで前後にガクつく。1 ステップ前と現在を
    // アキュムレータの端数で補間してから描く。
    float prevX = 0, prevY = 0, prevZ = 0, prevYaw = 0, prevSlip = 0;
    float viewX = 0, viewY = 0, viewZ = 0, viewYaw = 0, viewSlip = 0;
    bool viewReady = false;
    float bodyPitchSm = 0;
    std::vector<TrailPoint> trailL, trailR;  // ドリフト中のテールランプ軌跡
    Race race;
    Orbit camera;
    std::vector<SmokePuff> smoke;
    float smokeAcc = 0;
    unsigned smokeSeed = 12345;
    bool running = true;
    bool paused = false;
    bool showHelp = true;
    bool showDriftDebug = true;  // ドリフトのデバッグ表示（B キーで切替、既定はオン）
    DriveInput lastInput;        // デバッグ表示用に直近の入力を覚えておく
    bool showTuning = false;     // ドリフトのチューニングパネル（T キーで開閉）
    int tuneIndex = 0;           // パネルで選択中の項目
    double accumulator = 0;
};

// 初期化（メッシュ構築と GPU へのアップロード、車両リセット）。
// ウィンドウ生成と renderer の差し込みは platform::createWindow が先に済ませる。
void gameInit(Game& app);
// 1 フレームの進行（入力取得→物理→描画）。platform のループから呼ばれ、
// 経過時間 dtSeconds は外側が測って渡す（この層は時計を持たない）。
void gameFrame(Game& app, double dtSeconds);
// 終了処理は持たない: ウィンドウ・GL・入力の破棄はすべて外側
// (platform::destroyWindow) の担当。

// レース状態のリセットと周回判定。game.cpp 内の小物。
void resetRace(Game& app);
void tickRace(Race& race, float s, float length, float dt);
