#pragma once
// ============================================================================
// input.hpp ― 入力層の宣言
// 役割: キーボード / ゲームパッド / マウス / ブラウザの Gamepad API を読み、
//       1 フレーム分の運転入力 (DriveInput) へ変換する窓口。
// 依存: SDL2。変換後の DriveInput はロジック層 (car.hpp) の型なので、
//       物理側は SDL を知らなくてよい。
// ============================================================================

#include <SDL.h>

struct Game;
struct DriveInput;

// パッドのボタンが「押された瞬間」だけ立つフラグ（フレーム毎に作り直す）。
struct PadPulse {
    bool reset = false;
    bool pause = false;
    bool camera = false;
};

// 接続中のパッドを開く / 閉じる。
void openPad(Game& app);
void closePad(Game& app);

// 現在の入力状態を DriveInput に変換する。
// carDrifting はマウス式ドリフト補助のアンカー判定に使う（前フレームの状態）。
DriveInput readDrive(SDL_Window* window, SDL_GameController* pad, PadPulse& pulse,
                     bool carDrifting);
