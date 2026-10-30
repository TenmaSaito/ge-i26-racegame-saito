// ============================================================================
// hud.cpp ― 画面表示 (HUD) 層
// 役割: 速度・ラップ・ミニマップ・デバッグ・チューニング画面の合成。
//       値の計算はせず、Game の状態を Renderer の 2D 描画へ写すだけ。
//       ブラウザ版のデバッグ表示はページへ送り、画面内には描かない。
// ============================================================================

#include "hud.hpp"

#include "camera.hpp"
#include "car.hpp"
#include "enginesim.hpp"  // EngineSim (タコメーター)
#include "game.hpp"
#include "input.hpp"  // input::padStatus (パッド認識状態)
#include "scene.hpp"   // kRaceLaps
#include "scenery.hpp"  // countScenery (オブジェクト内訳)
#include "render.hpp"
#include "tuning.hpp"

#include <algorithm>
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
    SceneryCounts sc = countScenery(app.objects());  // オブジェクトの種類別内訳
    char buf[768];
    std::snprintf(buf, sizeof(buf),
                  "STATE: %s\nSLIP: %.0f DEG\nLAT V: %.1f M/S\nFWD V: %.1f M/S (%.0f KM/H)\n"
                  "AMOUNT: %d %%\nCMD: %.2f\nIN: BTN %d STEER %.2f\nIN: DSTK %.2f MSE %.2f THR %.1f\n"
                  "PAD %s\nOBJ %d TREE %d HOUSE %d BLD %d\nSCORE %d CHAIN %d M\n"
                  "ENGINE: GEAR %d  %d RPM\nAUDIO %s\nDIAG(7/8/9) TILT:%s CAM:%s MIR:%s%s",
                  car.drifting ? "DRIFT" : "GRIP", car.slipAngle * 57.29578f,
                  car.lateralSpeed, car.speed, car.speed * 3.6f,
                  static_cast<int>(std::lround(car.driftAmount * 100.0f)), car.driftCommand,
                  in.drift ? 1 : 0, in.steer, in.driftSteer, in.mouseDrift, in.throttle,
                  pad.c_str(),
                  static_cast<int>(app.objects().count()), sc.trees, sc.houses, sc.buildings,
                  static_cast<int>(car.driftScore), static_cast<int>(car.driftChain),
                  app.engine().gear(), static_cast<int>(app.engine().rpm()),
                  app.audioDiag().c_str(),
                  app.diagTilt() ? "ON" : "OFF", app.camera().groundLift ? "ON" : "OFF",
                  app.diagMirror() ? "ON" : "OFF", why);
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
    const int lines = 13;
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
    {
        SceneryCounts sc = countScenery(app.objects());
        std::snprintf(line, sizeof(line), "OBJ: %d T%d H%d B%d",
                      static_cast<int>(app.objects().count()), sc.trees, sc.houses, sc.buildings);
        hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
        ty += lineH;
    }
    std::snprintf(line, sizeof(line), "AUDIO: %s", app.audioDiag().c_str());
    hud.drawText(tx, ty, 1.6f, line, 0.9f, 0.92f, 0.94f, 1);
    ty += lineH;
    std::snprintf(line, sizeof(line), "DIAG(7/8/9) TILT:%s CAM:%s MIR:%s",
                  app.diagTilt() ? "ON" : "OFF", app.camera().groundLift ? "ON" : "OFF",
                  app.diagMirror() ? "ON" : "OFF");
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

// ============================================================================
// スタートシグナル (カウントダウンのランプ)
// ============================================================================
namespace {

// 塗りつぶした円を描く。IRenderer は四角形しか持たないので、
// 1px 程度の細い横帯を積み重ねて円にする (HUD の小さな円なら十分滑らか)。
void fillCircle(IRenderer& hud, float cx, float cy, float radius,
                float r, float g, float b, float a) {
    const float step = radius > 24.0f ? 1.5f : 1.0f;
    for (float dy = -radius; dy < radius; dy += step) {
        const float mid = dy + step * 0.5f;  // 帯の中央の高さで幅を決める
        const float half = std::sqrt(std::max(0.0f, radius * radius - mid * mid));
        hud.addQuad(cx - half, cy + dy, cx + half, cy + dy + step, r, g, b, a);
    }
}

// ランプ 1 個を描く。lit=点灯の強さ 0..1、glow=点灯直後の光の広がり 0..1。
void drawLamp(IRenderer& hud, float cx, float cy, float radius,
              float r, float g, float b, float lit, float glow, float alpha) {
    // 外側の光 (ハロー)。点いた瞬間に大きく広がり、すぐ落ち着く。
    if (lit > 0.0f) {
        const float haloR = radius * (1.35f + 0.55f * glow);
        fillCircle(hud, cx, cy, haloR, r, g, b, alpha * lit * (0.10f + 0.22f * glow));
        fillCircle(hud, cx, cy, radius * 1.15f, r, g, b, alpha * lit * 0.25f);
    }
    // 縁 (ランプのはめ込み枠) とレンズ。消灯時は暗い色で「そこにある」ことだけ見せる。
    fillCircle(hud, cx, cy, radius, 0.05f, 0.05f, 0.06f, alpha);
    const float dim = 0.16f;
    const float k = dim + (1.0f - dim) * lit;
    fillCircle(hud, cx, cy, radius * 0.86f, r * k, g * k, b * k, alpha);
    // レンズの映り込み (左上の小さなハイライト) で立体感を出す。
    fillCircle(hud, cx - radius * 0.28f, cy - radius * 0.30f, radius * 0.24f,
               1.0f, 1.0f, 1.0f, alpha * (0.10f + 0.30f * lit));
}

// 点いたばかりのランプの「光の広がり」(0..1)。点灯から 0.25 秒で消える。
float lampGlow(float sinceLit) {
    const float t = clampf(sinceLit / 0.25f, 0.0f, 1.0f);
    return (1.0f - t) * (1.0f - t);
}

}  // namespace

void Hud::drawCountdown(Game& app) {
    IRenderer& hud = *app.renderer();
    const Race& race = app.race();
    const bool counting = race.counting();
    const bool going = !counting && race.goFlash() > 0.0f;
    if (!counting && !going) {
        return;
    }
    const float W = float(hud.width());
    const float H = float(hud.height());

    // --- ランプ台 (3 灯)。バックミラー (画面上端〜18%) のすぐ下に置く ------
    const float radius = std::max(18.0f, H * 0.036f);
    const float spacing = radius * 2.7f;
    const float cx = W * 0.5f;
    const float cy = H * 0.215f + radius;
    const float boxW = spacing * 2.0f + radius * 2.9f;
    const float boxH = radius * 2.9f;
    // GO の後は台ごとフェードアウトする。
    const float alpha = going ? clampf(race.goFlash() / Race::kGoFlash * 1.6f, 0.0f, 1.0f) : 1.0f;
    hud.addQuad(cx - boxW * 0.5f - 3, cy - boxH * 0.5f - 3, cx + boxW * 0.5f + 3, cy + boxH * 0.5f + 3,
                0.55f, 0.58f, 0.62f, 0.55f * alpha);  // 枠
    hud.addQuad(cx - boxW * 0.5f, cy - boxH * 0.5f, cx + boxW * 0.5f, cy + boxH * 0.5f,
                0.03f, 0.035f, 0.045f, 0.92f * alpha);  // 本体

    // 3・2・1 の数字と、その数字になってからの経過秒。
    int num = 0;
    float sinceSlot = 0.0f;
    if (counting) {
        const float left = race.countdownLeft();
        num = std::min(3, std::max(1, int(std::ceil(left / Race::kCountdownSlot))));
        sinceSlot = Race::kCountdownSlot - (left - float(num - 1) * Race::kCountdownSlot);
    }

    for (int i = 0; i < 3; ++i) {
        const float lx = cx + float(i - 1) * spacing;
        if (going) {
            // GO: 3 灯すべてグリーン。点いた瞬間に強く光る。
            const float sinceGo = Race::kGoFlash - race.goFlash();
            drawLamp(hud, lx, cy, radius, 0.20f, 1.0f, 0.38f, 1.0f, lampGlow(sinceGo), alpha);
        } else {
            // カウント中: 3 で 1 灯、2 で 2 灯、1 で 3 灯がレッドで点く (左から順に)。
            const int litCount = 4 - num;
            const bool lit = i < litCount;
            const bool newest = i == litCount - 1;
            drawLamp(hud, lx, cy, radius, 1.0f, 0.12f, 0.08f, lit ? 1.0f : 0.0f,
                     newest ? lampGlow(sinceSlot) : 0.0f, alpha);
        }
    }

    // --- 数字 / GO! の大きな文字 (ランプの下) -------------------------------
    // 「手前からフェードイン」: 文字が小さく薄く現れ、素早く大きく濃くなる。
    const float textY = cy + boxH * 0.5f + H * 0.03f;
    char line[16];
    if (counting) {
        const float p = sinceSlot / Race::kCountdownSlot;               // 0→1
        const float q = std::min(1.0f, p * 2.4f);                       // 前半で寄り切る
        const float pop = 1.0f - (1.0f - q) * (1.0f - q) * (1.0f - q);  // easeOutCubic
        const float scale = 11.0f * (0.35f + 0.65f * pop);
        std::snprintf(line, sizeof(line), "%d", num);
        hud.drawText((W - 5.0f * scale) * 0.5f, textY, scale, line, 1.0f, 0.35f, 0.28f, pop);
        hud.drawText((W - 27.0f * 6.0f * 2.0f) * 0.5f, textY + 11.0f * 8.0f, 2.0f,
                     "HOLD THROTTLE: CHARGE LAUNCH", 0.85f, 0.88f, 0.92f, 0.9f);
        // ロケットスタートの溜め: スロットルを踏み続けるとバーのように
        // 伸びる。半分 (0.55) を超えると色が変わり、GO で発動する。
        const float charge = race.launchCharge();
        if (charge > 0.02f) {
            const int filled = int(charge * 10.0f + 0.5f);
            char bar[16];
            for (int i = 0; i < 10; ++i) bar[i] = i < filled ? '|' : '.';
            bar[10] = '\0';
            const bool armed = charge > 0.55f;
            hud.drawText((W - 59.0f * 3.0f) * 0.5f, textY + 11.0f * 8.0f + 32.0f, 3.0f, bar,
                         armed ? 1.0f : 0.75f, armed ? 0.55f : 0.85f, armed ? 0.25f : 0.9f, 1);
        }
    } else {
        const float p = 1.0f - race.goFlash() / Race::kGoFlash;  // 0→1
        const float q = std::min(1.0f, p * 2.4f);
        const float pop = 1.0f - (1.0f - q) * (1.0f - q) * (1.0f - q);
        const float scale = 11.0f * (0.35f + 0.65f * pop) * (1.0f + p * 0.12f);
        const float fade = 1.0f - p * p * 0.65f;
        if (race.launchPower() > 0.55f) {
            // ロケットスタート成功: 溜めが足りていたら専用の表示にする。
            const float s = scale * 0.62f;
            hud.drawText((W - 77.0f * s) * 0.5f, textY, s, "ROCKET START!", 1.0f, 0.62f, 0.25f, fade);
        } else {
            hud.drawText((W - 17.0f * scale) * 0.5f, textY, scale, "GO!", 0.55f, 1.0f, 0.60f, fade);
        }
    }
}

// ============================================================================
// タコメーター (右下): ギアと回転数のバー
// ============================================================================
void Hud::drawTachometer(Game& app) {
    IRenderer& hud = *app.renderer();
    const EngineSim& engine = app.engine();
    const float W = float(hud.width());
    const float H = float(hud.height());
    const float x1 = W - 40.0f;
    const float x0 = x1 - 250.0f;
    const float y0 = H - 100.0f;
    const float y1 = y0 + 12.0f;

    // ギア表示 (後退 R / 空ぶかし N / 1〜6)。
    char gear[16];
    if (engine.gear() < 0) {
        std::snprintf(gear, sizeof(gear), "R");
    } else if (engine.gear() == 0) {
        std::snprintf(gear, sizeof(gear), "N");
    } else {
        std::snprintf(gear, sizeof(gear), "%d", engine.gear());
    }
    hud.drawText(x0 - 34.0f, y0 - 12.0f, 4.0f, gear, 0.95f, 0.96f, 0.98f, 1);
    char line[32];
    std::snprintf(line, sizeof(line), "%5d RPM", int(engine.rpm() / 10.0f) * 10);
    hud.drawText(x1 - 9.0f * 6.0f * 1.6f, y0 - 22.0f, 1.6f, line, 0.8f, 0.84f, 0.88f, 1);

    // 回転数のバー: 20 個のセグメント。緑 → 黄 → 赤 (レッドゾーン)。
    const int segments = 20;
    const float gap = 2.0f;
    const float segW = (x1 - x0 - gap * float(segments - 1)) / float(segments);
    const float level = clampf(engine.rpm() / EngineSim::kRedRpm, 0.0f, 1.0f);
    hud.addQuad(x0 - 3, y0 - 3, x1 + 3, y1 + 3, 0.02f, 0.03f, 0.05f, 0.7f);
    for (int i = 0; i < segments; ++i) {
        const float t = (float(i) + 0.5f) / float(segments);
        const bool on = t <= level;
        float r = 0.30f, g = 0.95f, b = 0.45f;              // 低回転: 緑
        if (t > 0.70f) { r = 1.0f; g = 0.85f; b = 0.25f; }  // 中高回転: 黄
        if (t > 0.88f) { r = 1.0f; g = 0.25f; b = 0.20f; }  // レッドゾーン: 赤
        const float sx = x0 + float(i) * (segW + gap);
        hud.addQuad(sx, y0, sx + segW, y1, r, g, b, on ? 0.95f : 0.15f);
    }
}

// ブラウザの自動再生制限でまだ音が出せないときだけ、点滅で案内を出す。
void Hud::drawSoundHint(Game& app, float x, float y) {
    if (!app.audioWaitingForGesture()) {
        return;
    }
    hintBlink_ += 1.0f / 60.0f;
    if (std::sin(hintBlink_ * 5.0f) > -0.2f) {
        app.renderer()->drawText(x, y, 1.8f, "CLICK OR PRESS ANY KEY FOR SOUND", 1.0f, 0.85f, 0.45f, 1);
    }
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

    drawCountdown(app);

    char best[32];
    formatTime(best, sizeof(best), app.race().best());
    std::snprintf(line, sizeof(line), "BEST %s", best);
    hud.drawText(24, 90, 2.0f, line, 0.75f, 0.8f, 0.7f, 1);

    std::snprintf(line, sizeof(line), "CAR %s", kCarSpecs[app.carSpec()].name);
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

    // ドリフト表示: 画面中央下に出す (上部中央はバックミラーの場所)。
    // バーはスリップ角（中央が 0、左右に最大約60度）。
    // 下端の操作ヘルプ (下から約 110px) と重ならないよう、その少し上に置く。
    if (app.car().drifting) {
        const float baseY = float(hud.height()) - 196.0f;  // 表示ブロックの上端
        int deg = static_cast<int>(std::lround(std::fabs(app.car().slipAngle) * 57.29578f));
        std::snprintf(line, sizeof(line), "DRIFT %d DEG", deg);
        float scale = 4.0f;
        float textW = float(std::strlen(line)) * 6.0f * scale;
        hud.drawText((float(hud.width()) - textW) * 0.5f, baseY, scale, line, 1.0f, 0.62f, 0.2f, 1);
        float cx = float(hud.width()) * 0.5f;
        hud.addQuad(cx - 130, baseY + 38, cx + 130, baseY + 46, 0.02f, 0.03f, 0.05f, 0.6f);
        float fill = clampf(app.car().slipAngle / 1.05f, -1.0f, 1.0f) * 130.0f;
        float fx0 = fill < 0 ? cx + fill : cx;
        float fx1 = fill < 0 ? cx : cx + fill;
        hud.addQuad(fx0, baseY + 38, fx1, baseY + 46, 1.0f, 0.62f, 0.2f, 0.95f);
        hud.addQuad(cx - 1.5f, baseY + 36, cx + 1.5f, baseY + 48, 0.95f, 0.96f, 0.98f, 0.9f);
        std::snprintf(line, sizeof(line), "SCORE %d  CHAIN %d M",
                      static_cast<int>(app.car().driftScore), static_cast<int>(app.car().driftChain));
        float subW = float(std::strlen(line)) * 6.0f * 2.0f;
        hud.drawText((float(hud.width()) - subW) * 0.5f, baseY + 56, 2.0f, line, 1.0f, 0.8f, 0.45f, 1);
    } else if (std::fabs(app.car().slipAngle) > 0.15f && app.car().speed > 5.0f) {
        // ドリフト判定ではないが実際に滑っているとき（芝など）。
        int deg = static_cast<int>(std::lround(std::fabs(app.car().slipAngle) * 57.29578f));
        std::snprintf(line, sizeof(line), "SLIDE %d DEG", deg);
        float scale = 3.0f;
        float textW = float(std::strlen(line)) * 6.0f * scale;
        hud.drawText((float(hud.width()) - textW) * 0.5f, float(hud.height()) - 190.0f, scale, line, 0.95f, 0.85f, 0.3f, 1);
    }

    const char* camName = "CHASE";
    if (app.camera().mode == 1) camName = "CLOSE";
    if (app.camera().mode == 2) camName = "FAR";
    if (app.camera().mode == 3) camName = "DRIVER";
    hud.drawText(float(hud.width()) - 140, 22, 2.0f, camName, 0.8f, 0.84f, 0.88f, 1);

    drawTachometer(app);
    drawSoundHint(app, 24.0f, 170.0f);

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
        hud.drawText(float(hud.width()) * 0.5f - 105, float(hud.height()) * 0.45f + 52, 2.2f,
                     "ESC: RESUME", 0.9f, 0.9f, 0.9f, 1);
        hud.drawText(float(hud.width()) * 0.5f - 105, float(hud.height()) * 0.45f + 76, 2.2f,
                     "T: TITLE", 0.9f, 0.9f, 0.9f, 1);
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
    drawSoundHint(app, (W - 32.0f * 6.0f * 1.8f) * 0.5f, H * 0.60f);
    hud.flushHud();
}

// リザルト画面の文字。Race が保持する合計・ベスト・直近ラップを出す。
void Hud::drawResult(Game& app, float t, int rank) {
    IRenderer& hud = *app.renderer();
    hud.beginHud();
    const float W = float(hud.width());
    const float H = float(hud.height());
    const Race& race = app.race();
    // 中央パネル (タイム一覧の土台)。
    const float pw = 620.0f;
    const float ph = 470.0f;
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
    // 今回の順位 (ランキング圏内なら)。
    if (rank == 0) {
        centerText(py + 252, 2.4f, "NEW RECORD!  RANK #1", 1.0f, 0.6f, 0.3f);
    } else if (rank > 0) {
        std::snprintf(line, sizeof(line), "RANK #%d", rank + 1);
        centerText(py + 252, 2.4f, line, 0.95f, 0.85f, 0.5f);
    }
    // コース別ランキング (上位 5 件)。今回のタイムと一致する行は目立たせる。
    const std::vector<float>& board = app.ranking().entries(app.courseStem());
    float rowY = py + 300;
    for (size_t i = 0; i < board.size(); ++i) {
        formatTime(time, sizeof(time), board[i]);
        std::snprintf(line, sizeof(line), "#%d  %s", int(i) + 1, time);
        bool mine = int(i) == rank;
        centerText(rowY, 2.0f, line, mine ? 1.0f : 0.8f, mine ? 0.75f : 0.82f, mine ? 0.4f : 0.86f);
        rowY += 30;
    }
    // 行き先の案内は点滅させる。
    if (std::sin(t * 3.2f) > -0.3f) {
        centerText(py + ph - 44, 2.0f, "ENTER: RACE AGAIN    T: TITLE", 1.0f, 1.0f, 1.0f);
    }
    hud.flushHud();
}

// セレクト画面の文字。コース 3 件と車種 3 件を並べ、選択中を ▶ と色で示す。
// 各コースにはベスト合計タイム (ランキング) も添える。
void Hud::drawSelect(Game& app, int course, int spec, float t) {
    IRenderer& hud = *app.renderer();
    hud.beginHud();
    const float W = float(hud.width());
    const float H = float(hud.height());
    hud.addQuad(0, 0, W, H, 0.02f, 0.04f, 0.08f, 0.35f);
    auto centerText = [&](float y, float scale, const char* text, float r, float g, float b) {
        float w = float(std::strlen(text)) * 6.0f * scale;
        hud.drawText((W - w) * 0.5f, y, scale, text, r, g, b, 1);
    };
    char line[128];
    char time[32];
    centerText(H * 0.12f, 3.4f, "SELECT STAGE", 1.0f, 0.95f, 0.9f);
    float y = H * 0.24f;
    for (int i = 0; i < kCourseCount; ++i) {
        float best = app.ranking().best(kCourses[i].stem);
        if (best > 0) {
            formatTime(time, sizeof(time), best);
            std::snprintf(line, sizeof(line), "%s %s   BEST %s", i == course ? ">" : " ",
                          kCourses[i].name, time);
        } else {
            std::snprintf(line, sizeof(line), "%s %s", i == course ? ">" : " ", kCourses[i].name);
        }
        bool sel = i == course;
        centerText(y, 2.6f, line, sel ? 1.0f : 0.75f, sel ? 0.85f : 0.78f, sel ? 0.45f : 0.82f);
        y += 44;
    }
    centerText(y + 18, 3.4f, "SELECT CAR", 1.0f, 0.95f, 0.9f);
    y += 78;
    for (int i = 0; i < 3; ++i) {
        std::snprintf(line, sizeof(line), "%s %s", i == spec ? ">" : " ", kCarSpecs[i].name);
        bool sel = i == spec;
        centerText(y, 2.6f, line, sel ? 1.0f : 0.75f, sel ? 0.85f : 0.78f, sel ? 0.45f : 0.82f);
        y += 44;
    }
    centerText(y + 16, 1.8f, "LEFT/RIGHT: STAGE   UP/DOWN: CAR", 0.8f, 0.84f, 0.88f);
    if (std::sin(t * 3.2f) > -0.3f) {
        centerText(y + 50, 2.4f, "ENTER: START RACE", 1.0f, 1.0f, 1.0f);
    }
    hud.flushHud();
}
