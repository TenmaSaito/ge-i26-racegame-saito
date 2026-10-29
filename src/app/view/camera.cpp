// ============================================================================
// camera.cpp ― カメラ (Orbit) の実装
// 役割: 追跡カメラとドライバーズアイの位置決め、路面高のサンプリング。
//       描画は行わず、「目」の位置と注視点を決めるだけ。
// ============================================================================

#include "camera.hpp"

#include "track.hpp"

#include <cmath>

void Orbit::update(Vec3 carPos, float carYaw, float carSlip, float driftAmount,
                   const Track& track, float groundS, float dt) {
    // ドライバーズアイ: 運転席の頭位置に固定。前を見る向きは車体の向き主体で、
    // ドリフト時は進行方向へ少しだけ視線を逃がす。遅れ（ブレンド）は付けない。
    if (mode == 3) {
        float heading = carYaw + carSlip * 0.25f * driftAmount;
        Vec3 fw{std::sin(heading), 0, std::cos(heading)};
        // 運転席の位置（キャビン上端より少し上）。デッキ面が視界の下部に
        // フードとして入るくらいの高さにする。
        Vec3 eyePos = carPos + fw * -0.2f + Vec3{0, 1.22f, 0};
        eye = eyePos;
        look = eyePos + fw * 10.0f + Vec3{0, -0.45f, 0};
        ready = true;
        return;
    }
    // 追跡カメラの距離感。疾走感のため既定をぐっと近づけてある
    // (車が大きく映り、路傍の木やガードレールが速く流れる)。
    static constexpr float back[3] = {4.6f, 3.2f, 10.5f};   // 車の後方への距離
    static constexpr float up[3] = {1.8f, 1.2f, 4.6f};      // 高さ
    static constexpr float ahead[3] = {5.5f, 4.5f, 9.0f};   // 注視点の前方への距離
    // ドリフト中は車体の向きではなく進行方向寄りにカメラを向ける。
    // 車がカニ走りしても視線が置いていかれず、滑っている感じが伝わる。
    float heading = carYaw + carSlip * 0.55f * driftAmount;
    Vec3 forward{std::sin(heading), 0, std::cos(heading)};
    Vec3 eyePos = carPos - forward * back[mode] + Vec3{0, up[mode], 0};
    Vec3 lookPos = carPos + forward * ahead[mode] + Vec3{0, 1.15f, 0};
    // カメラが地面にめり込まないよう、真下の路面より低ければ持ち上げる。
    TrackPose under = track.query(eyePos.x, eyePos.z, carPos.y, groundS);
    if (under.found && under.xzDist < 30.0f && eyePos.y < under.centerY + 0.8f) {
        eyePos.y = under.centerY + 0.8f;
    }
    if (!ready) {
        eye = eyePos;
        look = lookPos;
        ready = true;
        return;
    }
    // 追従は速め（15/s）。遅いと加速時にカメラだけ置いていかれて
    // 車が画面の中で前後に泳ぐ。補間ポーズ側が滑らかなので速くても荒れない。
    float blend = 1.0f - std::exp(-dt * 15.0f);
    eye = eye + (eyePos - eye) * blend;
    look = look + (lookPos - look) * blend;
}

float surfaceHeightAt(const Track& track, Vec3 p, float fallbackY, float preferS) {
    TrackPose pose = track.query(p.x, p.z, fallbackY, preferS);
    if (!pose.found) return fallbackY;
    return pose.centerY + pose.lateral * pose.cross;
}
