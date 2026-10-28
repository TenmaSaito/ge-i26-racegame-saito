#pragma once

#include "meshbuild.hpp"
#include "track.hpp"

struct DriveInput {
    float throttle = 0;    // 0..1
    float brake = 0;       // 0..1  停止中は後退
    float steer = 0;       // -1..1  左が負、右が正
    // ドリフト用（Inertial Drift のツインスティック操作を取り入れたもの）
    float driftSteer = 0;  // -1..1  右スティック左右。ドリフトのきっかけと目標スリップ角
    bool drift = false;    // ドリフトボタン（ハンドブレーキ相当）。押しながら切ると滑る
};

// ドリフトの調整値。ゲーム中のチューニングパネル（T キー）から実行時に変更できる。
// 値は保存され、次回起動時に復元される（ネイティブ: drift_tuning.txt、ブラウザ: localStorage）。
struct DriftTuning {
    float gripNormal = 15.0f;  // 通常時の横グリップ（横速度の減衰率、1/s）
    float gripDrift = 1.15f;   // ドリフト時の横グリップ。小さいほど滑る
    float inertia = 1.0f;      // 慣性の強さ。大きいほど速度が向きを保ち、横滑りが長引く
    float driftDecel = 0.15f;  // ドリフト中の減速率（1/s）。大きいほどドリフトで失速する
    float maxSlip = 0.95f;     // 目標スリップ角の上限（ラジアン、約54度）
    float minSpeed = 9.0f;     // ドリフトに入れる最低速度（m/s、約32km/h）
};

extern DriftTuning driftTuning;

struct Car {
    float x = 0;
    float y = 0;
    float z = 0;
    float yaw = 0;
    float speed = 0;        // m/s。前方向の速度成分。正が前進
    float steerAngle = 0;   // 前輪の切れ角（ラジアン、右が正）
    float wheelSpin = 0;
    float brakeHold = 0;    // 止まってから後退に入るまでの待ち時間
    bool onAsphalt = true;
    TrackPose ground;

    // --- ドリフトの状態 -------------------------------------------------
    // 速度はスカラーではなくベクトルで持つ。車体が回っても速度ベクトルは
    // ワールドに残る（慣性）ので、進行方向と車体の向きがずれて横滑りする。
    float velX = 0;          // 速度ベクトル x（ワールド、東）
    float velZ = 0;          // 速度ベクトル z（ワールド、北）
    float lateralSpeed = 0;  // 車体横方向の速度（m/s、右が正）
    float slipAngle = 0;     // 進行方向と車体の向きの差（ラジアン）
    bool drifting = false;   // ドリフト中か
    float driftAmount = 0;   // 0=フルグリップ、1=フルスライドへの遷移量
    float driftCommand = 0;  // 平滑化したドリフト操作（キーボードのオン/オフ入力でも角度を保てるように）
    float driftCharge = 0;   // フリック入力（大舵+アクセル）の蓄積秒
    float driftScore = 0;    // ドリフトの累計ポイント
    float driftChain = 0;    // いまのドリフトで滑り続けている距離（m）
    float driftBestChain = 0;

    void reset(const Track& track);
    void update(float dt, const DriveInput& input, const Track& track);

    // 速度ベクトルを持って進む。ドリフトはこの関数の中身が本体。
    // グリップ時は横速度を強く殺し、ドリフト時は殺しきれずに滑らせる。
    void stepDrift(float dt, const DriveInput& input);
};

void buildCarBody(CpuMesh& mesh);
void buildCarWheel(CpuMesh& mesh);
void buildCarShadow(CpuMesh& mesh);
Mat4 carModelMatrix(const Car& car, float pitch, float roll);
