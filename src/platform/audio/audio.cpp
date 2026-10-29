// ============================================================================
// audio.cpp ― 音声実装 (SDL_mixer)
// 役割: 仮のタイヤスリップ音を「波形から合成して」鳴らす。音声ファイルは
//       使わず、メモリ上で波形を作って Mix_Chunk 化する (素材の権利問題を
//       避けつつ、音の仕組みだけ先に通すための仮実装)。
//       スリップ音 = 高めのサイン波 + 倍音 + 少量のノイズ、をループ再生し、
//       強さは音量で表す (ピッチ変更は SDL_mixer の素の機能には無いため)。
// ============================================================================

#include "audio.hpp"

#include <SDL_mixer.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// スリップ音の波形を合成する。44.1kHz / 16bit / モノラル、0.5 秒ループ。
// ループの継ぎ目でクリック音が出ないよう、末尾と先頭をクロスフェードする。
Mix_Chunk* makeSlipChunk() {
    const int rate = 44100;
    const int frames = rate / 2;  // 0.5 秒
    std::vector<int16_t> samples;
    samples.resize(size_t(frames));
    double phase = 0;             // 位相 (周波数を揺らすため積分で持つ)
    unsigned noiseState = 12345;  // 決定的なノイズ源 (LCG)
    float noiseSmooth = 0;
    for (int i = 0; i < frames; ++i) {
        double t = double(i) / rate;
        // ビブラート: 周波数を ±6% ゆっくり揺らすと「キー」という鳴きになる。
        double freq = 820.0 * (1.0 + 0.06 * std::sin(2.0 * 3.14159265 * 9.0 * t));
        phase += 2.0 * 3.14159265 * freq / rate;
        noiseState = noiseState * 1664525u + 1013904223u;
        float noise = (float((noiseState >> 16) & 0x7FFF) / 16384.0f - 1.0f);
        noiseSmooth += (noise - noiseSmooth) * 0.25f;  // 荒さを均す
        double v = 0.55 * std::sin(phase) + 0.25 * std::sin(2.0 * phase) + 0.10 * noiseSmooth;
        samples[size_t(i)] = int16_t(v * 32767.0 * 0.7);
    }
    // 継ぎ目のクロスフェード (30ms)。
    const int fade = 1323;
    for (int i = 0; i < fade; ++i) {
        float f = float(i) / float(fade);
        int tail = frames - fade + i;
        samples[size_t(tail)] = int16_t(samples[size_t(tail)] * (1.0f - f) + samples[size_t(i)] * f);
    }
    // WAV コンテナに詰めて Mix_Chunk 化する。
    std::vector<uint8_t> wav;
    auto put32 = [&](uint32_t v) {
        for (int b = 0; b < 4; ++b) wav.push_back(uint8_t((v >> (8 * b)) & 0xFF));
    };
    auto put16 = [&](uint16_t v) {
        for (int b = 0; b < 2; ++b) wav.push_back(uint8_t((v >> (8 * b)) & 0xFF));
    };
    auto putTag = [&](const char* tag) { for (int b = 0; b < 4; ++b) wav.push_back(uint8_t(tag[b])); };
    uint32_t dataBytes = uint32_t(samples.size() * 2);
    putTag("RIFF"); put32(36 + dataBytes); putTag("WAVE");
    putTag("fmt "); put32(16); put16(1); put16(1);
    put32(uint32_t(rate)); put32(uint32_t(rate * 2)); put16(2); put16(16);
    putTag("data"); put32(dataBytes);
    for (int16_t s : samples) put16(uint16_t(s));
    SDL_RWops* rw = SDL_RWFromMem(wav.data(), int(wav.size()));
    if (!rw) {
        return nullptr;
    }
    return Mix_LoadWAV_RW(rw, 1);  // freesrc=1 で RW も閉じる
}

// SDL_mixer 実装。チャンネルを 1 つ占有してループ再生する。
class MixerAudio final : public IAudio {
public:
    MixerAudio() : chunk_(makeSlipChunk()) {}
    ~MixerAudio() override {
        if (channel_ >= 0) {
            Mix_HaltChannel(channel_);
        }
        if (chunk_) {
            Mix_FreeChunk(chunk_);
        }
    }
    void setSlip(float amount) override {
        if (!chunk_) {
            return;  // 音が出せない環境では黙って何もしない
        }
        if (amount < 0.03f) {
            if (channel_ >= 0 && Mix_Playing(channel_)) {
                Mix_HaltChannel(channel_);
            }
            return;
        }
        if (channel_ < 0 || !Mix_Playing(channel_)) {
            channel_ = Mix_PlayChannel(-1, chunk_, -1);  // 無限ループ
        }
        if (channel_ >= 0) {
            int vol = int(25.0f + amount * 100.0f);
            if (vol > MIX_MAX_VOLUME) vol = MIX_MAX_VOLUME;
            Mix_Volume(channel_, vol);
        }
    }

private:
    Mix_Chunk* chunk_ = nullptr;  // 合成したスリップ音
    int channel_ = -1;            // 再生中のチャンネル (-1 は未再生)
};

}  // namespace

IAudio* createAudio() {
    return new MixerAudio();
}

void destroyAudio(IAudio* audio) {
    delete audio;
}
