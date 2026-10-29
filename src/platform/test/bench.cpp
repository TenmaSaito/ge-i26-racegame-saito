// ============================================================================
// bench.cpp ― ベンチマークの実装 (自動運転 + フレーム時間の計測)
// ============================================================================

#include "bench.hpp"

#include "game.hpp"
#include "input.hpp"
#include "platform.hpp"
#include "track.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 角度の畳み込み (wrapPi) と clampf は geom.hpp のものを使う。

int runBenchmark(Game& app, const char* mode) {
    platform::setFrameCapEnabled(false);  // 上限を切り、純粋な処理時間を測る
    if (std::strcmp(mode, "nofx") == 0) {
        app.effects().setEnabled(false);  // 煙・光跡・痕・火花を全部止める
    } else if (std::strcmp(mode, "noscene") == 0) {
        app.setSceneryEnabled(false);    // 地形と景観チャンクを止める
    }

    UiActions ui{};      // 差し込む UI 入力 (毎フレーム組み直す)
    DriveInput drive{};  // 差し込む運転入力 (自動運転)
    std::vector<char> driftFrame;  // そのフレームにドリフトしていたか
    // 車体の傾き (ピッチ) のブルブル計測: 旧方式 (ベースライン 4.4m + 固定
    // 3/s 平滑) と新方式 (7.2m + 6/s) で、平滑後の角度のフレーム間変動を比べる。
    double tiltOld = 0, tiltNew = 0, tiltSqOld = 0, tiltSqNew = 0;
    long tiltN = 0;
    float driftHold = 0;        // ドリフト継続の残り秒
    float driftCool = 0;        // 次にドリフトできるまでの秒

    const int kFrames = 60 * 45;  // 45 秒分 (タイトル→選択→レース)
    auto times = platform::runFrames(app, kFrames, [&](int frame) {
        ui = UiActions{};
        drive = DriveInput{};
        if (frame == 5) ui.confirm = true;    // タイトル → セレクト
        if (frame == 12) ui.confirm = true;   // セレクト → レース
        if (frame > 12) {
            // 自動運転: セルフテストと同じ純追跡で中心線を追い、
            // きついコーナーではドリフトボタンも入れて実プレイに寄せる。
            const Car& car = app.car();
            const Track& track = app.track();
            TrackPose pose = track.query(car.x, car.z, car.y,
                                         car.ground.found ? car.ground.s : -1.0f);
            float lookS = pose.s + 70.0f;
            if (lookS >= track.length) lookS -= track.length;
            int nearest = 0;
            float nearestGap = 1.0e9f;
            for (int i = 0; i < static_cast<int>(track.samples.size()); i += 2) {
                float gap = std::fabs(track.samples[i].s - lookS);
                if (gap > track.length * 0.5f) gap = track.length - gap;
                if (gap < nearestGap) {
                    nearestGap = gap;
                    nearest = i;
                }
            }
            int next = (nearest + 1) % static_cast<int>(track.samples.size());
            float targetYaw = std::atan2(track.samples[next].x - track.samples[nearest].x,
                                         track.samples[next].z - track.samples[nearest].z);
            float errNow = wrapPi(pose.yaw - car.yaw);
            float errAhead = wrapPi(targetYaw - car.yaw);
            float bend = std::fabs(wrapPi(targetYaw - pose.yaw));
            drive.steer = clampf(errNow * 2.6f + errAhead * 0.5f, -1.0f, 1.0f);
            drive.steer = clampf(drive.steer - pose.lateral * 0.12f, -1.0f, 1.0f);
            drive.throttle = bend < 0.15f ? 1.0f : 0.45f;
            if (bend > 0.28f && car.speed > 22.0f) {
                drive.throttle = 0;
                drive.brake = 1.0f;
            } else if (bend > 0.12f && car.speed > 34.0f) {
                drive.throttle = 0;
                drive.brake = 0.55f;
            }
            if (car.speed < 10.0f) {
                drive.brake = 0;
                drive.throttle = std::max(drive.throttle, 0.5f);
            }
            // ドリフトの再現: 強く切っている間はボタンを保持する。
            driftCool -= 1.0f / 60.0f;
            if (driftHold > 0) {
                driftHold -= 1.0f / 60.0f;
                drive.drift = true;
                drive.driftSteer = drive.steer;
                drive.throttle = 1.0f;
                drive.brake = 0;
            } else if (driftCool <= 0 && std::fabs(drive.steer) > 0.45f && car.speed > 26.0f) {
                driftHold = 1.3f;
                driftCool = 2.4f;
                drive.drift = true;
                drive.driftSteer = drive.steer;
            }
            driftFrame.push_back(car.drifting ? 1 : 0);
            // 傾きの計測 (そこそこ速いときだけ意味がある)。
            if (car.speed > 15.0f) {
                Vec3 fw{std::sin(car.yaw), 0, std::cos(car.yaw)};
                float preferS = car.ground.found ? car.ground.s : -1.0f;
                auto pitchAt = [&](float base) {
                    float hF = surfaceHeightAt(track, {car.x + fw.x * base, car.y, car.z + fw.z * base},
                                               car.y, preferS);
                    float hB = surfaceHeightAt(track, {car.x - fw.x * base, car.y, car.z - fw.z * base},
                                               car.y, preferS);
                    return std::atan2(hF - hB, base * 2.0f);
                };
                const float kOld = 1.0f - std::exp(-3.0f / 60.0f);
                const float kNew = 1.0f - std::exp(-6.0f / 60.0f);
                double prevOld = tiltOld, prevNew = tiltNew;
                tiltOld += (pitchAt(2.2f) - tiltOld) * kOld;
                tiltNew += (pitchAt(3.6f) - tiltNew) * kNew;
                if (tiltN > 0) {
                    tiltSqOld += (tiltOld - prevOld) * (tiltOld - prevOld);
                    tiltSqNew += (tiltNew - prevNew) * (tiltNew - prevNew);
                }
                ++tiltN;
            }
        }
        input::setBenchUi(&ui);
        input::setBenchDrive(&drive);
    });
    input::setBenchUi(nullptr);
    input::setBenchDrive(nullptr);

    // 集計: 最初の 10 秒 (タイトル+カウント+助走) を除いた区間で見る。
    std::vector<double> all, drift;
    for (size_t i = 600; i < times.size(); ++i) {
        all.push_back(times[i]);
        size_t di = i - 13;  // driftFrame は frame 13 から積まれている
        if (di < driftFrame.size() && driftFrame[di]) {
            drift.push_back(times[i]);
        }
    }
    auto stat = [](std::vector<double> v, const char* label) {
        if (v.empty()) {
            std::printf("%s: no samples\n", label);
            return;
        }
        std::sort(v.begin(), v.end());
        double sum = 0;
        for (double t : v) sum += t;
        std::printf("%s: n=%d avg=%.2fms p50=%.2f p95=%.2f max=%.2f\n", label,
                    int(v.size()), sum / double(v.size()), v[v.size() / 2],
                    v[size_t(v.size() * 0.95)], v.back());
    };
    if (tiltN > 1) {
        std::printf("  tilt jitter RMS: old=%.5f rad new=%.5f rad (n=%ld)\n",
                    std::sqrt(tiltSqOld / double(tiltN - 1)),
                    std::sqrt(tiltSqNew / double(tiltN - 1)), tiltN);
    }
    std::printf("bench mode=%s\n", mode);
    stat(all, "  all  ");
    stat(drift, "  drift");
    return 0;
}
