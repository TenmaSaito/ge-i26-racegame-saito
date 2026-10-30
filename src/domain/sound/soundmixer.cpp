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
// タイヤ (ドリフト) 音 ― 合成
//
// 実際のスキール音は、タイヤのゴムが路面に「張り付く→滑る」を毎秒数十回
// 繰り返すことで出る、揺らぎの大きい倍音の多い音。ここではそれを 3 層で作る:
//   1. 鳴き: 基本周波数 (約 0.5〜1kHz) と倍音。音程はゆっくりランダムに揺れ、
//      張り付き/滑りの繰り返し (20〜50Hz) で音量が細かく断続する。
//   2. 擦れ: ノイズを鳴きの 2.3 倍あたりで帯域通過させた「シャー」という成分。
//   3. ザラつき: 低域だけ残したノイズ (路面を擦るゴロゴロ)。
// 深く滑るほど低く太く (断続は減る)、浅いと高めでチリチリと断続する。
// 速いほど高く大きい。滑り始めは鋭く、長く滑るとタイヤが熱を持ったように
// 低めで擦れの多い音になる。
// ============================================================================
namespace {
constexpr float kTireAttackTau = 0.06f;    // 滑り始めの立ち上がり
constexpr float kTireReleaseTau = 0.22f;   // グリップ回復時の消え方
constexpr float kTireSpeedTau = 0.12f;
constexpr float kTireBiteSec = 0.4f;       // 出だしの「キュッ」が続く目安
constexpr float kTireHeatStartSec = 1.5f;  // 長く滑ると音色が変わり始める秒数
constexpr float kTireHeatRangeSec = 4.0f;  // 変化しきるまでの秒数
constexpr float kTireBaseHz = 560.0f;      // 低速での鳴きの高さ
constexpr float kTireSpeedHz = 420.0f;     // 速度で上がる分
constexpr float kTireGain = 0.30f;         // 全体の音量 (エンジン音より一歩引いた大きさ)
constexpr int kTireBlock = 16;             // 係数をまとめて計算するサンプル数
}  // namespace

float BandPass::process(float input, float freq, float q, float rate) {
    // Chamberlin 型の状態変数フィルタ。f は 32 サンプルごとに変えても滑らか。
    const float f = 2.0f * std::sin(3.14159265f * std::min(freq, rate * 0.2f) / rate);
    low += f * band;
    const float high = input - low - q * band;
    band += f * high;
    return band;
}

void TireVoice::render(float* out, int frames, float outRate, const TireSound& target) {
    const float depthTarget = clamp01(target.depth);
    if (depthTarget <= 0.0f && env_ < 1e-5f) {
        env_ = 0.0f;
        depthSm_ = 0.0f;
        return;  // 無音: 計算を省く
    }

    // --- 継続時間から決まる性格 (この呼び出しの間は一定とみなす) ----------
    const float bite = std::exp(-target.duration / kTireBiteSec);
    const float heat = clamp01((target.duration - kTireHeatStartSec) / kTireHeatRangeSec);

    const float kAttack = smoothingCoef(kTireAttackTau, outRate);
    const float kRelease = smoothingCoef(kTireReleaseTau, outRate);
    const float kSpeed = smoothingCoef(kTireSpeedTau, outRate);
    const float kWobble = smoothingCoef(0.05f, outRate);
    const float kChop = smoothingCoef(0.15f, outRate);
    const float toneCoef = lowpassCoef(4500.0f - 1500.0f * heat, outRate);
    const float rumbleCoef = lowpassCoef(300.0f, outRate);

    float freq = kTireBaseHz;
    float levelTarget = 0.0f;
    float chopDepth = 0.3f;
    float speedN = 0.0f;
    for (int i = 0; i < frames; ++i) {
        // --- パラメータ --------------------------------------------------
        depthSm_ = follow(depthSm_, depthTarget, kAttack, kRelease);
        speedSm_ += (target.speed - speedSm_) * kSpeed;

        // ランダムな音程の揺らぎ: 40〜90ms ごとに新しい目標を選び、滑らかに追う。
        if (--wobbleCountdown_ <= 0) {
            wobbleCountdown_ = int(outRate * (0.04f + 0.05f * (rng_.bipolar() * 0.5f + 0.5f)));
            wobbleTarget_ = 0.025f * rng_.bipolar();
            chopRateTarget_ = 35.0f + 13.0f * rng_.bipolar();  // 22〜48Hz
        }
        wobble_ += (wobbleTarget_ - wobble_) * kWobble;
        chopRate_ += (chopRateTarget_ - chopRate_) * kChop;

        // 重い計算 (べき乗・フィルタ係数) は kTireBlock サンプルに 1 回。
        if (i % kTireBlock == 0) {
            speedN = clamp01((speedSm_ - 4.0f) / 24.0f);  // 約 14〜100 km/h で 0→1
            // 音量: 浅い滑りでは小さく、深くなるほど急に大きくなる (常に鳴りっぱなしにしない)。
            levelTarget = std::pow(depthSm_, 1.4f) * std::sqrt(speedN) * (1.0f + 0.25f * bite);
            // 鳴きの高さ: 速いほど高い / 深いほど低い / 出だしは高い / 熱で下がる。
            freq = (kTireBaseHz + kTireSpeedHz * speedN) * (1.0f - 0.10f * depthSm_) *
                   (1.0f + 0.07f * bite) * (1.0f - 0.07f * heat);
            // 断続の深さ: 浅い滑りほどチリチリと途切れる。
            chopDepth = 0.25f + 0.35f * (1.0f - depthSm_);
        }
        env_ = follow(env_, levelTarget, kAttack, kRelease);

        // --- 1. 鳴き (倍音) ------------------------------------------------
        vibratoPhase_ += 7.3f / outRate;
        if (vibratoPhase_ >= 1.0f) vibratoPhase_ -= 1.0f;
        const float pitch = freq * (1.0f + wobble_ + 0.006f * std::sin(vibratoPhase_ * kTwoPi));
        phase_ += pitch / outRate;
        if (phase_ >= 1.0f) phase_ -= std::floor(phase_);
        const float ph = phase_ * kTwoPi;
        const float squeal = std::sin(ph) + 0.42f * std::sin(2.0f * ph + 0.7f) +
                             0.18f * std::sin(3.0f * ph + 1.9f) + 0.08f * std::sin(5.0f * ph);
        chopPhase_ += chopRate_ / outRate;
        if (chopPhase_ >= 1.0f) chopPhase_ -= 1.0f;
        const float chop = 1.0f - chopDepth * (0.5f + 0.5f * std::sin(chopPhase_ * kTwoPi));

        // --- 2. 擦れ (帯域ノイズ、左右で別のノイズ) / 3. ザラつき (低域ノイズ) ---
        const float hissL = hiss_[0].process(rng_.bipolar(), pitch * 2.3f, 0.35f, outRate);
        const float hissR = hiss_[1].process(rng_.bipolar(), pitch * 2.3f, 0.35f, outRate);
        rumble1_ += (rng_.bipolar() - rumble1_) * rumbleCoef;
        rumble2_ += (rumble1_ - rumble2_) * rumbleCoef;
        const float rumble = rumble2_ * 1.6f * depthSm_ * speedN;
        const float hissGain = 0.45f + 0.35f * heat;

        // 鳴きの角を少し丸める (熱で暗くなる)。
        tone_[0] += (squeal * chop * 0.55f - tone_[0]) * toneCoef;
        tone_[1] = tone_[0];

        const float volume = env_ * kTireGain;
        out[i * 2] += (tone_[0] + hissL * hissGain + rumble) * volume;
        out[i * 2 + 1] += (tone_[1] + hissR * hissGain + rumble) * volume;
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
