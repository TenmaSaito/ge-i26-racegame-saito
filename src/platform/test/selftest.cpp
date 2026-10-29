// ============================================================================
// selftest.cpp ― 自己検証（--self-test）
// 役割: ドリフトが成立することと、AI がコースを完走できることを自動確認する。
//       製品の実行経路には含まれない。物理 (car) とコース (track) だけに依存。
// ============================================================================

#include "selftest.hpp"

#include "car.hpp"
#include "effects.hpp"  // タイヤ痕の生成テスト用
#include "ghost.hpp"    // Ghost の検証用
#include "race.hpp"     // Race (ロケットスタート検証用)
#include "object.hpp"   // ObjectManager の検証用
#include "scenery.hpp"  // SceneryBuilder (地形) の検証用
#include "track.hpp"

#include <cmath>
#include <cstdio>

// ドリフトの自己確認: まっすぐ加速 -> ドリフトボタン+全開ステアで滑らせる ->
// 緩めると収束する、の 3 段階を見る。滑らなければ (スリップ角が付かなければ) 失敗。
int driftTest(const Track& track) {
    Car car;
    car.reset(track);
    const float dt = 1.0f / 60.0f;
    DriveInput straight;
    straight.throttle = 1;
    for (int i = 0; i < 60 * 3; ++i) {
        car.update(dt, straight, track);
    }
    if (car.speed < 15.0f) {
        std::printf("drift-test: did not reach speed (%.1f m/s)\n", car.speed);
        return 5;
    }
    DriveInput driftIn;
    driftIn.throttle = 1;
    driftIn.steer = 1;
    driftIn.drift = true;
    float maxSlip = 0;
    float maxLateral = 0;
    for (int i = 0; i < 48; ++i) {  // 0.8 秒
        car.update(dt, driftIn, track);
        maxSlip = std::max(maxSlip, std::fabs(car.slipAngle));
        maxLateral = std::max(maxLateral, std::fabs(car.lateralSpeed));
    }
    std::printf("drift-test: speed %.1f km/h  maxSlip %.0f deg  maxLateral %.1f m/s  score %.0f  chain %.0f m\n",
                car.speed * 3.6f, maxSlip * 57.29578f, maxLateral, car.driftScore, car.driftChain);
    if (maxSlip < 0.30f) {  // 約17度も付かないなら滑っていない
        std::printf("drift-test: no slide\n");
        return 6;
    }
    if (maxLateral < 3.0f) {
        std::printf("drift-test: no lateral velocity\n");
        return 7;
    }
    DriveInput calm;
    calm.throttle = 0.6f;
    for (int i = 0; i < 60 * 2; ++i) {
        car.update(dt, calm, track);
    }
    std::printf("drift-test: after release slip %.0f deg  drifting %d\n",
                std::fabs(car.slipAngle) * 57.29578f, car.drifting ? 1 : 0);
    if (std::fabs(car.slipAngle) > 0.12f || car.drifting) {
        std::printf("drift-test: did not recover grip\n");
        return 8;
    }
    // ツインスティック単体（キーボードの A/D + 矢印、パッドの右スティック）でも
    // 滑ることを確認する。ステアは 0、ドリフトスティックだけを倒す。
    Car car2;
    car2.reset(track);
    for (int i = 0; i < 60 * 3; ++i) {
        car2.update(dt, straight, track);
    }
    DriveInput twin;
    twin.throttle = 1;
    twin.driftSteer = -1;
    float twinMaxSlip = 0;
    for (int i = 0; i < 48; ++i) {
        car2.update(dt, twin, track);
        twinMaxSlip = std::max(twinMaxSlip, std::fabs(car2.slipAngle));
    }
    std::printf("drift-test: twin-stick maxSlip %.0f deg  drifting %d\n",
                twinMaxSlip * 57.29578f, car2.drifting ? 1 : 0);
    if (twinMaxSlip < 0.30f) {
        std::printf("drift-test: twin-stick did not slide\n");
        return 9;
    }
    // マウス（ドリフト中のずれ量）: 新しい車で入り直し、ステアを離して
    // ボタン+マウスだけで維持できること、逆向きのずれで打ち消せることを
    // 確認する。
    Car car3;
    car3.reset(track);
    for (int i = 0; i < 72; ++i) {
        car3.update(dt, straight, track);
    }
    DriveInput enter;
    enter.throttle = 1;
    enter.drift = true;
    enter.steer = -1;
    for (int i = 0; i < 15; ++i) {
        car3.update(dt, enter, track);
    }
    if (!car3.drifting) {
        std::printf("drift-test: mouse phase could not enter drift\n");
        return 10;
    }
    DriveInput mouseHold;
    mouseHold.throttle = 1;
    mouseHold.drift = true;
    mouseHold.mouseDrift = -0.7f;  // 進入と同じ向きにずらしたまま保持
    float mouseMaxSlip = 0;
    for (int i = 0; i < 60; ++i) {
        car3.update(dt, mouseHold, track);
        mouseMaxSlip = std::max(mouseMaxSlip, std::fabs(car3.slipAngle));
    }
    std::printf("drift-test: mouse-hold maxSlip %.0f deg  drifting %d\n",
                mouseMaxSlip * 57.29578f, car3.drifting ? 1 : 0);
    if (mouseMaxSlip < 0.30f || !car3.drifting) {
        std::printf("drift-test: mouse did not sustain drift\n");
        return 11;
    }
    DriveInput mouseCounter;
    mouseCounter.throttle = 1;
    mouseCounter.drift = true;
    mouseCounter.mouseDrift = 0.7f;  // 逆向き = カウンター
    // 穏やかな進入の車はカウンターの効きも漸進的。固定フレームで切らず、
    // 「2.5 秒以内に滑りがほぼ消えること」を見る。
    int counterFrames = 0;
    while (counterFrames < 150 && car3.slipAngle > 0.15f) {
        car3.update(dt, mouseCounter, track);
        ++counterFrames;
    }
    std::printf("drift-test: mouse-counter slip %.0f deg after %d frames\n",
                car3.slipAngle * 57.29578f, counterFrames);
    if (car3.slipAngle > 0.15f) {
        std::printf("drift-test: mouse counter did not cancel drift\n");
        return 12;
    }
    return 0;
}

int runSelfTest(const Track& track, float minLength, float maxLength) {
    int driftResult = driftTest(track);
    if (driftResult != 0) {
        return driftResult;
    }
    Car car;
    car.reset(track);
    float travelled = 0;
    float prevS = -1;
    float maxLat = 0;
    for (int step = 0; step < 60 * 50; ++step) {
        TrackPose pose = track.query(car.x, car.z, car.y, car.ground.found ? car.ground.s : -1.0f);
        float lookS = pose.s + 70.0f;
        if (lookS >= track.length) {
            lookS -= track.length;
        }
        int nearest = 0;
        float nearestGap = 1.0e9f;
        for (int i = 0; i < static_cast<int>(track.samples.size()); i += 2) {
            float gap = std::fabs(track.samples[i].s - lookS);
            if (gap > track.length * 0.5f) {
                gap = track.length - gap;
            }
            if (gap < nearestGap) {
                nearestGap = gap;
                nearest = i;
            }
        }
        int next = (nearest + 1) % static_cast<int>(track.samples.size());
        float tx = track.samples[next].x - track.samples[nearest].x;
        float tz = track.samples[next].z - track.samples[nearest].z;
        float targetYaw = std::atan2(tx, tz);
        float errNow = wrapPi(pose.yaw - car.yaw);
        float errAhead = wrapPi(targetYaw - car.yaw);
        float bend = std::fabs(wrapPi(targetYaw - pose.yaw));
        DriveInput input;
        input.steer = clampf(errNow * 2.6f + errAhead * 0.5f, -1.0f, 1.0f);
        input.steer = clampf(input.steer - pose.lateral * 0.12f, -1.0f, 1.0f);
        input.throttle = bend < 0.15f ? 1.0f : 0.45f;
        input.brake = 0;
        if (bend > 0.28f && car.speed > 22.0f) {
            input.throttle = 0;
            input.brake = 1.0f;
        } else if (bend > 0.12f && car.speed > 34.0f) {
            input.throttle = 0;
            input.brake = 0.55f;
        }
        if (car.speed < 10.0f) {
            input.brake = 0;
            input.throttle = std::max(input.throttle, 0.5f);
        }
        car.update(1.0f / 60.0f, input, track);
        pose = track.query(car.x, car.z, car.y, car.ground.found ? car.ground.s : -1.0f);
        if (prevS >= 0) {
            float ds = pose.s - prevS;
            if (ds < -track.length * 0.5f) ds += track.length;
            if (ds > track.length * 0.5f) ds -= track.length;
            if (ds > 0) travelled += ds;
        }
        prevS = pose.s;
        maxLat = std::max(maxLat, pose.xzDist);
        if (!std::isfinite(car.x) || !std::isfinite(car.speed) || !std::isfinite(car.yaw)) {
            std::printf("self-test non-finite\n");
            return 1;
        }
    }
    std::printf("self-test length %.0f travelled %.0f maxLat %.1f speed %.1f km/h\n",
                track.length, travelled, maxLat, car.speed * 3.6f);
    if (track.length < minLength || track.length > maxLength) return 2;
    if (travelled < 1200.0f) return 3;
    if (maxLat > 30.0f) return 4;
    if (!std::isfinite(car.y)) return 6;
    // --- ゴーストの残像: ドリフト中の記録から光跡の点列が復元できる ----
    {
        Ghost ghost;
        Car gc;
        gc.drifting = true;
        gc.driftAmount = 0.8f;  // 光跡の出る条件
        ghost.beginLap();
        for (int i = 0; i < 120; ++i) {  // 2 秒分、まっすぐ進みながら記録
            gc.x = float(i) * 0.5f;
            ghost.recordPose(gc);
        }
        ghost.finishLap(10.0f);
        std::vector<Ghost::TrailPt> gl, gr;
        if (!ghost.trailPointsAt(1.5f, gl, gr) || gl.size() < 10 || gr.size() < 10) {
            return 22;  // 残像の点列が取れない
        }
    }
    // 地形: コース脇は路面中心と同じ高さ、遠方は山として盛り上がること。
    {
        SceneryBuilder builder(track);
        CpuMesh terrain = builder.buildBackdrop();
        std::printf("self-test terrain tris %d\n", int(terrain.indices.size() / 3));
        if (terrain.indices.size() < 30000) return 15;  // 高度場グリッド一式
        // メッシュ検証: 路面帯 (中心から 15.5m 以内 (アスファルト+縁石の帯)) の三角形の重心が路面より
        // 1.2m 以上上に来ていたら「屋根化」している (実績あり)。
        // 20m より外は意図した山肌なので対象外。
        for (size_t ti = 0; ti + 2 < terrain.indices.size(); ti += 3) {
            Vec3 c{0, 0, 0};
            for (int v = 0; v < 3; ++v) {
                const float* p = &terrain.vertices[size_t(terrain.indices[ti + v]) * 9];
                c.x += p[0] / 3.0f; c.y += p[1] / 3.0f; c.z += p[2] / 3.0f;
            }
            TrackPose q = track.queryPlanar(c.x, c.z);
            if (q.found && q.xzDist < 15.5f && c.y > q.centerY + 1.2f) return 19;
        }
        const TrackSample& s0 = track.samples[100];
        TrackPose g0 = track.query(s0.x, s0.z, s0.y);
        float near = builder.terrainHeightAt(s0.x, s0.z);
        float far = builder.terrainHeightAt(s0.x + g0.rightX * 800.0f, s0.z + g0.rightZ * 800.0f);
        std::printf("self-test terrain near %.1f far %.1f (center %.1f)\n", near, far, g0.centerY);
        if (std::fabs(near - g0.centerY) > 0.01f) return 16;
        if (far < g0.centerY + 4.0f) return 17;  // 丘+遠景で必ず少しは盛り上がる
        // 回帰検証: 全サンプル地点で地形が路面より上に出ないこと。
        // 立体交差で上下の区間を取り違えると地形が路面を覆う事故になる
        // (実際に起きた)。橋の下では地形が下の路面に付くので「下」は許す。
        for (const TrackSample& smp : track.samples) {
            float h = builder.terrainHeightAt(smp.x, smp.z);
            if (h > smp.y + 0.6f) return 18;
        }
    }
    // 火花: 壁に擦らせた車を与えると火花が出ること。
    {
        Effects fx;
        Car scrape = car;
        scrape.wallContact = 1;
        scrape.wallSide = 1;
        scrape.wallImpact = 0.5f;
        scrape.speed = 22.0f;
        for (int i = 0; i < 30; ++i) {
            fx.update(scrape, 1.0f / 60.0f);
        }
    {
        // ロケットスタート: 溜めありで発動し、溜めなしでは発動しないこと。
        Race r1;
        r1.startCountdown();
        r1.setLaunchHeld(true);
        for (int i = 0; i < 130; ++i) r1.tick(10.0f, 1000.0f, 1.0f / 60.0f);
        if (!(r1.launchBoost() > 0.0f && r1.launchPower() > 0.55f)) return 20;
        Race r2;
        r2.startCountdown();
        for (int i = 0; i < 130; ++i) r2.tick(10.0f, 1000.0f, 1.0f / 60.0f);
        if (r2.launchBoost() != 0.0f) return 21;
        std::printf("self-test launch charge/boost ok\n");
    }
        std::printf("self-test sparks %d\n", fx.sparkCount());
        if (fx.sparkCount() <= 0) return 14;
    }
    // 音声ポート: 実装が無くても setSlip は安全 (Game 側のヌルガード確認は
    // ここでは行わず、ブラウザ実機で鳴ることを正とする)。
    // タイヤ痕: ドリフト状態の車を直進させると、痕の区間が刻まれること。
    {
        Effects fx;
        Car skid;
        skid.drifting = true;
        skid.driftAmount = 0.8f;
        skid.speed = 20.0f;
        skid.slipAngle = 0.5f;
        skid.lateralSpeed = 5.0f;
        skid.onAsphalt = true;
        for (int i = 0; i < 90; ++i) {
            skid.x += 20.0f / 60.0f;
            fx.update(skid, 1.0f / 60.0f);
        }
        std::printf("self-test skidmarks %d segments\n", fx.markCount());
        if (fx.markCount() < 10) return 7;
    }
    return 0;
}

