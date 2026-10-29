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
        offReady = false;  // 追跡モードへ戻ったらオフセットを作り直す
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
    if (!ready || !offReady) {
        // 初期配置: オフセットを目標値で初期化して即置く。
        eyeOffSm = eyePos - carPos;
        lookOffSm = lookPos - carPos;
        eye = eyePos;
        look = lookPos;
        ready = true;
        offReady = true;
        return;
    }
    // リセット等で大きく離れていたら即座に置き直す (泳ぎ防止)。
    Vec3 jump = eyePos - eye;
    if (jump.x * jump.x + jump.y * jump.y + jump.z * jump.z > 900.0f) {
        eyeOffSm = eyePos - carPos;
        lookOffSm = lookPos - carPos;
        eye = eyePos;
        look = lookPos;
        return;
    }
    // 追従の平滑化は「車から見たオフセット」そのものに掛ける。
    // 平滑化済みオフセットをメンバに保持し、目標オフセットへ少しずつ
    // 寄せてから現在の車位置を加算する。こうすると車の移動は 1:1 で
    // 目に伝わり (遅れが出ない)、向きの変化だけが滑らかになる。
    // 以前は前フレームの eye から毎回オフセットを逆算していたが、
    // それは数式上「絶対位置の平滑化」と同一になってしまい、遅れの
    // 揺れがそのまま車の前後ブレになっていた。
    // 係数 15/s は従来どおり (加速でカメラが置いていかれる感覚の調整値)。
    float blend = 1.0f - std::exp(-dt * 15.0f);
    eyeOffSm = eyeOffSm + ((eyePos - carPos) - eyeOffSm) * blend;
    lookOffSm = lookOffSm + ((lookPos - carPos) - lookOffSm) * blend;
    eye = carPos + eyeOffSm;
    look = carPos + lookOffSm;
}

float surfaceHeightAt(const Track& track, Vec3 p, float fallbackY, float preferS) {
    TrackPose pose = track.query(p.x, p.z, fallbackY, preferS);
    if (!pose.found) return fallbackY;
    return pose.centerY + pose.lateral * pose.cross;
}
