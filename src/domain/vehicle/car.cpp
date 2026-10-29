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

float maxSteerAt(float speed) {
    return 0.46f / (1.0f + std::fabs(speed) * 0.085f);
}

}  // namespace

// ドリフトの調整値の実体。既定値は DriftTuning（car.hpp）の初期化子。
// 実行時の変更はチューニングパネル（main.cpp）からこの変数を書き換える。
DriftTuning driftTuning;

void Car::reset(const Track& track) {
    x = track.spawnX;
    y = track.spawnY;
    z = track.spawnZ;
    yaw = track.spawnYaw;
    speed = 0;
    steerAngle = 0;
    steerSm = 0;
    driftLockout = 0;
    wheelSpin = 0;
    brakeHold = 0;
    velX = 0;
    velZ = 0;
    lateralSpeed = 0;
    slipAngle = 0;
    drifting = false;
    driftAmount = 0;
    driftCommand = 0;
    driftHold = 0;
    driftCalm = 0;
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
    // きっかけは 2 つ（どちらも意図的な入力のみ。自動で入るフリックは廃止）。
    //   1. ドリフトボタン（Space / Shift、パッド B / LB / RB）を押しながら切る
    //   2. 右スティック（ツインスティック）: 切った向きに車体だけを回して滑らせる
    // ドリフト中は横グリップが driftTuning.gripDrift まで落ち、ステア（または右スティック）が
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
    // マウスはツインスティックと別チャンネルにする。スティックのような
    // 「0.45 を超えたら主役交代」という崖があると、効かない→突然奪う、
    // という怪しい挙動になるため。マウスは小さいデッドゾーンから比例し、
    // 目標スリップ角へ加算方式で効かせる（下の targetSlip 合成を参照）。
    const float mouseIn = clampf(input.mouseDrift, -1.0f, 1.0f);
    const bool mouseActive = std::fabs(mouseIn) > 0.03f;
    // 自動ドリフト（フリック: 大舵+アクセルで勝手に入る）は廃止した
    // （ユーザー指示）。ドリフトは必ず意図的な入力（ボタン+ステア /
    // ツインスティック）でのみ始まる。
    const bool wantDrift = (input.drift && std::fabs(steerIn) > 0.2f) || twinActive;
    // 進入の鋭さ。ツインスティックは「倒した分だけ角度」という直接操作なので、
    // 鋭さが最低でも応答が鈍くなりすぎないよう下限を設ける（ボタン/キー/マウス
    // 経由の進入は設定どおり穏やかになる）。
    const float entryEff = twinActive ? std::max(driftTuning.entrySharp, 0.4f)
                                      : driftTuning.entrySharp;
    // ドリフト系の入力が何かあるか（ボタンだけ押している等も含む）。長い
    // コーナーでは必要なステアが小さく wantDrift が切れがちで、そのたびに
    // ドリフトが終わって「押して離して」の繰り返しになっていた。そのため
    // 維持の判定はこちらを使い、ドリフトを「角度をダイヤルで調整できる
    // 粘る状態」にする（入口の判定は従来どおり wantDrift）。
    // マウスは開始のきっかけにはならないが、ドリフト中にずらしていれば
    // 維持の入力として数える（スティックを倒し続けているのと同じ）。
    const bool anyDriftInput = input.drift || twinActive || (drifting && mouseActive);
    if (wantDrift) {
        driftHold = 0.4f;  // 入力が一瞬途切れても終わらない猶予
    } else {
        driftHold = std::max(0.0f, driftHold - dt);
    }
    // 操作の主役（右スティック優先）。キーボードはオン/オフしかないので、そのまま
    // 使うと目標角度が跳ねてスピンしやすい。コマンドを平滑化して、デジタル入力
    // でも「浅く入って深める」ができるようにする（アナログ入力にはほぼ影響なし）。
    // ドリフト中は、ボタンを押している限り小さいステアにも追随する。
    const float rawCommand = twinActive ? twinIn : steerIn;
    const float commandTarget = (wantDrift || (drifting && anyDriftInput)) ? rawCommand : 0.0f;
    // コマンドの立ち上がり速度も進入の鋭さで変わる（鋭い車ほど即座に全開）。
    const float commandRate = 3.5f + 4.5f * entryEff;
    driftCommand += clampf(commandTarget - driftCommand, -commandRate * dt, commandRate * dt);
    const float command = driftCommand;
    // 目標スリップ角は操作と逆符号になる:
    // 右へ切ると車体が右へ回り、速度は慣性で左に残るのでスリップ角は負。
    // 大きさはコマンドに比例（倒した分だけ角度が付く）。さらに高速では
    // 上限を絞る（速いコーナーで浅く安定して流せるように）。
    const float velMag = std::sqrt(velX * velX + velZ * velZ);
    const float speedCap = clampf(26.0f / std::max(velMag, 10.0f), 0.45f, 1.0f);
    float targetSlip =
        -(command > 0 ? 1.0f : -1.0f) * driftTuning.maxSlip *
        std::pow(std::fabs(command), 0.8f) * speedCap;
    // マウスの合成: 同じ向きなら「より深い方」を採用（キー全開＋マウスで
    // 二重に効くことはない）、逆向きなら打ち消し合う（カウンターとして
    // 効く）。どちらも連続量なので、境目で挙動が跳ねることはない。
    if (mouseActive && (drifting || wantDrift)) {
        const float mouseTarget =
            -(mouseIn > 0 ? 1.0f : -1.0f) * driftTuning.maxSlip *
            std::pow(std::fabs(mouseIn), 0.8f) * speedCap;
        const bool sameSide = (mouseTarget > 0.0f) == (targetSlip > 0.0f);
        if (sameSide || std::fabs(targetSlip) < 0.01f) {
            if (std::fabs(mouseTarget) > std::fabs(targetSlip)) {
                targetSlip = mouseTarget;
            }
        } else {
            targetSlip += mouseTarget;
        }
    }

    if (driftLockout > 0.0f) driftLockout -= dt;
    if (!drifting && wantDrift && speed > driftTuning.minSpeed && driftLockout <= 0.0f) {
        drifting = true;
    }
    if (drifting && speed < 3.5f) {
        drifting = false;
    }
    // 終了判定: 入力が全く無い状態で滑りが収まったら終わる。ボタン保持中は
    // 終わらない（角度はコマンドで調整する）。ただし保持中でも、滑りが
    // ほぼ無くコマンドも無い状態が続けば自然に終わる（直線でベタつかない）。
    if (std::fabs(slipAngle) < 0.08f && std::fabs(command) < 0.05f) {
        driftCalm += dt;
    } else {
        driftCalm = 0;
    }
    if (drifting && !anyDriftInput && driftHold <= 0.0f &&
        std::fabs(slipAngle) < 0.10f && std::fabs(lateralSpeed) < 1.5f) {
        drifting = false;
    }
    if (drifting && anyDriftInput && !twinActive && !mouseActive && driftCalm > 0.6f) {
        drifting = false;
    }
    // グリップ量は急に切り替えず滑らかに移す。入るときの速さは進入の鋭さで
    // 変える（戻るときは従来通り少し遅く）。
    const float amountTarget = drifting ? 1.0f : 0.0f;
    const float amountRate = drifting ? (1.7f + 3.1f * entryEff) : 1.8f;
    driftAmount += clampf(amountTarget - driftAmount, -amountRate * dt, amountRate * dt);

    // --- ヨーレート --------------------------------------------------------
    float yawRate = 0;
    if (std::fabs(speed) > 0.05f) {
        yawRate = (speed / kWheelbase) * std::tan(steerAngle);
    }
    float slow = clampf(1.0f - std::fabs(speed) / 8.0f, 0.0f, 1.0f);
    yawRate += steerAngle * 0.9f * slow;
    // ドリフト中はステアによる回頭を弱める。コーナーではステアを切り続ける
    // ため、そのままだとステアのヨーがドリフト側の角度制御と喧嘩して、
    // 角度が浅いところで釣り合ってしまう（実測: 目標27度に対し12度）。
    // Inertial Drift が「Lスティック=進行、Rスティック=角度」と分けている
    // のと同じで、滑っている間は角度の主役をドリフト入力に渡す。
    yawRate *= (1.0f - 0.5f * driftAmount);
    if (driftAmount > 0.01f && (velX != 0.0f || velZ != 0.0f)) {
        const float velAngle = std::atan2(velX, velZ);  // 進行方向（yaw と同じ測り方）
        if (anyDriftInput || driftHold > 0.0f) {
            // 目標スリップ角になる車体の向きへ回す追加ヨー。進入の鋭さが低いと
            // ゲインも上限も下がり、車体が一気に回らず角度がじわりと付く
            // （鋭い設定では Inertial Drift のように切った瞬間に跳ねる）。
            const float yawErr = wrapPi((velAngle - targetSlip) - yaw);
            const float yawGain = 2.0f + 1.4f * entryEff;
            const float yawClamp = 0.95f + 0.85f * entryEff;
            // フィードフォワード: グリップは速度の向きを車体へ寄せてスリップを
            // 常に消そうとするので、比例制御だけだと目標角まで届かず手前で
            // 釣り合ってしまう（実測: 目標39度に対し約20度で頭打ち。特に
            // ステア 0 でマウス/スティックだけで保つ場合に効かない）。
            // 「目標角を保つのに必要なヨーレート = グリップ x 目標角」を先に
            // 足しておくと、釣り合い点が目標角そのものになる。近づき方自体は
            // ゲインと上限で決まるため、穏やかな進入の性格は変わらない。
            float gripNow = driftTuning.gripNormal +
                            (driftTuning.gripDrift - driftTuning.gripNormal) * driftAmount;
            gripNow /= std::max(0.05f, driftTuning.inertia);
            if (!onAsphalt) {
                gripNow *= 0.55f;
            }
            const float yawFeedForward = -gripNow * targetSlip;
            yawRate += clampf(yawErr * yawGain + yawFeedForward, -yawClamp, yawClamp) * driftAmount;
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
    // 駆動力の配分。通常は車体前方へ足すが、ドリフト中は半分を進行方向
    // （速度ベクトルの向き）へ回す。滑りながらでも前に進み続け、ドリフト
    // のままコーナーを抜けていく動きにする（車体前方だけだと、角度が
    // 深いほど駆動力が進みに変わらない）。
    const float alongVel = drifting ? 0.5f * driftAmount : 0.0f;
    vf += driveDelta * (1.0f - alongVel);
    if (alongVel > 0.0f && driveDelta > 0.0f) {
        const float magV = std::sqrt(vf * vf + vl * vl);
        if (magV > 1.0f) {
            const float push = driveDelta * alongVel;
            vf += push * vf / magV;
            vl += push * vl / magV;
        }
    }

    // --- グリップ: 速度の向きを車体前方へ回す ------------------------------
    // 横速度をそのまま捨てるモデルだと、滑るほど運動エネルギーを失って失速
    // する（実測: アクセル全開のドリフト1.5秒で 120→57km/h、気持ちよくない）。
    // 頭文字Dアーケードのドリフトは「速度を殺さず向きを変える」のが芯なので、
    // グリップは速度ベクトルの大きさを保ったままスリップ角を指数減衰させる
    // 回転として効かせる。減速は driftDecel と通常の抗力・抵抗だけが担う。
    // 立ち上がりでは横滑り分が前方向へ変換されるため、自然に加速感が出る。
    // 慣性（inertia）が大きいほど回転が弱まり、速度ベクトルが向きを保つ。
    float grip = driftTuning.gripNormal + (driftTuning.gripDrift - driftTuning.gripNormal) * driftAmount;
    grip /= std::max(0.05f, driftTuning.inertia);
    if (!onAsphalt) {
        grip *= 0.55f;  // 芝はもともと滑る
    }
    float mag = std::sqrt(vf * vf + vl * vl);
    if (vf > 0.5f && mag > 0.01f) {
        float turned = std::atan2(vl, vf) * std::exp(-grip * dt);
        vf = mag * std::cos(turned);
        vl = mag * std::sin(turned);
    } else {
        vl *= std::exp(-grip * dt);  // 後進・極低速時は従来通り横だけ消す
    }
    if (drifting) {
        // ドリフト中の減速。ボタン・スティックを問わず、滑っている間ずっと効く。
        float keep = std::exp(-driftTuning.driftDecel * driftAmount * dt);
        vf *= keep;
        vl *= keep;
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
    // キーボードのデジタル入力は段差がそのままヨーに伝わってカクつくので、
    // 入力自体をなだらかにする。ドリフト中はカウンターを速く効かせたいので速め。
    {
        float steerRate = drifting ? 8.0f : 5.0f;
        steerSm += clampf(clampf(input.steer, -1.0f, 1.0f) - steerSm,
                          -steerRate * dt, steerRate * dt);
    }
    float steerMax = maxSteerAt(speed) * (drifting ? 1.3f : 1.0f) * steerMul;
    float target = steerSm * steerMax;
    float step = kSteerRate * (drifting ? 1.35f : 1.0f) * steerMul * dt;
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
        float ratio = clampf(speed / (kMaxSpeed * topSpeedMul), 0.0f, 1.0f);
        force += input.throttle * kAccel * powerMul * (1.0f - ratio * ratio);
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

    wallContact = 0;
    wallImpact = 0;
    const float wall = track.wallLateral();
    if (std::fabs(ground.lateral) > wall) {
        wallContact = 1;
        wallSide = ground.lateral > 0 ? 1.0f : -1.0f;
        float sign = ground.lateral > 0 ? 1.0f : -1.0f;
        float overflow = std::fabs(ground.lateral) - wall;
        x -= ground.rightX * sign * overflow;
        z -= ground.rightZ * sign * overflow;
        // 壁の外へ向いている分だけ速度を落とす。コースに沿っているときは壁を擦るだけ。
        float facingOut = sign * (std::sin(yaw) * ground.rightX + std::cos(yaw) * ground.rightZ);
        if (facingOut > 0.15f) {
            float cap = 2.0f + 8.0f * (1.0f - facingOut);
            if (speed > cap) {
                // 削られた速度の大きさを衝突の勢いとして記録する (火花用)。
                wallImpact = clampf((speed - cap) / 18.0f, 0.0f, 1.0f);
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
        // --- 壁ずり対策 -----------------------------------------------------
        // 強く当たったフレームは、壁に沿う成分も含めて速度を落とす（軽く
        // 擦るだけなら残す）。さらに、接触中に進行方向と車体の向きが大きく
        // ずれているときは車体を進行方向へ少し回して壁に沿わせる。そのまま
        // だと、回転グリップが速度を車体前方へ戻そうとし、壁がそれを削る
        // 均衡で「横向きのまま壁を滑り続ける」状態になってしまうため。
        if (outVel > 4.0f) {
            velX *= 0.55f;
            velZ *= 0.55f;
            // 壁に強く当たったらドリフトは即終了。グリップを取り戻さないと
            // ドリフトのまま壁に張り付いて動けなくなるため、遷移量も
            // 一気に下げ、少しの間は再突入も止める。
            if (drifting || driftAmount > 0.05f) {
                drifting = false;
                driftAmount = std::min(driftAmount, 0.1f);
                driftCommand = 0;
                driftHold = 0;
                driftLockout = 0.6f;
            }
        }
        float velMag = std::sqrt(velX * velX + velZ * velZ);
        if (velMag > 1.0f) {
            float dyaw = wrapPi(std::atan2(velX, velZ) - yaw);
            if (std::fabs(dyaw) > 0.10f) {
                yaw += clampf(dyaw, -2.2f * dt, 2.2f * dt);
            }
        }
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
    // 高さは路面へ即座に貼り付けない。最寄り区間の切り替わりで中心高が
    // 段差状に飛ぶ坂があり、そのまま代入すると上下にガクつくので、
    // 短い時定数（約 45ms）で追わせて段差だけを均す。
    {
        // 路面の高さ（中心高＋バンク）。
        float targetY = ground.centerY + bankLat * ground.cross;
        float k = 1.0f - std::exp(-22.0f * dt);
        // 段差が大きい場所（標高データの継ぎ目など）では、指数追随だけだと
        // 1 フレームで大きく動くので、上下速度にも上限を付けて数フレームに分ける。
        // ※重力で浮かせる仕様は一度入れたが、接地感が変わるため撤回した
        //   (2026-10-29 本人判断: 浮遊なしの挙動が好み)。
        y += clampf((targetY - y) * k, -18.0f * dt, 18.0f * dt);
    }
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

// 車種テーブル (kCarSpecs) は constexpr として car.hpp 側に移した。

