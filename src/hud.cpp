// ============================================================================
// hud.cpp ― 画面表示 (HUD) 層
// 役割: 速度・ラップ・ミニマップ・デバッグ・チューニング画面の合成。
//       値の計算はせず、Game の状態を Renderer の 2D 描画へ写すだけ。
//       ブラウザ版のデバッグ表示はページへ送り、画面内には描かない。
// ============================================================================

#include "hud.hpp"

#include "camera.hpp"
#include "car.hpp"
#include "game.hpp"
#include "input.hpp"  // input::padStatus (パッド認識状態)
#include "scene.hpp"   // kRaceLaps
#include "render.hpp"
#include "tuning.hpp"

#ifdef __EMSCRIPTEN__
#endif

#include <cmath>
#include <cstdio>
#include <cstring>

void formatTime(char* buf, int size, float seconds) {
    if (seconds < 0) {
        std::snprintf(buf, size, "--:--.---");
        return;
    }
    int minutes = static_cast<int>(seconds / 60.0f);
    float rest = seconds - float(minutes) * 60.0f;
    std::snprintf(buf, size, "%d:%06.3f", minutes, rest);
}


void Hud::drawMinimap(Game& app) {
    float size = 190.0f;
    float x0 = 24.0f;
    float y0 = float(app.renderer()->height()) - size - 28.0f;
    app.renderer()->addQuad(x0, y0, x0 + size, y0 + size, 0.02f, 0.03f, 0.05f, 0.78f);
    // 範囲はコース固定なので初回だけ計算して使い回す（毎フレーム全数走査しない）。
    if (!mmReady_) {
        mmMinX_ = mmMaxX_ = app.track().samples[0].x;
        mmMinZ_ = mmMaxZ_ = app.track().samples[0].z;
        for (const TrackSample& sample : app.track().samples) {
            mmMinX_ = std::min(mmMinX_, sample.x);
            mmMaxX_ = std::max(mmMaxX_, sample.x);
            mmMinZ_ = std::min(mmMinZ_, sample.z);
            mmMaxZ_ = std::max(mmMaxZ_, sample.z);
        }
        mmReady_ = true;
    }
    float minX = mmMinX_;
    float maxX = mmMaxX_;
    float minZ = mmMinZ_;
    float maxZ = mmMaxZ_;
    float midX = (minX + maxX) * 0.5f;
    float midZ = (minZ + maxZ) * 0.5f;
    float span = std::max(maxX - minX, maxZ - minZ) * 1.12f;
    auto mapX = [&](float x) { return x0 + size * 0.5f + (x - midX) / span * (size - 18.0f); };
    auto mapY = [&](float z) { return y0 + size * 0.5f - (z - midZ) / span * (size - 18.0f); };
    const int n = static_cast<int>(app.track().samples.size());
    // 190px のミニマップに 2000 本の線は不要。4 サンプルに 1 本で見た目は同じ。
    for (int i = 0; i < n; i += 4) {
        const TrackSample& a = app.track().samples[i];
        const TrackSample& b = app.track().samples[(i + 4) % n];
        float r = 0.75f, g = 0.78f, bl = 0.82f;
        if (a.bridge || b.bridge) {
            r = 0.95f;
            g = 0.72f;
            bl = 0.28f;
        }
        app.renderer()->drawLine(mapX(a.x), mapY(a.z), mapX(b.x), mapY(b.z), 2.0f, r, g, bl, 1);
    }
    float mx = mapX(app.car().x);
    float my = mapY(app.car().z);
    Vec3 nose{std::sin(app.car().yaw), 0, std::cos(app.car().yaw)};
    Vec3 side{std::cos(app.car().yaw), 0, -std::sin(app.car().yaw)};
    float nx = mx + nose.x * 9.0f;
    float ny = my - nose.z * 9.0f;
    float lx = mx - nose.x * 5.0f + side.x * 5.0f;
    float ly = my + nose.z * 5.0f - side.z * 5.0f;
    float rx = mx - nose.x * 5.0f - side.x * 5.0f;
    float ry = my + nose.z * 5.0f + side.z * 5.0f;
    app.renderer()->drawLine(lx, ly, nx, ny, 2.0f, 0.9f, 0.2f, 0.18f, 1);
    app.renderer()->drawLine(rx, ry, nx, ny, 2.0f, 0.9f, 0.2f, 0.18f, 1);
    app.renderer()->drawLine(lx, ly, rx, ry, 2.0f, 0.9f, 0.2f, 0.18f, 1);
}

#ifdef __EMSCRIPTEN__
// ブラウザ版のデバッグ表示はページ側のパネルに出す（画面をすっきりさせる）。
// ページが window.raceSetDebug を定義していれば、約 12Hz で文字列を送る。
void Hud::sendDebugToPage(Game& app) {
    // ページへの送信実務は Game の差し込み口 (debugSink) の先 = 外側の担当。
    // ここは表示文字列を組み立てるだけ。
    static int counter = 0;
    if ((counter++ % 5) != 0) return;
    if (!app.hasDebugSink()) return;
    const Car& car = app.car();
    const DriveInput& in = app.lastInput();
    const char* why = "";
    bool trying = in.drift || std::fabs(in.driftSteer) > 0.45f;
    if (!car.drifting && trying) {
        if (car.speed < 9.0f) {
            why = "\nWHY: TOO SLOW - NEED 32 KM/H";
        } else if (std::fabs(in.steer) < 0.2f && std::fabs(in.driftSteer) <= 0.45f) {
            why = "\nWHY: NEED STEER OR STICK";
        }
    }
    std::string pad = input::padStatus();  // パッド認識状態 (無ければ NONE...)
    char buf[768];
    std::snprintf(buf, sizeof(buf),
                  "STATE: %s\nSLIP: %.0f DEG\nLAT V: %.1f M/S\nFWD V: %.1f M/S (%.0f KM/H)\n"
                  "AMOUNT: %d %%\nCMD: %.2f\nIN: BTN %d STEER %.2f\nIN: DSTK %.2f MSE %.2f THR %.1f\n"
                  "PAD %s\nSCORE %d CHAIN %d M%s",
                  car.drifting ? "DRIFT" : "GRIP", car.slipAngle * 57.29578f,
                  car.lateralSpeed, car.speed, car.speed * 3.6f,
                  static_cast<int>(std::lround(car.driftAmount * 100.0f)), car.driftCommand,
                  in.drift ? 1 : 0, in.steer, in.driftSteer, in.mouseDrift, in.throttle,
                  pad.c_str(),
                  static_cast<int>(car.driftScore), static_cast<int>(car.driftChain), why);
    app.sendDebug(buf);
}
#endif

// ドリフトのデバッグ表示（B キーで切替）。状態・角度・横速度・入力と、
// ドリフトに入らないときの理由を右上に常時出す。走らなくても切り分けられる。
void Hud::drawDriftDebug(Game& app) {
    IRenderer& hud = *app.renderer();
    const Car& car = app.car();
    const DriveInput& in = app.lastInput();
    const float pw = 336.0f;
    const float lineH = 21.0f;
    const int lines = 12;
    const float ph = 14.0f + lineH * float(lines);
    const float px = float(hud.width()) - pw - 16.0f;
    const float py = 52.0f;
    hud.addQuad(px, py, px + pw, py + ph, 0.02f, 0.03f, 0.05f, 0.72f);
    float tx = px + 12.0f;
    float ty = py + 8.0f;
    char line[96];
    hud.drawText(tx, ty, 1.6f, "DRIFT DEBUG", 0.85f, 0.88f, 0.92f, 1);
    ty += lineH;
    if (car.drifting) {
        hud.drawText(tx, ty, 1.6f, "STATE: DRIFT", 1.0f, 0.62f, 0.2f, 1);
    } else {
        hud.drawText(tx, ty, 1.6f, "STATE: GRIP", 0.65f, 0.7f, 0.75f, 1);
    }
    ty += lineH;
    std::snprintf(line, sizeof(line), "SLIP: %.0f DEG", car.slipAngle * 57.29578f);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "LAT V: %.1f M/S", car.lateralSpeed);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "FWD V: %.1f M/S", car.speed);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "AMOUNT: %d %%", static_cast<int>(std::lround(car.driftAmount * 100.0f)));
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "CMD: %.2f", car.driftCommand);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "IN: BTN %d STEER %.2f", in.drift ? 1 : 0, in.steer);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "IN: DSTK %.2f MSE %.2f THR %.1f",
                  in.driftSteer, in.mouseDrift, in.throttle);
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "SCORE %d CHAIN %d M",
                  static_cast<int>(car.driftScore), static_cast<int>(car.driftChain));
    hud.drawText(tx, ty, 1.6f, line, 1.0f, 0.8f, 0.45f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "PAD: %s", input::padStatus().c_str());
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    // ドリフト操作をしているのに入らないときは、理由を出す。
    const char* why = "";
    bool trying = in.drift || std::fabs(in.driftSteer) > 0.45f;
    if (!car.drifting && trying) {
        if (car.speed < 9.0f) {
            why = "WHY: TOO SLOW - NEED 32 KM/H";
        } else if (std::fabs(in.steer) < 0.2f && std::fabs(in.driftSteer) <= 0.45f) {
            why = "WHY: NEED STEER OR STICK";
        }
    }
    hud.drawText(tx, ty, 1.6f, why, 0.95f, 0.5f, 0.45f, 1);
}

// ドリフトのチューニングパネル（T キー）。値はその場で物理に反映され、自動保存される。
void Hud::drawTuning(Game& app) {
    IRenderer& hud = *app.renderer();
    const float pw = 540.0f;
    const float rowH = 34.0f;
    const float ph = 58.0f + rowH * float(kTuneItemCount) + 44.0f;
    const float px = (float(hud.width()) - pw) * 0.5f;
    const float py = (float(hud.height()) - ph) * 0.5f - 30.0f;
    hud.addQuad(px, py, px + pw, py + ph, 0.02f, 0.03f, 0.05f, 0.85f);
    hud.drawText(px + 20, py + 14, 2.2f, "DRIFT TUNING", 1.0f, 0.62f, 0.2f, 1);
    char line[96];
    for (int i = 0; i < kTuneItemCount; ++i) {
        const TuneItem& item = tuneItems[i];
        float ry = py + 56.0f + rowH * float(i);
        bool selected = i == app.tuneIndex();
        if (selected) {
            hud.addQuad(px + 10, ry - 4, px + pw - 10, ry + 26, 0.95f, 0.5f, 0.15f, 0.35f);
        }
        float r = selected ? 1.0f : 0.85f;
        float g = selected ? 0.75f : 0.88f;
        float b = selected ? 0.4f : 0.92f;
        hud.drawText(px + 20, ry, 1.8f, item.name, r, g, b, 1);
        if (item.display == 1) {
            std::snprintf(line, sizeof(line), "%.0f DEG", *item.value * 57.29578f);
        } else if (item.display == 2) {
            std::snprintf(line, sizeof(line), "%.0f KM/H", *item.value * 3.6f);
        } else if (item.display == 3) {
            std::snprintf(line, sizeof(line), "%.1f", *item.value);
        } else {
            std::snprintf(line, sizeof(line), "%.2f", *item.value);
        }
        hud.drawText(px + 300, ry, 1.8f, line, r, g, b, 1);
        // 値の範囲内での位置を示すバー。
        float frac = (*item.value - item.lo) / (item.hi - item.lo);
        float bx0 = px + 420;
        float bx1 = px + pw - 20;
        hud.addQuad(bx0, ry + 4, bx1, ry + 12, 0.1f, 0.12f, 0.16f, 0.9f);
        hud.addQuad(bx0, ry + 4, bx0 + (bx1 - bx0) * frac, ry + 12, 1.0f, 0.62f, 0.2f, 0.95f);
    }
    float fy = py + 56.0f + rowH * float(kTuneItemCount) + 8.0f;
    hud.drawText(px + 20, fy, 1.5f, "UP/DOWN SELECT  LEFT/RIGHT ADJUST  SHIFT: FAST", 0.75f, 0.8f, 0.84f, 1);
    hud.drawText(px + 20, fy + 20, 1.5f, "T: CLOSE   VALUES APPLY LIVE AND SAVE AUTOMATICALLY", 0.75f, 0.8f, 0.84f, 1);
}

void Hud::draw(Game& app) {
    IRenderer& hud = *app.renderer();
    hud.beginHud();
    char line[96];
    // 速度表示はベクトルの大きさ。ドリフト中は前方向成分だけだと実際より遅く見える。
    float velMag = std::sqrt(app.car().velX * app.car().velX + app.car().velZ * app.car().velZ);
    int kmh = static_cast<int>(std::lround(velMag * 3.6f));
    std::snprintf(line, sizeof(line), "%d KM/H", kmh);
    hud.drawText(24, 22, 4.0f, line, 0.95f, 0.96f, 0.98f, 1);

    std::snprintf(line, sizeof(line), "FPS %d", int(std::lround(app.fps())));
    hud.drawText(float(app.renderer()->width()) - 118.0f, float(app.renderer()->height()) - 56.0f, 2.2f, line, 0.85f, 0.88f, 0.92f, 1);

    if (!app.race().timing()) {
        std::snprintf(line, sizeof(line), "OUT LAP");
    } else {
        std::snprintf(line, sizeof(line), "LAP %d", app.race().completed() + 1);
    }
    hud.drawText(24, 64, 2.2f, line, 0.85f, 0.88f, 0.92f, 1);
    formatTime(line, sizeof(line), app.race().lap());
    hud.drawText(150, 64, 2.2f, line, 0.95f, 0.96f, 0.98f, 1);

    char best[32];
    formatTime(best, sizeof(best), app.race().best());
    std::snprintf(line, sizeof(line), "BEST %s", best);
    hud.drawText(24, 90, 2.0f, line, 0.75f, 0.8f, 0.7f, 1);

    std::snprintf(line, sizeof(line), "CAR %s  [1/2/3]", kCarSpecs[app.carSpec()].name);
    hud.drawText(24, 142, 2.0f, line, 0.8f, 0.85f, 0.9f, 1);

    int percent = static_cast<int>(app.car().ground.s / app.track().length * 100.0f);
    const char* corner = app.track().nameOf(app.car().ground.nameId);
    if (app.car().ground.corner > 0 && corner[0]) {
        std::snprintf(line, sizeof(line), "T%d %s  %d%%", app.car().ground.corner, corner, percent);
    } else if (corner[0]) {
        std::snprintf(line, sizeof(line), "%s  %d%%", corner, percent);
    } else {
        std::snprintf(line, sizeof(line), "%d%%", percent);
    }
    hud.drawText(24, 116, 2.0f, line, 0.9f, 0.9f, 0.92f, 1);

    // ドリフト表示: 走行中でも目に入るよう、画面中央上に大きく出す。
    // 下のバーはスリップ角（中央が 0、左右に最大約60度）。
    if (app.car().drifting) {
        int deg = static_cast<int>(std::lround(std::fabs(app.car().slipAngle) * 57.29578f));
        std::snprintf(line, sizeof(line), "DRIFT %d DEG", deg);
        float scale = 4.0f;
        float textW = float(std::strlen(line)) * 6.0f * scale;
        hud.drawText((float(hud.width()) - textW) * 0.5f, 20, scale, line, 1.0f, 0.62f, 0.2f, 1);
        float cx = float(hud.width()) * 0.5f;
        hud.addQuad(cx - 130, 58, cx + 130, 66, 0.02f, 0.03f, 0.05f, 0.6f);
        float fill = clampf(app.car().slipAngle / 1.05f, -1.0f, 1.0f) * 130.0f;
        float fx0 = fill < 0 ? cx + fill : cx;
        float fx1 = fill < 0 ? cx : cx + fill;
        hud.addQuad(fx0, 58, fx1, 66, 1.0f, 0.62f, 0.2f, 0.95f);
        hud.addQuad(cx - 1.5f, 56, cx + 1.5f, 68, 0.95f, 0.96f, 0.98f, 0.9f);
        std::snprintf(line, sizeof(line), "SCORE %d  CHAIN %d M",
                      static_cast<int>(app.car().driftScore), static_cast<int>(app.car().driftChain));
        float subW = float(std::strlen(line)) * 6.0f * 2.0f;
        hud.drawText((float(hud.width()) - subW) * 0.5f, 76, 2.0f, line, 1.0f, 0.8f, 0.45f, 1);
    } else if (std::fabs(app.car().slipAngle) > 0.15f && app.car().speed > 5.0f) {
        // ドリフト判定ではないが実際に滑っているとき（芝など）。
        int deg = static_cast<int>(std::lround(std::fabs(app.car().slipAngle) * 57.29578f));
        std::snprintf(line, sizeof(line), "SLIDE %d DEG", deg);
        float scale = 3.0f;
        float textW = float(std::strlen(line)) * 6.0f * scale;
        hud.drawText((float(hud.width()) - textW) * 0.5f, 22, scale, line, 0.95f, 0.85f, 0.3f, 1);
    }

    const char* camName = "CHASE";
    if (app.camera().mode == 1) camName = "CLOSE";
    if (app.camera().mode == 2) camName = "FAR";
    if (app.camera().mode == 3) camName = "DRIVER";
    hud.drawText(float(hud.width()) - 140, 22, 2.0f, camName, 0.8f, 0.84f, 0.88f, 1);

#ifndef __EMSCRIPTEN__
    if (app.showHelp()) {
        hud.drawText(24, float(hud.height()) - 102, 1.7f,
                     "W/UP ACCEL  S/DOWN BRAKE  A/D STEER  SPACE/SHIFT/CTRL DRIFT  R RESET  C CAMERA  F FULLSCREEN  P PAUSE  B DEBUG  T TUNE",
                     0.9f, 0.92f, 0.94f, 1);
        hud.drawText(24, float(hud.height()) - 78, 1.7f,
                     "KEYBOARD TWIN-STICK: A/D STEER PLUS ARROW KEYS AS DRIFT STICK",
                     0.75f, 0.8f, 0.84f, 1);
        hud.drawText(24, float(hud.height()) - 54, 1.7f,
                     "PAD: RT ACCEL  LT BRAKE  L-STICK STEER  R-STICK OR B/LB/RB DRIFT  Y CAMERA",
                     0.75f, 0.8f, 0.84f, 1);
        hud.drawText(24, float(hud.height()) - 30, 1.7f,
                     "MOUSE: DURING DRIFT, SLIDE FROM DRIFT-START POS = DRIFT STICK (FARTHER = MORE ANGLE)",
                     0.75f, 0.8f, 0.84f, 1);
    }
#endif
    hud.drawText(float(hud.width()) - 250, float(hud.height()) - 28, 1.6f, "(C) OSM / GSI", 0.85f, 0.88f, 0.9f, 1);
    if (app.paused()) {
        hud.drawText(float(hud.width()) * 0.5f - 70, float(hud.height()) * 0.45f, 5.0f, "PAUSE", 1, 0.95f, 0.9f, 1);
    }
    drawMinimap(app);
#ifdef __EMSCRIPTEN__
    sendDebugToPage(app);
#else
    if (app.showDriftDebug()) {
        drawDriftDebug(app);
    }
#endif
    if (app.showTuning()) {
        drawTuning(app);
    }
    hud.flushHud();
}


// タイトル画面の文字。背景はアトラクトカメラのワールド描画で、
// ここは大会名・コース名・決定の案内・操作説明だけを重ねる。
void Hud::drawTitle(Game& app, float t) {
    IRenderer& hud = *app.renderer();
    hud.beginHud();
    const float W = float(hud.width());
    const float H = float(hud.height());
    // 全体を少し暗くして文字を読みやすくする。
    hud.addQuad(0, 0, W, H, 0.02f, 0.04f, 0.08f, 0.35f);
    auto centerText = [&](float y, float scale, const char* text, float r, float g, float b) {
        float w = float(std::strlen(text)) * 6.0f * scale;
        hud.drawText((W - w) * 0.5f, y, scale, text, r, g, b, 1);
    };
    centerText(H * 0.14f, 2.0f, "DRIFT RACE", 0.85f, 0.88f, 0.92f);
    centerText(H * 0.22f, 5.0f, app.title().c_str(), 1.0f, 0.95f, 0.9f);
    char line[96];
    std::snprintf(line, sizeof(line), "%d LAPS RACE", kRaceLaps);
    centerText(H * 0.40f, 2.2f, line, 0.95f, 0.85f, 0.6f);
    // 決定の案内は点滅させる（目に留まりやすくするため）。
    if (std::sin(t * 3.2f) > -0.3f) {
        centerText(H * 0.52f, 2.6f, "PRESS ENTER / CLICK / PAD START", 1.0f, 1.0f, 1.0f);
    }
    centerText(H * 0.68f, 1.8f, "W/UP ACCEL  S/DOWN BRAKE  A/D STEER  SPACE DRIFT", 0.8f, 0.84f, 0.88f);
    centerText(H * 0.68f + 30.0f, 1.8f, "PAD: L-STICK STEER  R-STICK / B DRIFT  RT/LT PEDALS", 0.8f, 0.84f, 0.88f);
    std::snprintf(line, sizeof(line), "CAR: %s  [1/2/3 IN RACE]", kCarSpecs[app.carSpec()].name);
    centerText(H * 0.68f + 60.0f, 1.8f, line, 0.8f, 0.84f, 0.88f);
    hud.flushHud();
}

// リザルト画面の文字。Race が保持する合計・ベスト・直近ラップを出す。
void Hud::drawResult(Game& app, float t) {
    IRenderer& hud = *app.renderer();
    hud.beginHud();
    const float W = float(hud.width());
    const float H = float(hud.height());
    const Race& race = app.race();
    // 中央パネル (タイム一覧の土台)。
    const float pw = 580.0f;
    const float ph = 340.0f;
    const float px = (W - pw) * 0.5f;
    const float py = (H - ph) * 0.5f;
    hud.addQuad(px, py, px + pw, py + ph, 0.02f, 0.03f, 0.05f, 0.88f);
    auto centerText = [&](float y, float scale, const char* text, float r, float g, float b) {
        float w = float(std::strlen(text)) * 6.0f * scale;
        hud.drawText((W - w) * 0.5f, y, scale, text, r, g, b, 1);
    };
    centerText(py + 24, 4.4f, "FINISH!", 1.0f, 0.85f, 0.4f);
    char line[96];
    char time[32];
    formatTime(time, sizeof(time), race.total());
    std::snprintf(line, sizeof(line), "TOTAL     %s", time);
    centerText(py + 116, 2.6f, line, 0.95f, 0.96f, 0.98f);
    formatTime(time, sizeof(time), race.best());
    std::snprintf(line, sizeof(line), "BEST LAP  %s", time);
    centerText(py + 162, 2.6f, line, 0.95f, 0.85f, 0.5f);
    formatTime(time, sizeof(time), race.lastLap());
    std::snprintf(line, sizeof(line), "LAST LAP  %s", time);
    centerText(py + 208, 2.6f, line, 0.85f, 0.88f, 0.92f);
    // 行き先の案内は点滅させる。
    if (std::sin(t * 3.2f) > -0.3f) {
        centerText(py + ph - 54, 2.0f, "ENTER: RACE AGAIN    T: TITLE", 1.0f, 1.0f, 1.0f);
    }
    hud.flushHud();
}
