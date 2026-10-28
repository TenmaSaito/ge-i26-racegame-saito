#pragma once
// ============================================================================
// hud.hpp ― 画面表示 (HUD) 層の宣言
// 役割: 速度・ラップ・ミニマップ・デバッグ・チューニング画面など、
//       2D 表示の合成だけを担当する。描画そのものは Renderer に任せる。
// ============================================================================

struct Game;

void formatTime(char* buf, int size, float seconds);
void drawHud(Game& app);
void drawMinimap(Game& app);
void drawDriftDebug(Game& app);
void drawTuning(Game& app);
#ifdef __EMSCRIPTEN__
void pushDebugToPage(Game& app);
#endif
