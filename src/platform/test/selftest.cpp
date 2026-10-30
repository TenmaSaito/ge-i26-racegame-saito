// ============================================================================
// selftest.cpp ― 自己検証（--self-test）
// 役割: ドリフトが成立することと、AI がコースを完走できることを自動確認する。
//       音の合成 (sound) も、音声デバイスを使わずに数値で確認する。
//       製品の実行経路には含まれない。
// ============================================================================

#include "selftest.hpp"

#include "aidriver.hpp"   // AI ドライバーの検証用
#include "car.hpp"
#include "enginesim.hpp"  // EngineSim (ギアと回転数) の検証用
#include "effects.hpp"  // タイヤ痕の生成テスト用
#include "ghost.hpp"    // Ghost の検証用
#include "race.hpp"     // Race (ロケットスタート検証用)
#include "racerecord.hpp"  // 走行記録と学習の検証用
#include "object.hpp"   // ObjectManager の検証用
#include "pcm.hpp"         // WAV 読み込み・ループ加工の検証用
#include "scenery.hpp"  // SceneryBuilder (地形) の検証用
#include "soundmixer.hpp"  // ミキサーの検証用
#include "track.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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


// ============================================================================
// 音の自己確認 (音声デバイスは使わず、合成結果の数値だけを見る)
// ============================================================================
namespace {

// テスト用の WAV (16bit ステレオ、正弦波) をメモリ上に作る。
std::string makeTestWav(int rate, int frames, float hz) {
    std::string wav;
    auto put16 = [&](uint32_t v) { wav.push_back(char(v & 0xFF)); wav.push_back(char((v >> 8) & 0xFF)); };
    auto put32 = [&](uint32_t v) { put16(v & 0xFFFF); put16(v >> 16); };
    const uint32_t dataBytes = uint32_t(frames) * 4;
    wav += "RIFF"; put32(36 + dataBytes); wav += "WAVE";
    wav += "fmt "; put32(16); put16(1); put16(2); put32(uint32_t(rate)); put32(uint32_t(rate) * 4); put16(4); put16(16);
    wav += "data"; put32(dataBytes);
    for (int i = 0; i < frames; ++i) {
        const int16_t v = int16_t(std::sin(6.2831853f * hz * float(i) / float(rate)) * 16000.0f);
        put16(uint16_t(v));
        put16(uint16_t(v));
    }
    return wav;
}

// 波形の RMS (音の大きさの目安)。
float rms(const std::vector<float>& buf) {
    double sum = 0;
    for (float v : buf) sum += double(v) * v;
    return buf.empty() ? 0.0f : float(std::sqrt(sum / double(buf.size())));
}

}  // namespace

int soundTest() {
    // --- WAV の読み込み ---------------------------------------------------
    Pcm pcm;
    if (!decodeWav(makeTestWav(44100, 44100, 220.0f), pcm) || pcm.rate != 44100 || pcm.frames() != 44100) {
        std::printf("sound-test: decodeWav failed\n");
        return 30;
    }
    if (decodeWav(std::string("not a wav file"), pcm) || pcm.frames() != 44100) {
        return 31;  // 壊れた入力は失敗し、読み込み済みの中身も変わらない
    }

    // --- シームレスループ: 最後のサンプル → 最初のサンプルが滑らかにつながる ---
    Pcm loop = makeSeamlessLoop(pcm, 0.0f, 0.2f);
    const int n = loop.frames();
    float maxStep = 0;  // 隣り合うサンプルの差の最大 (ループ内部)
    for (int i = 1; i < n; ++i) {
        maxStep = std::max(maxStep, std::fabs(loop.samples[size_t(i) * 2] - loop.samples[size_t(i - 1) * 2]));
    }
    const float seamStep = std::fabs(loop.samples[0] - loop.samples[size_t(n - 1) * 2]);
    std::printf("self-test loop seam %.4f (inner max %.4f)\n", seamStep, maxStep);
    if (n <= 0 || seamStep > maxStep * 1.5f + 1e-4f) {
        return 32;  // 継ぎ目で波形が跳んでいる (プチ音になる)
    }

    // --- ミキサー: 鳴らす/止めるが音量に反映される -------------------------
    SoundMixer mixer;
    mixer.setOutputRate(48000);
    mixer.setEngineLoop(loop);
    mixer.setTireLoop(loop);
    std::vector<float> buf(2048 * 2);
    mixer.render(buf.data(), 2048);
    if (rms(buf) > 1e-4f) return 33;  // 何も指示していなければ無音

    EngineSound engine;
    engine.running = true;
    engine.rpm = 3000.0f;
    engine.rpmNorm = 0.3f;
    engine.throttle = 1.0f;
    engine.load = 1.0f;
    mixer.setEngine(engine);
    for (int i = 0; i < 20; ++i) mixer.render(buf.data(), 2048);  // フェードインを待つ
    const float engineRms = rms(buf);
    std::printf("self-test engine rms %.3f\n", engineRms);
    if (engineRms < 0.02f) return 34;
    for (float v : buf) {
        if (!(std::fabs(v) <= 1.0f)) return 35;  // NaN や過大値が出ていない
    }

    mixer.setEngine(EngineSound{});
    for (int i = 0; i < 60; ++i) mixer.render(buf.data(), 2048);  // フェードアウトを待つ
    if (rms(buf) > 1e-3f) return 36;

    TireSound tire;
    tire.depth = 0.8f;
    tire.duration = 1.0f;
    tire.speed = 25.0f;
    mixer.setTires(tire);
    for (int i = 0; i < 10; ++i) mixer.render(buf.data(), 2048);
    std::printf("self-test tire rms %.3f\n", rms(buf));
    if (rms(buf) < 0.02f) return 37;

    // --- エンジン回転数のモデル: 加速でシフトアップ、リミッターで頭打ち ---
    EngineSim sim;
    sim.reset(82.0f);
    for (float v = 0; v < 80.0f; v += 0.2f) {
        sim.update(1.0f / 60.0f, v, 1.0f, 0.0f, false);
    }
    std::printf("self-test engine gear %d rpm %.0f\n", sim.gear(), sim.rpm());
    if (sim.gear() < 5 || sim.rpm() > EngineSim::kRedRpm) return 38;
    sim.reset(82.0f);
    for (int i = 0; i < 120; ++i) sim.update(1.0f / 60.0f, 0.0f, 1.0f, 0.0f, true);  // 空ぶかし全開
    if (sim.gear() != 0 || sim.rpm() < EngineSim::kRedRpm * 0.85f) return 39;
    std::printf("self-test sound ok\n");
    return 0;
}

// ============================================================================
// AI と走行記録の自己確認
// ============================================================================

// AI が 1 人でコースを 1 周できること (強さごと)。ラップタイムと壁への接触を表示する。
// 強い AI ほど速いこと (EASY > NORMAL > HARD のラップタイム) も確かめる。
int aiTest(const Track& track) {
    const float dt = 1.0f / 60.0f;
    float laps[kAiLevelCount] = {0, 0, 0};
    std::printf("ai-test spawn at s=%.1f of %.1f m\n", spawnDistance(track), track.length);
    for (int level = 0; level < kAiLevelCount; ++level) {
        Car car;
        placeCarOnTrack(car, track, spawnDistance(track), 0.0f);
        AiDriver driver;
        driver.prepare(track, aiSkillFor(AiLevel(level), 0.0f), {}, 12345u);
        Race race;
        race.reset();
        int wallSteps = 0;
        float maxLateral = 0.0f;
        for (int step = 0; step < 60 * 600 && race.completed() < 1; ++step) {
            DriveInput in = driver.drive(car, track, dt, {});
            car.update(dt, in, track);
            race.tick(car.ground.s, track.length, dt);
            if (car.wallContact) ++wallSteps;
            maxLateral = std::max(maxLateral, std::fabs(car.ground.lateral));
        }
        std::printf("ai-test %-6s lap %.2f s  wall %.1f s  maxLat %.1f m\n", aiLevelName(AiLevel(level)),
                    race.lastLap(), float(wallSteps) * dt, maxLateral);
        if (race.completed() < 1) {
            std::printf("ai-test: %s did not finish a lap\n", aiLevelName(AiLevel(level)));
            return 40;
        }
        laps[level] = race.lastLap();
    }
    if (!(laps[0] > laps[1] && laps[1] > laps[2])) {
        std::printf("ai-test: levels are not ordered by pace\n");
        return 41;
    }
    return 0;
}

// 走行記録の保存形式と学習の確認。
int recordTest() {
    RaceRecord r;
    r.name = "tester 01";
    r.course = "suzuka";
    r.car = 1;
    r.level = 2;
    r.total = 300.5f;
    r.bestLap = 98.25f;
    r.profile.assign(kProfileBins, 40.0f);
    RaceRecord back;
    if (!RaceRecord::parse(r.serialize(), back) || back.name != "TESTER_01" || back.course != "suzuka" ||
        std::fabs(back.total - 300.5f) > 0.01f || back.profile.size() != size_t(kProfileBins)) {
        std::printf("record-test: round trip failed\n");
        return 42;
    }
    if (RaceRecord::parse("R1|X|suzuka|0|0|10|20|1,2", back)) {
        return 43;  // ベストが合計より遅い・区間数が足りない記録は拒否
    }

    RecordBook book;
    book.addOwn(r);
    RaceRecord other = r;
    other.name = "RIVAL";
    other.total = 290.0f;
    other.profile.assign(kProfileBins, 50.0f);
    book.addShared(other);
    if (book.addShared(other) != 0) return 44;  // 同じ記録は 2 度入らない
    const std::vector<float> learned = book.learnedProfile("suzuka");
    // 速い記録 (50 m/s) の重みが大きいので、平均は 45 より 50 寄りになる。
    if (learned.size() != size_t(kProfileBins) || !(learned[0] > 45.0f && learned[0] < 50.0f)) return 45;
    if (book.pickRivalModels("suzuka", "TESTER_01", 3).size() != 1) return 46;

    book.noteRace(1, {"RIVAL", "AKAGI"}, 1, 3);
    book.noteRace(1, {"RIVAL"}, 2, 2);
    if (book.opponentsFaced() != 2 || book.battles() != 3 || book.wins() != 1) return 47;
    if (!(book.paceAdjust(1) == 0.0f)) return 48;  // 1 回勝って 1 回最下位 → 補正は元に戻る

    RecordBook restored;
    restored.deserialize(book.serialize());
    if (restored.opponentsFaced() != 2 || restored.battles() != 3 || restored.sharedCount("suzuka") != 1) return 49;
    std::printf("self-test records ok (learned %.1f m/s)\n", learned[0]);
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
    // 音: WAV 読み込み・ループの継ぎ目・ミキサーの鳴らし分け・ギアシフト。
    if (int rc = soundTest()) {
        return rc;
    }
    // 対戦相手の AI と、走行記録・学習。
    if (int rc = aiTest(track)) {
        return rc;
    }
    if (int rc = recordTest()) {
        return rc;
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

