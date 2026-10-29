// ============================================================================
// audio.cpp ― 音声実装 (SDL_mixer)
// 役割: 車の音を鳴らす。素材は assets/sound/ の WAV (提供された音源を変換)。
//   - エンジン音 (engine.wav の定常部分 2.5 秒): SDL_mixer には再生速度の
//     変更機能が無いため、読み込み時に 6 段階のピッチ違いループを
//     リサンプリングで用意し、回転数に応じて切り替える (段付きの変速感)。
//   - ドリフト音 (drift.wav、「ロケットスタート」音源): ドリフト開始で
//     鳴らし始め、強さで音量を変える。終わったらフェードアウト。
// どちらも「音が出せない環境では黙って何もしない」のが契約。
// ============================================================================

#include "audio.hpp"

#include <SDL_mixer.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// 音素材の場所を探す (実行環境で基準フォルダが違うため候補を順に試す)。
std::string findAsset(const char* name) {
    const std::string rel = std::string("assets/sound/") + name;
    const char* prefixes[] = {"", "../", "/"};
    for (const char* prefix : prefixes) {
        std::string path = std::string(prefix) + rel;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            std::fclose(f);
            return path;
        }
    }
    return "";
}

// PCM16 WAV を読み、ステレオ 44.1kHz の int16 配列へ正規化して返す。
// 対応はゲーム素材として十分な範囲 (PCM / 16bit / 1〜2ch / 任意レート)。
bool loadWavStereo(const std::string& path, std::vector<int16_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 44) {
        std::fclose(f);
        return false;
    }
    std::vector<uint8_t> bytes;
    bytes.resize(size_t(size));
    bool readOk = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    if (!readOk || std::memcmp(bytes.data(), "RIFF", 4) != 0) {
        return false;
    }
    auto u16 = [&](size_t at) { return uint16_t(bytes[at] | (bytes[at + 1] << 8)); };
    auto u32 = [&](size_t at) {
        return uint32_t(bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) |
                        (uint32_t(bytes[at + 3]) << 24));
    };
    int channels = 0;
    int rate = 0;
    const uint8_t* data = nullptr;
    uint32_t dataBytes = 0;
    for (size_t at = 12; at + 8 <= bytes.size();) {
        uint32_t chunkSize = u32(at + 4);
        if (std::memcmp(bytes.data() + at, "fmt ", 4) == 0) {
            // fmt チャンクのデータ部 (at + 8 から) の並び: 形式(0) /
            // チャンネル数(2) / サンプリングレート(4) / バイトレート(8) /
            // ブロックアライン(12) / ビット深度(14)。ビット深度は
            // at + 8 + 14 = at + 22 が正しい位置 (以前 at + 34 を見て
            // おり、波形データの値を読んで常に失敗していた)。
            if (u16(at + 8) != 1) {
                return false;  // PCM 以外は扱わない
            }
            channels = u16(at + 10);
            rate = int(u32(at + 12));
            if (u16(at + 22) != 16) {
                return false;  // 16bit のみ
            }
        } else if (std::memcmp(bytes.data() + at, "data", 4) == 0) {
            data = bytes.data() + at + 8;
            dataBytes = chunkSize;
        }
        at += 8 + chunkSize + (chunkSize & 1);
    }
    if (!data || channels < 1 || channels > 2 || rate <= 0) {
        return false;
    }
    int frames = int(dataBytes / (2 * channels));
    // まず素材レートのステレオへ展開し、次に 44.1kHz へリサンプリングする。
    std::vector<int16_t> src(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        int16_t l = int16_t(data[2 * (i * channels)] | (data[2 * (i * channels) + 1] << 8));
        int16_t r = channels == 2
                        ? int16_t(data[2 * (i * channels) + 2] | (data[2 * (i * channels) + 3] << 8))
                        : l;
        src[size_t(i) * 2] = l;
        src[size_t(i) * 2 + 1] = r;
    }
    if (rate == 44100) {
        out = src;
        return true;
    }
    int outFrames = int(int64_t(frames) * 44100 / rate);
    out.resize(size_t(outFrames) * 2);
    for (int i = 0; i < outFrames; ++i) {
        double pos = double(i) * rate / 44100.0;
        int i0 = int(pos);
        int i1 = i0 + 1 < frames ? i0 + 1 : i0;
        float fr = float(pos - i0);
        for (int ch = 0; ch < 2; ++ch) {
            float a = src[size_t(i0) * 2 + ch];
            float b = src[size_t(i1) * 2 + ch];
            out[size_t(i) * 2 + ch] = int16_t(a + (b - a) * fr);
        }
    }
    return true;
}

// ピッチ違いループを作る。rate 倍の速さで読む = 音程が rate 倍。
// 継ぎ目は末尾と先頭をクロスフェードしてクリック音を抑える。
std::vector<int16_t> makePitchVariant(const std::vector<int16_t>& base, float rate) {
    int inFrames = int(base.size() / 2);
    int outFrames = int(inFrames / rate);
    std::vector<int16_t> out(size_t(outFrames) * 2);
    for (int i = 0; i < outFrames; ++i) {
        double pos = double(i) * rate;
        int i0 = int(pos) % inFrames;
        int i1 = (i0 + 1) % inFrames;
        float fr = float(pos - int(pos));
        for (int ch = 0; ch < 2; ++ch) {
            float a = base[size_t(i0) * 2 + ch];
            float b = base[size_t(i1) * 2 + ch];
            out[size_t(i) * 2 + ch] = int16_t(a + (b - a) * fr);
        }
    }
    const int fade = 1500;  // 約 34ms
    if (outFrames > fade * 2) {
        for (int j = 0; j < fade; ++j) {
            float f = float(j) / float(fade);
            int tail = outFrames - fade + j;
            for (int ch = 0; ch < 2; ++ch) {
                float a = out[size_t(tail) * 2 + ch];
                float b = out[size_t(j) * 2 + ch];
                out[size_t(tail) * 2 + ch] = int16_t(a * (1.0f - f) + b * f);
            }
        }
    }
    return out;
}

class MixerAudio final : public IAudio {
public:
    MixerAudio() {
        // エンジン: 定常ループから 6 段階のピッチ違いを用意する。
        std::vector<int16_t> base;
        std::string enginePath = findAsset("engine.wav");
        if (!enginePath.empty() && loadWavStereo(enginePath, base) && base.size() > 8) {
            // 段は等比 (0.80 → 1.90 を 24 分割)。1 段の変化は約 3.8% で、
            // 半音 (約 5.9%) より小さい。切替時のクロスフェードと合わせて
            // 「段々」な音程変化を感じさせないための細かさ。
            for (int i = 0; i < kEngineSteps; ++i) {
                float rate = 0.80f * std::pow(1.90f / 0.80f,
                                              float(i) / float(kEngineSteps - 1));
                enginePcm_[i] = makePitchVariant(base, rate);
                engineChunks_[i] = Mix_QuickLoad_RAW(
                    reinterpret_cast<uint8_t*>(enginePcm_[i].data()),
                    uint32_t(enginePcm_[i].size() * sizeof(int16_t)));
            }
        }
        // ドリフト: 先頭 2.2 秒 (音量の安定した部分) を読み、継ぎ目を
        // クロスフェードして「終わらないループ」に加工する。ドリフトが
        // 長く続いても途中で切れない。
        std::vector<int16_t> driftBase;
        std::string driftPath = findAsset("drift.wav");
        if (!driftPath.empty() && loadWavStereo(driftPath, driftBase) && driftBase.size() > 8) {
            driftPcm_ = makePitchVariant(driftBase, 1.0f);  // 加工 (継ぎ目処理)
            drift_ = Mix_QuickLoad_RAW(reinterpret_cast<uint8_t*>(driftPcm_.data()),
                                       uint32_t(driftPcm_.size() * sizeof(int16_t)));
        }
    }
    ~MixerAudio() override {
        for (int i = 0; i < 2; ++i) {
            if (engineCh_[i] >= 0) Mix_HaltChannel(engineCh_[i]);
        }
        if (driftCh_ >= 0) Mix_HaltChannel(driftCh_);
        for (int i = 0; i < kEngineSteps; ++i) {
            if (engineChunks_[i]) Mix_FreeChunk(engineChunks_[i]);
        }
        if (drift_) Mix_FreeChunk(drift_);
    }

    // エンジン音。rpm 0..1 が段 (ピッチ) を、load 0..1 が音量を決める。
    // 両方 0 は「止める」の合図 (レース外)。
    void setEngine(float rpm, float load) override {
        if (!engineChunks_[0]) {
            return;
        }
        if (rpm <= 0.0f && load <= 0.0f) {
            // 停止の合図 (レース外): 両チャンネルとも止める。
            for (int i = 0; i < 2; ++i) {
                if (engineCh_[i] >= 0 && Mix_Playing(engineCh_[i])) {
                    Mix_HaltChannel(engineCh_[i]);
                }
            }
            engineStep_ = -1;
            return;
        }
        // 段の選択は四捨五入 (境界で小刻みに往復しないよう中心寄せ)。
        int step = int(rpm * float(kEngineSteps - 1) + 0.5f);
        if (step >= kEngineSteps) step = kEngineSteps - 1;
        if (step < 0) step = 0;
        int cur = engineCh_[engineActive_];
        bool curPlaying = cur >= 0 && Mix_Playing(cur);
        if (step != engineStep_ || !curPlaying) {
            // 段が変わったら、旧チャンネルをフェードアウトしつつ
            // もう一方のチャンネルで新段をフェードインする (45ms の
            // クロスフェード)。瞬時切替の「段差感」を消すため。
            int next = 1 - engineActive_;
            if (engineCh_[next] >= 0 && Mix_Playing(engineCh_[next])) {
                Mix_HaltChannel(engineCh_[next]);  // 前のフェード残りを掃除
            }
            if (curPlaying) {
                Mix_FadeOutChannel(cur, 45);
            }
            engineCh_[next] = Mix_FadeInChannel(-1, engineChunks_[step], -1, 45);
            engineActive_ = next;
            engineStep_ = step;
            cur = engineCh_[next];
        }
        if (cur >= 0) {
            int vol = int(26.0f + load * 58.0f + rpm * 36.0f);
            if (vol > 118) vol = 118;
            Mix_Volume(cur, vol);
        }
    }

    // 診断用: 素材の読み込み可否とエンジン再生状態。
    std::string diag() const override {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "ENG %s%s DRIFT %s",
                      engineChunks_[0] ? "OK" : "NG",
                      ((engineCh_[0] >= 0 && Mix_Playing(engineCh_[0])) ||
                       (engineCh_[1] >= 0 && Mix_Playing(engineCh_[1]))) ? "+PLAY" : "",
                      drift_ ? "OK" : "NG");
        return buf;
    }

    // ドリフト音 (ロケットスタート音源のループ加工版)。ドリフト中は
    // 無限ループで鳴り続け、強さで音量が変わる。終わったらフェードアウト。
    void setSlip(float amount) override {
        if (!drift_) {
            return;
        }
        bool playing = driftCh_ >= 0 && Mix_Playing(driftCh_);
        if (amount >= 0.05f) {
            if (!playing) {
                driftCh_ = Mix_PlayChannel(-1, drift_, -1);  // 無限ループ
            }
            if (driftCh_ >= 0) {
                int vol = int(30.0f + amount * 95.0f);
                if (vol > 125) vol = 125;
                Mix_Volume(driftCh_, vol);
            }
        } else if (playing) {
            Mix_FadeOutChannel(driftCh_, 150);
        }
    }

private:
    static constexpr int kEngineSteps = 24;            // ピッチの段数
    std::vector<int16_t> enginePcm_[kEngineSteps];     // 段ごとの波形 (保持が必須)
    Mix_Chunk* engineChunks_[kEngineSteps] = {};       // 段ごとの再生チャンク
    std::vector<int16_t> driftPcm_;                    // ドリフト音のループ波形 (保持が必須)
    Mix_Chunk* drift_ = nullptr;                       // ドリフト音
    int engineCh_[2] = {-1, -1};                       // エンジン再生チャンネル (クロスフェード用に 2 本)
    int engineActive_ = 0;                             // 現在アクティブな側 (0/1)
    int driftCh_ = -1;                                 // ドリフト再生チャンネル
    int engineStep_ = -1;                              // 現在の段
};

}  // namespace

IAudio* createAudio() {
    return new MixerAudio();
}

void destroyAudio(IAudio* audio) {
    delete audio;
}
