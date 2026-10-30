// ============================================================================
// soundmixer.cpp ― ソフトウェアミキサーの実装
// 音作りの数値 (音程の幅・音量の配分・時定数) は各ボイスの先頭の定数に
// まとめてある。音の印象を変えたいときはそこを触る。
// ============================================================================

#include "soundmixer.hpp"

#include <cmath>

namespace sound {

namespace {

constexpr float kTwoPi = 6.2831853f;

// 0..1 に収める。
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// 1 次ローパスフィルタの係数 (カットオフ周波数 hz)。
float lowpassCoef(float hz, float rate) { return 1.0f - std::exp(-kTwoPi * hz / rate); }

// 4 点エルミート補間。ループを小数位置で読むときの補間に使う。
// 直線補間より高域のザラつきが少なく、ピッチを大きく変えても音が濁りにくい。
float hermite(float xm1, float x0, float x1, float x2, float t) {
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}

// ループ素材の小数位置 pos を読み、L/R を返す (末尾は先頭へ回り込む)。
void readLoop(const Pcm& pcm, double pos, float& left, float& right) {
    const int n = pcm.frames();
    const int i0 = int(pos);
    const float t = float(pos - double(i0));
    const int im1 = (i0 - 1 + n) % n;
    const int i1 = (i0 + 1) % n;
    const int i2 = (i0 + 2) % n;
    const float* s = pcm.samples.data();
    left = hermite(s[im1 * 2], s[i0 * 2], s[i1 * 2], s[i2 * 2], t);
    right = hermite(s[im1 * 2 + 1], s[i0 * 2 + 1], s[i1 * 2 + 1], s[i2 * 2 + 1], t);
}

// 読み位置を進めてループ範囲 [0, n) に収める。
double advance(double pos, double step, int n) {
    pos += step;
    if (pos >= double(n)) pos -= double(n) * std::floor(pos / double(n));
    return pos;
}

// 上昇と下降で速さの違う平滑化 (音の立ち上がりは速く、消え際はゆっくり等)。
float follow(float value, float target, float upCoef, float downCoef) {
    return value + (target - value) * (target > value ? upCoef : downCoef);
}

}  // namespace

float smoothingCoef(float tauSec, float rate) {
    if (tauSec <= 0.0f || rate <= 0.0f) {
        return 1.0f;
    }
    return 1.0f - std::exp(-1.0f / (tauSec * rate));
}

// ============================================================================
// エンジン音
// ============================================================================
namespace {
// 音程: 録音ループを読む速さ。アイドルでは 0.62 倍、レッドゾーンで 2.05 倍。
// 実車の排気音は回転数にほぼ比例して高くなるため、回転数に対して直線で決める。
constexpr float kEngineIdleRate = 0.62f;
constexpr float kEngineRedRate = 2.05f;
// 平滑化の時定数 (秒)。回転数は素早く、アクセルの音色はやや遅く追う。
constexpr float kEngineRpmTau = 0.035f;
constexpr float kEngineThrottleTau = 0.07f;
constexpr float kEngineFadeInTau = 0.15f;   // 鳴り始め
constexpr float kEngineFadeOutTau = 0.30f;  // 止まるとき
// 音色: アクセルを踏むほど・回すほど明るく (高域が出る)、
// アクセルオフでは高回転でもこもった音 (オーバーラン) になる。
constexpr float kEngineToneMinHz = 650.0f;
constexpr float kEngineToneRangeHz = 6800.0f;
// 燃焼音 (4 気筒 = 1 回転に 2 回の爆発) の重ね具合。
constexpr float kFiringPerRev = 2.0f;
constexpr float kEngineGain = 0.55f;
}  // namespace

void EngineVoice::render(float* out, int frames, float outRate, const EngineSound& target) {
    if (loop_.empty()) {
        return;
    }
    const float envTarget = target.running ? 1.0f : 0.0f;
    if (!primed_) {
        // 初回は目標値から始める (0 rpm からせり上がる不自然な音を避ける)。
        rpmSm_ = clamp01(target.rpmNorm);
        hzSm_ = target.rpm / 60.0f * kFiringPerRev;
        throttleSm_ = clamp01(target.throttle);
        loadSm_ = clamp01(target.load);
        primed_ = true;
    }
    if (envTarget == 0.0f && env_ < 1e-5f) {
        env_ = 0.0f;
        return;  // 無音: 計算を省く
    }

    const float kRpm = smoothingCoef(kEngineRpmTau, outRate);
    const float kThrottle = smoothingCoef(kEngineThrottleTau, outRate);
    const float kEnvUp = smoothingCoef(kEngineFadeInTau, outRate);
    const float kEnvDown = smoothingCoef(kEngineFadeOutTau, outRate);
    const float rpmTarget = clamp01(target.rpmNorm);
    const float hzTarget = target.rpm / 60.0f * kFiringPerRev;
    const float throttleTarget = clamp01(target.throttle);
    const float loadTarget = clamp01(target.load);
    const float rateScale = float(loop_.rate) / outRate;  // 素材と出力のレート差
    const int n = loop_.frames();
    float toneCoef = lowpassCoef(kEngineToneMinHz, outRate);

    for (int i = 0; i < frames; ++i) {
        // --- パラメータを目標へ滑らかに寄せる -------------------------------
        rpmSm_ += (rpmTarget - rpmSm_) * kRpm;
        hzSm_ += (hzTarget - hzSm_) * kRpm;
        throttleSm_ += (throttleTarget - throttleSm_) * kThrottle;
        loadSm_ += (loadTarget - loadSm_) * kThrottle;
        env_ = follow(env_, envTarget, kEnvUp, kEnvDown);

        // 音色フィルタの係数は 32 サンプルに 1 回だけ計算する (十分滑らか)。
        if ((i & 31) == 0) {
            const float bright = 0.3f * rpmSm_ + 0.7f * loadSm_;
            toneCoef = lowpassCoef(kEngineToneMinHz + kEngineToneRangeHz * bright, outRate);
        }

        // --- 録音ループ: 回転数に比例した速さで読む = 音程が連続に変わる ----
        float left, right;
        readLoop(loop_, pos_, left, right);
        const float rate = kEngineIdleRate + (kEngineRedRate - kEngineIdleRate) * rpmSm_;
        pos_ = advance(pos_, double(rate * rateScale), n);
        // 2 段の 1 次ローパス (-12dB/oct) で音色を作る。
        lp1_[0] += (left - lp1_[0]) * toneCoef;
        lp1_[1] += (right - lp1_[1]) * toneCoef;
        lp2_[0] += (lp1_[0] - lp2_[0]) * toneCoef;
        lp2_[1] += (lp1_[1] - lp2_[1]) * toneCoef;
        // フィルタで削れた高域を少しだけ戻す (踏んでいるときの「張り」)。
        const float presence = 0.25f * loadSm_;
        float bodyL = lp2_[0] + (left - lp2_[0]) * presence;
        float bodyR = lp2_[1] + (right - lp2_[1]) * presence;

        // --- 燃焼音: 回転数に同期した倍音を足し、低回転の「ドロドロ感」を出す ---
        firePhase_ += hzSm_ / outRate;
        if (firePhase_ >= 1.0f) {
            firePhase_ -= std::floor(firePhase_);
            fireJitter_ = 1.0f + 0.25f * rng_.bipolar();  // 爆発ごとの強さのばらつき
        }
        const float ph = firePhase_ * kTwoPi;
        const float firing = (std::sin(ph) * 0.6f + std::sin(ph * 2.0f) * 0.3f + std::sin(ph * 3.0f) * 0.15f) *
                             fireJitter_;
        const float fireGain = (0.08f + 0.14f * loadSm_) * (1.2f - 0.7f * rpmSm_);

        // --- 音量: アイドル分 + アクセル分 + 回転分 ---------------------------
        const float volume = env_ * kEngineGain * (0.30f + 0.40f * loadSm_ + 0.30f * rpmSm_);
        out[i * 2] += (bodyL + firing * fireGain) * volume;
        out[i * 2 + 1] += (bodyR + firing * fireGain) * volume;
    }
}

// ============================================================================
// タイヤ (ドリフト) 音
// ============================================================================
namespace {
constexpr float kTireAttackTau = 0.045f;   // 滑り始めの立ち上がり
constexpr float kTireReleaseTau = 0.16f;   // グリップ回復時の消え方
constexpr float kTireSpeedTau = 0.10f;
constexpr float kTireBiteSec = 0.35f;      // 出だしの「キュッ」が続く目安
constexpr float kTireHeatStartSec = 1.5f;  // 長く滑ると音色が変わり始める秒数
constexpr float kTireHeatRangeSec = 4.0f;  // 変化しきるまでの秒数
constexpr float kTireLfoHz = 0.17f;        // 2 つの読み位置を行き来する速さ
constexpr float kTireGain = 0.85f;
}  // namespace

void TireVoice::render(float* out, int frames, float outRate, const TireSound& target) {
    if (loop_.empty()) {
        return;
    }
    const float depthTarget = clamp01(target.depth);
    if (depthTarget <= 0.0f && env_ < 1e-5f) {
        env_ = 0.0f;
        depthSm_ = 0.0f;
        return;  // 無音: 計算を省く (読み位置もそのまま保持)
    }

    // --- 継続時間から決まる性格 (フレーム内では一定とみなす) ---------------
    // bite: 滑り始めほど 1。出だしは鋭く高めに鳴く。
    // heat: 長く滑るほど 1。タイヤが熱を持ち、低めでザラついた音になる。
    const float bite = std::exp(-target.duration / kTireBiteSec);
    const float heat = clamp01((target.duration - kTireHeatStartSec) / kTireHeatRangeSec);

    const float kAttack = smoothingCoef(kTireAttackTau, outRate);
    const float kRelease = smoothingCoef(kTireReleaseTau, outRate);
    const float kSpeed = smoothingCoef(kTireSpeedTau, outRate);
    const float kJitter = smoothingCoef(0.06f, outRate);
    const float squealCoef = lowpassCoef(9000.0f - 4500.0f * heat, outRate);
    const float noiseCoef = lowpassCoef(900.0f, outRate);
    const float rateScale = float(loop_.rate) / outRate;
    const int n = loop_.frames();
    const double half = double(n) * 0.5;

    for (int i = 0; i < frames; ++i) {
        // --- パラメータ --------------------------------------------------
        depthSm_ = follow(depthSm_, depthTarget, kAttack, kRelease);
        speedSm_ += (target.speed - speedSm_) * kSpeed;
        const float speedN = clamp01((speedSm_ - 3.0f) / 22.0f);  // 約 11〜90 km/h で 0→1
        const float speedGain = std::sqrt(speedN);

        // ゆらぎ: 約 70ms ごとに新しい目標を選び、滑らかに追う。
        // 実際のスキール音は路面の細かな変化で音程と音量が揺れている。
        if (--jitterCountdown_ <= 0) {
            jitterCountdown_ = int(outRate * (0.05f + 0.04f * (rng_.bipolar() * 0.5f + 0.5f)));
            jitterPitchTarget_ = 0.018f * rng_.bipolar();
            jitterGainTarget_ = 0.12f * rng_.bipolar();
        }
        jitterPitch_ += (jitterPitchTarget_ - jitterPitch_) * kJitter;
        jitterGain_ += (jitterGainTarget_ - jitterGain_) * kJitter;

        // 音量: 深さに対してやや早めに立ち上がるカーブ。出だしは少し強い。
        const float levelTarget = std::pow(depthSm_, 0.8f) * speedGain * (1.0f + 0.3f * bite);
        env_ = follow(env_, levelTarget, kAttack, kRelease);

        // 音程: 速いほど高い / 深いほど低い / 出だしは高い / 熱で下がる。
        const float pitch = (0.86f + 0.24f * speedN) * (1.0f - 0.08f * depthSm_) * (1.0f + 0.08f * bite) *
                            (1.0f - 0.06f * heat) * (1.0f + jitterPitch_);

        // --- スキール音: 半周ずらした 2 つの読み位置を等パワーで行き来する ---
        // 1 つのループだけだと数秒ごとの繰り返しが耳につくため。
        float aL, aR, bL, bR;
        readLoop(loop_, pos_, aL, aR);
        readLoop(loop_, advance(pos_, half, n), bL, bR);
        pos_ = advance(pos_, double(pitch * rateScale), n);
        lfoPhase_ += kTireLfoHz / outRate;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
        const float mix = 0.5f + 0.5f * std::sin(lfoPhase_ * kTwoPi);  // 0..1
        const float gA = std::cos(mix * 1.5707963f);
        const float gB = std::sin(mix * 1.5707963f);
        squealLp_[0] += (aL * gA + bL * gB - squealLp_[0]) * squealCoef;
        squealLp_[1] += (aR * gA + bR * gB - squealLp_[1]) * squealCoef;

        // --- 路面ノイズ: タイヤが路面を擦る「ザー」という低めの成分 -----------
        noiseLp1_ += (rng_.bipolar() - noiseLp1_) * noiseCoef;
        noiseLp2_ += (noiseLp1_ - noiseLp2_) * noiseCoef;
        const float scrub = noiseLp2_ * 2.2f * depthSm_ * speedN * (0.6f + 0.4f * heat);

        const float volume = env_ * kTireGain * (1.0f + jitterGain_);
        out[i * 2] += (squealLp_[0] + scrub) * volume;
        out[i * 2 + 1] += (squealLp_[1] + scrub) * volume;
    }
}

// ============================================================================
// 合図音
// ============================================================================
void CueVoice::trigger(SoundCue cue) {
    for (Beep& beep : beeps_) {
        if (!beep.active) {
            beep.active = true;
            beep.time = 0.0f;
            beep.phase = 0.0f;
            // 3・2・1 は低く短く、GO は 1 オクターブ上で長く (定番の合図)。
            beep.freq = cue == SoundCue::CountdownGo ? 1320.0f : 660.0f;
            beep.length = cue == SoundCue::CountdownGo ? 0.60f : 0.18f;
            return;
        }
    }
}

void CueVoice::render(float* out, int frames, float outRate) {
    const float dt = 1.0f / outRate;
    for (Beep& beep : beeps_) {
        if (!beep.active) {
            continue;
        }
        for (int i = 0; i < frames; ++i) {
            // 立ち上がり 5ms、最後の 40ms でフェードアウト (クリック音防止)。
            float env = clamp01(beep.time / 0.005f) * clamp01((beep.length - beep.time) / 0.04f);
            env *= 0.75f + 0.25f * std::exp(-beep.time * 6.0f);  // 頭を少し強く
            const float ph = beep.phase * kTwoPi;
            const float v = (std::sin(ph) + 0.25f * std::sin(ph * 2.0f)) * env * 0.28f;
            out[i * 2] += v;
            out[i * 2 + 1] += v;
            beep.phase += beep.freq * dt;
            if (beep.phase >= 1.0f) beep.phase -= 1.0f;
            beep.time += dt;
            if (beep.time >= beep.length) {
                beep.active = false;
                break;
            }
        }
    }
}

}  // namespace sound

// ============================================================================
// ミキサー本体
// ============================================================================
void SoundMixer::setEngine(const EngineSound& state) {
    engineRpm_.store(state.rpm);
    engineRpmNorm_.store(state.rpmNorm);
    engineThrottle_.store(state.throttle);
    engineLoad_.store(state.load);
    engineRunning_.store(state.running);  // 最後に書く (他の値が揃ってから効く)
}

void SoundMixer::setTires(const TireSound& state) {
    tireDuration_.store(state.duration);
    tireSpeed_.store(state.speed);
    tireDepth_.store(state.depth);
}

void SoundMixer::playCue(SoundCue cue) {
    if (cue == SoundCue::CountdownGo) {
        pendingGos_.fetch_add(1);
    } else {
        pendingTicks_.fetch_add(1);
    }
}

void SoundMixer::render(float* out, int frames) {
    for (int i = 0; i < frames * 2; ++i) {
        out[i] = 0.0f;
    }

    // --- ゲーム側の最新状態を受け取る --------------------------------------
    EngineSound engine;
    engine.running = engineRunning_.load();
    engine.rpm = engineRpm_.load();
    engine.rpmNorm = engineRpmNorm_.load();
    engine.throttle = engineThrottle_.load();
    engine.load = engineLoad_.load();
    TireSound tire;
    tire.depth = tireDepth_.load();
    tire.duration = tireDuration_.load();
    tire.speed = tireSpeed_.load();
    for (int n = pendingTicks_.exchange(0); n > 0; --n) cues_.trigger(SoundCue::CountdownTick);
    for (int n = pendingGos_.exchange(0); n > 0; --n) cues_.trigger(SoundCue::CountdownGo);

    // --- 各ボイスを加算で書き込む ------------------------------------------
    engine_.render(out, frames, outRate_, engine);
    tire_.render(out, frames, outRate_, tire);
    cues_.render(out, frames, outRate_);

    // --- 全体音量とソフトクリップ (重なって大きくなっても割れないように) ---
    const float master = master_.load();
    for (int i = 0; i < frames * 2; ++i) {
        float v = out[i] * master;
        const float a = std::fabs(v);
        if (a > 0.8f) {
            // 0.8 を超えた分だけ tanh でなだらかに頭打ちにする (最大 1.0)。
            v = (v < 0 ? -1.0f : 1.0f) * (0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f));
        }
        out[i] = v;
    }

    engineLevel_.store(engine_.level());
    tireLevel_.store(tire_.level());
}
