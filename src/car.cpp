#include "car.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMaxSpeed = 82.0f;      // 約 295 km/h
constexpr float kMaxReverse = 9.0f;
constexpr float kAccel = 13.0f;
constexpr float kBrake = 30.0f;
constexpr float kReverseAccel = 8.0f;
constexpr float kDrag = 0.00045f;
constexpr float kRolling = 0.9f;
constexpr float kWheelbase = 3.05f;
constexpr float kSteerRate = 2.6f;
constexpr float kWheelRadius = 0.34f;

// --- ドリフト用 -------------------------------------------------------------
constexpr float kGripLateral = 15.0f;   // 通常時の横グリップ（横速度の減衰率、1/s）
constexpr float kGripDrift = 1.15f;     // ドリフト時の横グリップ。これだけ残すと滑り続ける
constexpr float kMaxSlipAngle = 0.95f;  // 目標スリップ角の上限（約54度）
constexpr float kDriftMinSpeed = 9.0f;  // ドリフトに入れる最低速度（m/s、約32km/h）

float maxSteerAt(float speed) {
    return 0.46f / (1.0f + std::fabs(speed) * 0.085f);
}

}  // namespace

void Car::reset(const Track& track) {
    x = track.spawnX;
    y = track.spawnY;
    z = track.spawnZ;
    yaw = track.spawnYaw;
    speed = 0;
    steerAngle = 0;
    wheelSpin = 0;
    brakeHold = 0;
    velX = 0;
    velZ = 0;
    lateralSpeed = 0;
    slipAngle = 0;
    drifting = false;
    driftAmount = 0;
    driftCommand = 0;
    driftCharge = 0;
    driftScore = 0;
    driftChain = 0;
    driftBestChain = 0;
    onAsphalt = true;
    ground = track.query(x, z, y);
}

void Car::stepDrift(float dt, const DriveInput& input) {
    // ------------------------------------------------------------------------
    // ドリフト走行（Inertial Drift 風）。
    //
    // 肝は「速度はスカラーではなくベクトル」なこと。速度ベクトル (velX, velZ) は
    // ワールド座標に残る（＝慣性）。車体（yaw）だけが先に回るので、進行方向と
    // 車体の向きがずれ、そのずれがスリップ角＝横滑りになる。グリップ走行は
    // 「横速度を毎フレーム強く殺す」場合の特殊形としてここに含まれる。
    //
    // きっかけは 3 つ。
    //   1. ドリフトボタン（Space / Shift、パッド B / LB / RB）を押しながら切る
    //   2. 右スティック（ツインスティック）: 切った向きに車体だけを回して滑らせる
    //   3. フリック: 高速で大きく切りながらアクセルを踏み続けるとリアが流れる
    // ドリフト中は横グリップが kGripDrift まで落ち、ステア（または右スティック）が
    // 目標スリップ角を決める。逆に切れば（カウンターステア）スリップ角が縮んで
    // 収束する。ボタンを離すとグリップが戻り、滑りが収まってドリフト終了。
    //
    // アクセル、ブレーキ、壁との接触は update() 側にある。update() は前方向の
    // 速度成分である speed に駆動力を積分してからここを呼ぶ。
    // ------------------------------------------------------------------------
    const float sinYaw0 = std::sin(yaw);
    const float cosYaw0 = std::cos(yaw);
    // 前フレーム確定時点の前方向速度。speed は update() が駆動力を積分済みなので、
    // その差分が「今フレーム、車体前方へ新たに加わった速度」になる。
    const float forwardBefore = velX * sinYaw0 + velZ * cosYaw0;
    const float driveDelta = speed - forwardBefore;

    // --- ドリフトの開始・維持の判定 -------------------------------------
    const float steerIn = clampf(input.steer, -1.0f, 1.0f);
    const float twinIn = clampf(input.driftSteer, -1.0f, 1.0f);
    const bool twinActive = std::fabs(twinIn) > 0.45f;
    // フリック入力の蓄積。条件が外れたら速く抜ける。
    if (std::fabs(steerIn) > 0.9f && input.throttle > 0.5f && speed > 26.0f) {
        driftCharge += dt;
    } else {
        driftCharge = std::max(0.0f, driftCharge - dt * 2.0f);
    }
    const bool flick = driftCharge > 0.30f;
    const bool wantDrift = (input.drift && std::fabs(steerIn) > 0.2f) || twinActive || flick;
    // 操作の主役（右スティック優先）。キーボードはオン/オフしかないので、そのまま
    // 使うと目標角度が跳ねてスピンしやすい。コマンドを平滑化して、デジタル入力
    // でも「浅く入って深める」ができるようにする（アナログ入力にはほぼ影響なし）。
    const float rawCommand = twinActive ? twinIn : steerIn;
    const float commandTarget = wantDrift ? rawCommand : 0.0f;
    driftCommand += clampf(commandTarget - driftCommand, -6.0f * dt, 6.0f * dt);
    const float command = driftCommand;
    // 目標スリップ角は操作と逆符号になる:
    // 右へ切ると車体が右へ回り、速度は慣性で左に残るのでスリップ角は負。
    const float targetSlip =
        -(command > 0 ? 1.0f : -1.0f) * kMaxSlipAngle * (0.45f + 0.55f * std::fabs(command));

    if (!drifting && wantDrift && speed > kDriftMinSpeed) {
        drifting = true;
    }
    if (drifting && speed < 3.5f) {
        drifting = false;
    }
    if (drifting && !wantDrift && std::fabs(slipAngle) < 0.10f && std::fabs(lateralSpeed) < 1.5f) {
        drifting = false;
    }
    // グリップ量は急に切り替えず滑らかに移す（入るとき速く、戻るとき少し遅く）。
    const float amountTarget = drifting ? 1.0f : 0.0f;
    const float amountRate = drifting ? 3.0f : 1.8f;
    driftAmount += clampf(amountTarget - driftAmount, -amountRate * dt, amountRate * dt);

    // --- ヨーレート --------------------------------------------------------
    float yawRate = 0;
    if (std::fabs(speed) > 0.05f) {
        yawRate = (speed / kWheelbase) * std::tan(steerAngle);
    }
    float slow = clampf(1.0f - std::fabs(speed) / 8.0f, 0.0f, 1.0f);
    yawRate += steerAngle * 0.9f * slow;
    if (driftAmount > 0.01f && (velX != 0.0f || velZ != 0.0f)) {
        const float velAngle = std::atan2(velX, velZ);  // 進行方向（yaw と同じ測り方）
        if (wantDrift) {
            // 目標スリップ角になる車体の向きへ回す追加ヨー。
            const float yawErr = wrapPi((velAngle - targetSlip) - yaw);
            yawRate += clampf(yawErr * 3.0f, -1.6f, 1.6f) * driftAmount;
        } else {
            // 入力を離したら、車体を進行方向へ戻す向きに弱く回して収束を助ける。
            const float yawErr = wrapPi(velAngle - yaw);
            yawRate += clampf(yawErr * 1.2f, -0.8f, 0.8f) * driftAmount;
        }
    }
    yaw += yawRate * dt;

    // --- 新しい車体軸で速度を分解する（速度ベクトル自体はワールドに保持） ---
    const float sinYaw = std::sin(yaw);
    const float cosYaw = std::cos(yaw);
    float vf = velX * sinYaw + velZ * cosYaw;   // 前方向成分
    float vl = velX * cosYaw - velZ * sinYaw;   // 横方向成分（右が正）
    vf += driveDelta;                           // 駆動力は車体前方へ足す

    // --- グリップ: 横速度を指数減衰させる ---------------------------------
    float grip = kGripLateral + (kGripDrift - kGripLateral) * driftAmount;
    if (!onAsphalt) {
        grip *= 0.55f;  // 芝はもともと滑る
    }
    vl *= std::exp(-grip * dt);
    if (input.drift && drifting) {
        vf -= vf * 0.35f * dt;  // 後輪ロック相当。前方向も少しだけ削れる
    }

    // --- 速度ベクトルを再構成して位置を進める ------------------------------
    velX = sinYaw * vf + cosYaw * vl;
    velZ = cosYaw * vf - sinYaw * vl;
    x += velX * dt;
    z += velZ * dt;

    speed = vf;
    lateralSpeed = vl;
    slipAngle = vf > 0.5f ? std::atan2(vl, vf) : 0.0f;

    // --- ドリフトのスコア（角度 x 速度で加点、距離をチェーンとして数える） --
    if (drifting && driftAmount > 0.4f && vf > 5.0f) {
        driftChain += std::sqrt(velX * velX + velZ * velZ) * dt;
        driftScore += std::fabs(slipAngle) * vf * dt * 12.0f;
        if (driftChain > driftBestChain) {
            driftBestChain = driftChain;
        }
    } else if (!drifting) {
        driftChain = 0;
    }
}

void Car::update(float dt, const DriveInput& input, const Track& track) {
    // ドリフト中はカウンターステアが要るので、切れ角と応答を少し広げる。
    float steerMax = maxSteerAt(speed) * (drifting ? 1.3f : 1.0f);
    float target = clampf(input.steer, -1.0f, 1.0f) * steerMax;
    float step = kSteerRate * (drifting ? 1.35f : 1.0f) * dt;
    steerAngle += clampf(target - steerAngle, -step, step);
    steerAngle = clampf(steerAngle, -0.6f, 0.6f);

    if (std::fabs(speed) < 0.45f && input.brake > 0.5f && input.throttle < 0.1f) {
        brakeHold += dt;
    } else {
        brakeHold = 0;
    }
    bool reversing = brakeHold > 0.28f;

    float force = 0;
    float speedBefore = speed;
    if (input.brake > 0.05f && !reversing) {
        force -= input.brake * kBrake * (speed >= 0 ? 1.0f : -1.0f);
    } else if (reversing) {
        force -= input.brake * kReverseAccel;
    } else if (input.throttle > 0.0f && speed > -0.4f) {
        float ratio = clampf(speed / kMaxSpeed, 0.0f, 1.0f);
        force += input.throttle * kAccel * (1.0f - ratio * ratio);
    }
    force -= kDrag * speed * std::fabs(speed);
    if (std::fabs(speed) > 0.05f) {
        force -= kRolling * (speed > 0 ? 1.0f : -1.0f);
    }
    // 芝は加速が鈍く、速度も伸びない（横グリップの低下は stepDrift 側）。
    if (!onAsphalt) {
        if (force > 0) {
            force *= 0.55f;
        }
        if (std::fabs(speed) > 0.2f) {
            force -= 3.2f * (speed > 0 ? 1.0f : -1.0f);
        }
    }

    speed += force * dt;
    if (!reversing && input.brake > 0.05f) {
        if (speedBefore >= 0 && speed < 0) speed = 0;
        if (speedBefore <= 0 && speed > 0) speed = 0;
    }
    speed = clampf(speed, -kMaxReverse, kMaxSpeed);
    wheelSpin += speed / kWheelRadius * dt;

    stepDrift(dt, input);

    float preferS = ground.found ? ground.s : -1.0f;
    ground = track.query(x, z, y, preferS);
    if (!ground.found) {
        return;
    }

    const float wall = track.wallLateral();
    if (std::fabs(ground.lateral) > wall) {
        float sign = ground.lateral > 0 ? 1.0f : -1.0f;
        float overflow = std::fabs(ground.lateral) - wall;
        x -= ground.rightX * sign * overflow;
        z -= ground.rightZ * sign * overflow;
        // 壁の外へ向いている分だけ速度を落とす。コースに沿っているときは壁を擦るだけ。
        float facingOut = sign * (std::sin(yaw) * ground.rightX + std::cos(yaw) * ground.rightZ);
        if (facingOut > 0.15f) {
            float cap = 2.0f + 8.0f * (1.0f - facingOut);
            if (speed > cap) {
                speed = cap;
            }
        }
        // 速度ベクトル側も壁に合わせる。外向き成分を残すと、次のフレームも
        // 壁へ滑り続けてしまう。内向き・沿う向きの成分（立て直し）は残す。
        float outX = ground.rightX * sign;
        float outZ = ground.rightZ * sign;
        float outVel = velX * outX + velZ * outZ;
        if (outVel > 0) {
            velX -= outX * outVel;
            velZ -= outZ * outVel;
        }
        // speed の上限（上の cap）をベクトルへ反映し直し、前後・横成分を取り直す。
        {
            float fx = std::sin(yaw);
            float fz = std::cos(yaw);
            float rx = std::cos(yaw);
            float rz = -std::sin(yaw);
            float vf = velX * fx + velZ * fz;
            float vl = velX * rx + velZ * rz;
            if (vf > speed) {
                vf = speed;
            }
            velX = fx * vf + rx * vl;
            velZ = fz * vf + rz * vl;
            speed = vf;
            lateralSpeed = vl;
            slipAngle = vf > 0.5f ? std::atan2(vl, vf) : 0.0f;
        }
        ground.lateral = sign * wall;
    }

    float bankLimit = track.halfWidth + track.curbWidth;
    float bankLat = clampf(ground.lateral, -bankLimit, bankLimit);
    y = ground.centerY + bankLat * ground.cross;
    onAsphalt = std::fabs(ground.lateral) <= track.halfWidth + 0.15f;
}

void buildCarBody(CpuMesh& mesh) {
    const Vec3 red{0.72f, 0.08f, 0.09f};
    const Vec3 dark{0.06f, 0.06f, 0.07f};
    const Vec3 carbon{0.12f, 0.12f, 0.13f};
    mesh.addBox({-0.88f, 0.32f, -2.05f}, {0.88f, 0.62f, 2.05f}, red);
    mesh.addBox({-0.72f, 0.62f, -0.55f}, {0.72f, 1.05f, 1.05f}, red);
    mesh.addBox({-0.62f, 0.78f, -0.35f}, {0.62f, 1.02f, 0.9f}, dark);
    mesh.addBox({-0.7f, 0.32f, 1.7f}, {0.7f, 0.5f, 2.25f}, red);
    mesh.addBox({-0.95f, 0.26f, 1.95f}, {0.95f, 0.36f, 2.35f}, carbon);
    mesh.addBox({-0.86f, 0.85f, -2.2f}, {0.86f, 1.0f, -1.85f}, carbon);
    mesh.addBox({-0.15f, 0.95f, -2.05f}, {0.15f, 1.15f, -1.9f}, carbon);
}

void buildCarWheel(CpuMesh& mesh) {
    const int seg = 14;
    const float radius = 0.34f;
    const float halfW = 0.15f;
    const Vec3 tire{0.07f, 0.07f, 0.08f};
    const Vec3 rim{0.72f, 0.74f, 0.76f};
    for (int i = 0; i < seg; ++i) {
        float a0 = float(i) / float(seg) * 6.2831853f;
        float a1 = float(i + 1) / float(seg) * 6.2831853f;
        float c0 = std::cos(a0);
        float s0 = std::sin(a0);
        float c1 = std::cos(a1);
        float s1 = std::sin(a1);
        Vec3 normal = normalize(Vec3{0, c0 + c1, s0 + s1});
        Vec3 a{-halfW, c0 * radius, s0 * radius};
        Vec3 b{halfW, c0 * radius, s0 * radius};
        Vec3 c{halfW, c1 * radius, s1 * radius};
        Vec3 d{-halfW, c1 * radius, s1 * radius};
        mesh.addQuad(a, b, c, d, normal, tire);
        for (int side = -1; side <= 1; side += 2) {
            float x = float(side) * halfW;
            Vec3 nrm{float(side), 0, 0};
            Vec3 center{x, 0, 0};
            Vec3 p0{x, c0 * radius * 0.62f, s0 * radius * 0.62f};
            Vec3 p1{x, c1 * radius * 0.62f, s1 * radius * 0.62f};
            Vec3 o0{x, c0 * radius, s0 * radius};
            Vec3 o1{x, c1 * radius, s1 * radius};
            mesh.addQuad(o0, o1, p1, p0, nrm, tire);
            mesh.addTriangle(center, p0, p1, nrm, rim);
        }
    }
}

void buildCarShadow(CpuMesh& mesh) {
    const Vec3 ink{0.02f, 0.02f, 0.025f};
    mesh.addQuad({-1.05f, 0, -2.1f}, {1.05f, 0, -2.1f}, {1.05f, 0, 2.15f}, {-1.05f, 0, 2.15f}, {0, 1, 0}, ink);
}

Mat4 carModelMatrix(const Car& car, float pitch, float roll) {
    Vec3 forward = normalize(Vec3{
        std::sin(car.yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(car.yaw) * std::cos(pitch),
    });
    Vec3 right0{std::cos(car.yaw), 0, -std::sin(car.yaw)};
    Vec3 up0 = normalize(cross(forward, right0));
    Vec3 right = normalize(right0 * std::cos(roll) + up0 * std::sin(roll));
    Vec3 up = normalize(cross(forward, right));
    return basis({car.x, car.y, car.z}, right, up, forward);
}
