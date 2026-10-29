// ============================================================================
// selftest.cpp ― 自己検証（--self-test）
// 役割: ドリフトが成立することと、AI がコースを完走できることを自動確認する。
//       製品の実行経路には含まれない。物理 (car) とコース (track) だけに依存。
// ============================================================================

#include "selftest.hpp"

#include "car.hpp"
#include "effects.hpp"  // タイヤ痕の生成テスト用
#include "object.hpp"   // ObjectManager の検証用
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
    int airSteps = 0;
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
        if (car.airborne) {
            ++airSteps;  // 重力: 坂の頂点で浮いた回数 (報告用)
        }
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
    std::printf("self-test airborne steps %d (gravity)\n", airSteps);
    if (track.length < minLength || track.length > maxLength) return 2;
    if (travelled < 1200.0f) return 3;
    if (maxLat > 30.0f) return 4;
    if (car.airborne) return 5;  // 走り終わって空中に残っていたら着地できていない
    if (!std::isfinite(car.y)) return 6;
    // 重力: 空中に放り出された車が放物線を描いて必ず着地すること。
    // (坂の頂点での離陸そのものはコース形状と速度に依存するので、
    //  ここでは放物線と着地の正しさだけを決定的に確認する。)
    {
        Car fall;
        fall.reset(track);
        // スタート地点は平らなはずなので、数ステップ接地を安定させる。
        DriveInput idle;
        for (int i = 0; i < 30; ++i) fall.update(1.0f / 60.0f, idle, track);
        float groundY = fall.y;
        fall.airborne = true;
        fall.vy = 5.0f;  // 上向き 5 m/s で離陸した状態を再現
        float peak = fall.y;
        int airFrames = 0;
        for (int i = 0; i < 180 && fall.airborne; ++i) {
            fall.update(1.0f / 60.0f, idle, track);
            peak = std::max(peak, fall.y);
            ++airFrames;
        }
        std::printf("self-test gravity peak %.2f m, air %d frames, landed %d\n",
                    peak - groundY, airFrames, fall.airborne ? 0 : 1);
        if (fall.airborne) return 8;             // 3 秒経っても落ちてこない
        if (peak - groundY < 0.5f) return 9;      // 放物線になっていない
        if (std::fabs(fall.y - groundY) > 0.05f) return 10;  // 着地位置がずれている
    }
    // オブジェクト管理: 登録・名前検索・型検索・一括更新・破棄ができること。
    {
        struct Probe : IObject {
            int updates = 0;
            void update(float) override { ++updates; }
        };
        struct Other : IObject {};
        ObjectManager mgr;
        Probe& a = mgr.add<Probe>("Alpha");
        mgr.add<Probe>("Beta");
        mgr.add<Other>("Gamma");
        bool okFind = mgr.find("Alpha") == &a && mgr.find("Nope") == nullptr;
        bool okType = mgr.countOfType<Probe>() == 2 && mgr.findAllOfType<Probe>().size() == 2 &&
                      mgr.findOfType<Other>() != nullptr;
        mgr.updateAll(0.016f);
        bool okUpdate = a.updates == 1;
        mgr.destroy("Beta");
        mgr.updateAll(0.016f);  // 破棄の掃除は一括更新のタイミングで行われる
        bool okDestroy = mgr.count() == 2 && mgr.find("Beta") == nullptr;
        std::printf("self-test objects find %d type %d update %d destroy %d\n",
                    okFind ? 1 : 0, okType ? 1 : 0, okUpdate ? 1 : 0, okDestroy ? 1 : 0);
        if (!okFind || !okType || !okUpdate || !okDestroy) return 11;
    }
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

