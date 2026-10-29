#pragma once
// ============================================================================
// input.hpp ― 入力の「ポート」(アプリケーション層が外側へ求める入力の宣言)
// 役割: キーボード / ゲームパッド / マウス / ブラウザの Gamepad API を読み、
//       1 フレーム分の運転入力 (DriveInput) と UI の意図 (UiActions) に
//       翻訳する窓口を宣言する。実装は input.cpp (外側の層、SDL を使用)。
// 方針: このヘッダ自体は SDL を知らない。内側 (game) は「意図」だけを受け
//       取り、キー割り当ての解釈はすべて外側 (input.cpp) が担う。
// ============================================================================

#include "car.hpp"  // DriveInput（ロジック層の型）

#include <string>

struct SDL_Window;  // 実物は外側だけが知る（前方宣言のみ）

// パッドのトリガ操作（押した瞬間だけ立つ）。
struct PadPulse {
    bool camera = false;
    bool reset = false;
    bool pause = false;
};

// SDL イベントを翻訳した「UI の意図」。game はこれを見て状態を変える。
struct UiActions {
    bool quit = false;          // 終了要求 (ウィンドウを閉じる / Esc)
    bool reset = false;         // R キー / パッド BACK
    bool pauseToggle = false;   // P キー / パッド START
    bool cameraNext = false;    // C キー (4 段階循環)
    bool cameraNext3 = false;   // パッド Y (3 段階循環、従来仕様)
    bool helpToggle = false;    // H キー
    bool debugToggle = false;   // B キー
    bool tuningToggle = false;  // T キー
    int carSpec = -1;           // 1/2/3 キー (0/1/2、無ければ -1)
    int tuneSelect = 0;         // 調整パネル: 項目移動 (-1 / +1)
    float tuneAdjust = 0.0f;    // 調整パネル: 値の増減方向×倍率 (Shift で 5 倍)
};

namespace input {

// --- 外側の配線 (platform が呼ぶ) ---
// ウィンドウを引き渡して入力の受け付けを開始する (パッドもここで開く)。
void attachWindow(SDL_Window* window);
// 入力の受け付けを終える (パッドを閉じる)。
void detach();

// --- 内側が毎フレーム使う口 ---
// SDL イベントをすべて消化し、UI の意図に翻訳して返す。
// フルスクリーン切替 (F キー) は表示系の操作なので、この層がそのまま実行する。
// tuningOpen の間は矢印キーを調整の意図として返す。
UiActions pollUi(bool tuningOpen);
// 現在の物理入力状態から運転入力を組み立てる (パッドのトリガは pulse へ)。
DriveInput sampleDrive(PadPulse& pulse, bool carDrifting);

// 接続中のパッド名 (デバッグ表示用)。無ければ "NONE" 始まりの案内文。
// ブラウザ版はボタンを押すまでパッドが見えない仕様なので、その旨も返す。
std::string padStatus();

}  // namespace input
