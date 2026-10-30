#pragma once
// ============================================================================
// soundmixer.hpp ― ソフトウェアミキサー (エンジン音・タイヤ音・合図音の合成)
// 役割: ゲームから届く「音の状態」(soundparams.hpp) を、実際の波形へ変える。
//       出力先 (SDL / Web Audio / Android など) は知らず、呼ばれたら
//       float のステレオ波形を書き出すだけ。音声デバイスとの接続は
//       platform 層 (audio.cpp) の仕事。
//
// しくみの概要:
//   - パラメータは「目標値」として受け取り、音声側で 1 サンプルごとに
//     滑らかに目標へ寄せる。ゲームの 60Hz 更新がそのまま音に出ると
//     段々 (ジッパーノイズ) になるため。
//   - エンジン音は録音ループを「回転数に比例した速さ」で読み出して
//     音程を連続的に変える (段階切替なし)。アクセルで音色の明るさと
//     音量、回転数に同期した燃焼音 (倍音) を重ねて厚みを出す。
//   - タイヤ音 (ドリフトのスキール音) は素材を使わず合成する。タイヤが路面で
//     「張り付く/滑る」を細かく繰り返して鳴る音を、揺らぎのある倍音 (鳴き) +
//     帯域を絞ったノイズ (擦れ) + 低いノイズ (路面のザラつき) で作る。
//     ループの継ぎ目が無いので、何秒滑っても同じ音の繰り返しにならない。
//     滑りの深さ・継続時間・速度で音量/音程/音色が変わる。
//
// スレッド: set* / playCue はゲーム側スレッド、render は音声スレッドから
//   呼ばれてよい (受け渡しは atomic)。ブラウザ版は同じスレッドで動く。
// 依存: 標準ライブラリと pcm.hpp / soundparams.hpp のみ。
// ============================================================================

#include "pcm.hpp"
#include "soundparams.hpp"

#include <atomic>
#include <cstdint>

namespace sound {

// 1 次の平滑化係数 (時定数 tauSec、サンプリングレート rate)。
// value += (target - value) * coef を毎サンプル行うと、tauSec で約 63% 近づく。
float smoothingCoef(float tauSec, float rate);

// 軽量な疑似乱数 (xorshift32)。ゆらぎとノイズ用で、品質より速さ優先。
class Random {
public:
    explicit Random(uint32_t seed) : state_(seed ? seed : 1u) {}
    // -1..1 の一様乱数
    float bipolar() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return float(state_ & 0xFFFFFF) / float(0x7FFFFF) - 1.0f;
    }

private:
    uint32_t state_;
};

// ----------------------------------------------------------------------------
// エンジン音のボイス
// ----------------------------------------------------------------------------
class EngineVoice {
public:
    void setLoop(Pcm loop) { loop_ = std::move(loop); }
    bool ready() const { return !loop_.empty(); }
    // out (ステレオ交互) へ加算で書く。target は最新のエンジン状態。
    void render(float* out, int frames, float outRate, const EngineSound& target);
    float level() const { return env_; }

private:
    Pcm loop_;              // 継ぎ目処理済みのエンジン音ループ
    double pos_ = 0.0;      // ループ上の読み位置 (フレーム、小数あり)
    float rpmSm_ = 0.0f;    // 平滑化した正規化回転数 0..1
    float hzSm_ = 0.0f;     // 平滑化した燃焼周波数 (Hz)
    float throttleSm_ = 0.0f;
    float loadSm_ = 0.0f;
    float env_ = 0.0f;      // 全体の音量エンベロープ (鳴り始め/終わりのフェード)
    float lp1_[2] = {0, 0}; // 音色フィルタ (1 段目、L/R)
    float lp2_[2] = {0, 0}; // 音色フィルタ (2 段目、L/R)
    float firePhase_ = 0.0f;   // 燃焼音の位相 0..1
    float fireJitter_ = 1.0f;  // 燃焼 1 回ごとの強さのばらつき
    Random rng_{0x1234567u};
    bool primed_ = false;   // 最初の 1 回は平滑化せず目標値から始める
};

// 2 次の状態変数フィルタ (帯域通過)。ノイズから特定の高さの成分だけを取り出す。
struct BandPass {
    float low = 0.0f, band = 0.0f;
    // input を通し、中心 freq (Hz)・鋭さ q (大きいほど狭い) の帯域成分を返す。
    float process(float input, float freq, float q, float rate);
};

// ----------------------------------------------------------------------------
// タイヤ (ドリフト) 音のボイス。素材を使わない合成音。
// ----------------------------------------------------------------------------
class TireVoice {
public:
    void render(float* out, int frames, float outRate, const TireSound& target);
    float level() const { return env_; }

private:
    float depthSm_ = 0.0f;    // 平滑化した滑りの深さ
    float speedSm_ = 0.0f;    // 平滑化した車速 (m/s)
    float env_ = 0.0f;        // 音量エンベロープ
    float phase_ = 0.0f;      // 鳴き (倍音) の位相 0..1
    float chopPhase_ = 0.0f;  // 張り付き/滑りの繰り返し (細かい断続) の位相 0..1
    float wobble_ = 0.0f, wobbleTarget_ = 0.0f;  // 音程の揺らぎ (ランダム、ゆっくり)
    float chopRate_ = 30.0f, chopRateTarget_ = 30.0f;  // 断続の速さ (Hz)
    int wobbleCountdown_ = 0;                     // 次の揺らぎまでのサンプル数
    float vibratoPhase_ = 0.0f;                   // 細かいビブラートの位相
    BandPass hiss_[2];        // 擦れ (L/R で別のノイズを通して広がりを出す)
    float rumble1_ = 0.0f, rumble2_ = 0.0f;  // 路面のザラつき (低域ノイズ)
    float tone_[2] = {0, 0};  // 鳴きの角を丸めるフィルタ (L/R)
    Random rng_{0x9E3779B9u};
};

// ----------------------------------------------------------------------------
// 合図音 (カウントダウンの電子音) のボイス。正弦波の合成なので素材は不要。
// ----------------------------------------------------------------------------
class CueVoice {
public:
    void trigger(SoundCue cue);
    void render(float* out, int frames, float outRate);

private:
    struct Beep {
        bool active = false;
        float freq = 0.0f;    // 基本周波数 (Hz)
        float length = 0.0f;  // 鳴らす長さ (秒)
        float time = 0.0f;    // 鳴り始めからの経過 (秒)
        float phase = 0.0f;   // 位相 0..1
    };
    static constexpr int kMaxBeeps = 4;
    Beep beeps_[kMaxBeeps];
};

}  // namespace sound

// ----------------------------------------------------------------------------
// ミキサー本体: 3 つのボイスをまとめ、音量を整えて出力する。
// ----------------------------------------------------------------------------
class SoundMixer {
public:
    // --- 準備 (音声デバイスを動かす前に呼ぶ) ---------------------------------
    void setOutputRate(int rate) { outRate_ = float(rate > 0 ? rate : 48000); }
    // エンジン音のループ素材を渡す (makeSeamlessLoop 済みのもの)。空ならエンジンは無音。
    // タイヤ音は合成なので素材は要らない。
    void setEngineLoop(Pcm loop) { engine_.setLoop(std::move(loop)); }
    bool hasEngine() const { return engine_.ready(); }

    // --- ゲーム側から (毎フレーム呼んでよい) --------------------------------
    void setEngine(const EngineSound& state);
    void setTires(const TireSound& state);
    void playCue(SoundCue cue);
    void setMasterVolume(float volume) { master_.store(volume); }

    // --- 音声スレッドから ----------------------------------------------------
    // frames フレーム分のステレオ波形 (L,R 交互、-1..1) を out へ上書きする。
    void render(float* out, int frames);

    // --- 診断 (デバッグ表示用) -----------------------------------------------
    float engineLevel() const { return engineLevel_.load(); }
    float tireLevel() const { return tireLevel_.load(); }

private:
    sound::EngineVoice engine_;
    sound::TireVoice tire_;
    sound::CueVoice cues_;
    float outRate_ = 48000.0f;

    // ゲーム側 → 音声側の受け渡し (atomic なのでロック不要)。
    std::atomic<bool> engineRunning_{false};
    std::atomic<float> engineRpm_{0.0f}, engineRpmNorm_{0.0f};
    std::atomic<float> engineThrottle_{0.0f}, engineLoad_{0.0f};
    std::atomic<float> tireDepth_{0.0f}, tireDuration_{0.0f}, tireSpeed_{0.0f};
    std::atomic<int> pendingTicks_{0}, pendingGos_{0};
    std::atomic<float> master_{0.8f};

    // 音声側 → ゲーム側 (診断用の現在音量)。
    std::atomic<float> engineLevel_{0.0f}, tireLevel_{0.0f};
};
