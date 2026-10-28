// ============================================================================
// camera.cpp ― カメラ層
// 役割: 追跡カメラとドライバーズアイの位置決め、路面高のサンプリング。
//       描画は行わず、「目」の位置と注視点を Game の camera に書くだけ。
// ============================================================================

#include "camera.hpp"

#include "game.hpp"
#include "track.hpp"

#include <cmath>

void updateCamera(Game& app, float dt) {
    // ドライバーズアイ: 運転席の頭位置に固定。前を見る向きは車体の向き主体で、
    // ドリフト時は進行方向へ少しだけ視線を逃がす。遅れ（ブレンド）は付けない。
    const float px = app.viewReady ? app.viewX : app.car.x;
    const float py = app.viewReady ? app.viewY : app.car.y;
    const float pz = app.viewReady ? app.viewZ : app.car.z;
    const float pyaw = app.viewReady ? app.viewYaw : app.car.yaw;
    const float pslip = app.viewReady ? app.viewSlip : app.car.slipAngle;
    if (app.camera.mode == 3) {
        float heading = pyaw + pslip * 0.25f * app.car.driftAmount;
        Vec3 fw{std::sin(heading), 0, std::cos(heading)};
        // 運転席の位置（キャビン上端より少し上）。デッキ面が視界の下部に
        // フードとして入るくらいの高さにする。
        Vec3 eye = Vec3{px, py, pz} + fw * -0.2f + Vec3{0, 1.22f, 0};
        app.camera.eye = eye;
        app.camera.look = eye + fw * 10.0f + Vec3{0, -0.45f, 0};
        app.camera.ready = true;
        return;
    }
    // 疾走感のため既定カメラをぐっと近づけた（後方 8.6→6.0m、高さ 3.3→2.3m）。
    // 車が大きく映り、路傍の木やガードレールが速く流れるようになる。
    const float back[3] = {4.6f, 3.2f, 10.5f};
    const float up[3] = {1.8f, 1.2f, 4.6f};
    const float ahead[3] = {5.5f, 4.5f, 9.0f};
    int mode = app.camera.mode;
    // ドリフト中は車体の向きではなく進行方向寄りにカメラを向ける。
    // 車がカニ走りしても視線が置いていかれず、滑っている感じが伝わる。
    float heading = pyaw + pslip * 0.55f * app.car.driftAmount;
    Vec3 forward{std::sin(heading), 0, std::cos(heading)};
    Vec3 eye = Vec3{px, py, pz} - forward * back[mode] + Vec3{0, up[mode], 0};
    Vec3 look = Vec3{px, py, pz} + forward * ahead[mode] + Vec3{0, 1.15f, 0};
    TrackPose under = app.track.query(eye.x, eye.z, app.car.y, app.car.ground.s);
    if (under.found && under.xzDist < 30.0f && eye.y < under.centerY + 0.8f) {
        eye.y = under.centerY + 0.8f;
    }
    if (!app.camera.ready) {
        app.camera.eye = eye;
        app.camera.look = look;
        app.camera.ready = true;
        return;
    }
    // 追従は速め（7→15/s）。遅いと加速時にカメラだけ置いていかれて
    // 車が画面の中で前後に泳ぐ。補間ポーズ側が滑らかなので速くても荒れない。
    float blend = 1.0f - std::exp(-dt * 15.0f);
    app.camera.eye = app.camera.eye + (eye - app.camera.eye) * blend;
    app.camera.look = app.camera.look + (look - app.camera.look) * blend;
}

// タイヤ煙の発生と更新。ドリフト中（滑り角が付いて後輪が滑っている間）
// だけ後輪位置から出し、上昇しながら広がる。

float surfaceHeightAt(const Game& app, Vec3 p) {
    TrackPose pose = app.track.query(p.x, p.z, app.car.y, app.car.ground.s);
    if (!pose.found) return app.car.y;
    return pose.centerY + pose.lateral * pose.cross;
}

float carPitch(const Game& app) {
    Vec3 forward{std::sin(app.car.yaw), 0, std::cos(app.car.yaw)};
    float preferS = app.car.ground.found ? app.car.ground.s : -1.0f;
    TrackPose ahead = app.track.query(app.car.x + forward.x * 2.4f, app.car.z + forward.z * 2.4f, app.car.y, preferS);
    TrackPose behind = app.track.query(app.car.x - forward.x * 2.4f, app.car.z - forward.z * 2.4f, app.car.y, preferS);
    if (!ahead.found || !behind.found) {
        return 0;
    }
    return std::atan2(ahead.centerY - behind.centerY, 4.8f);
}


// ドライバーズアイ用: 鼻先だけのメッシュ。本体全部を描くとデッキ面が
// 視界の下半分を埋めてしまうので、前方クリップとスプリッタだけ見せる。
