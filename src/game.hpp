#pragma once
// ============================================================================
// game.hpp ― ゲーム全体の状態 (Game) と進行の入口
// 役割: アプリの状態を 1 か所に集約し、1 フレームの進行 (gameFrame) と
//       初期化・終了の宣言を持つ。旧 App 構造体の後継。
// 依存: 描画 (render.hpp)、車両・コース等のロジック層。SDL の型は
//       ウィンドウ等のポインタ保持のためだけに使う。
// ============================================================================

#include "camera.hpp"   // Orbit（カメラ状態）
#include "car.hpp"      // Car, DriveInput
#include "effects.hpp"  // SmokePuff, TrailPoint
#include "render.hpp"   // Renderer, GpuMesh
#include "track.hpp"    // Track

#include <SDL.h>

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
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    SDL_GameController* pad = nullptr;
    Renderer renderer;
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
    Uint64 prevCounter = 0;
};

// 初期化（メッシュ構築と GPU へのアップロード、車両リセット）。
// ウィンドウ生成は platform::createWindow が先に済ませている前提。
void gameInit(Game& app);
// 1 フレームの進行（イベント処理→物理→描画）。platform のループから呼ばれる。
void gameFrame(Game& app);
// 終了処理（パッド・GL・ウィンドウの破棄は platform 側と分担）。
void gameShutdown(Game& app);

// レース状態のリセットと周回判定。game.cpp 内の小物。
void resetRace(Game& app);
void tickRace(Race& race, float s, float length, float dt);
