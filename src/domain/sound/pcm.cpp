// ============================================================================
// pcm.cpp ― 波形データ (PCM) の読み込みと加工の実装
// ============================================================================

#include "pcm.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

// リトルエンディアンの整数を読む小さな道具。
uint32_t readU16(const unsigned char* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8); }
uint32_t readU32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// 1 サンプル分のバイト列を -1..1 の float にする。
float sampleToFloat(const unsigned char* p, int bits, bool isFloat) {
    if (isFloat) {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    switch (bits) {
    case 8:  // 8bit は符号なし (中心が 128)
        return (float(p[0]) - 128.0f) / 128.0f;
    case 16:
        return float(int16_t(readU16(p))) / 32768.0f;
    case 24: {
        int32_t v = int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8;
        return float(v) / 8388608.0f;
    }
    case 32:
        return float(int32_t(readU32(p))) / 2147483648.0f;
    default:
        return 0.0f;
    }
}

}  // namespace

bool decodeWav(const std::string& bytes, Pcm& out) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t size = bytes.size();
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }

    // --- チャンクを順にたどり、fmt と data を探す ---------------------------
    int format = 0, channels = 0, rate = 0, bits = 0;
    const unsigned char* pcm = nullptr;
    size_t pcmBytes = 0;
    for (size_t at = 12; at + 8 <= size;) {
        const uint32_t chunkSize = readU32(data + at + 4);
        const unsigned char* body = data + at + 8;
        const size_t available = size - (at + 8);
        if (std::memcmp(data + at, "fmt ", 4) == 0 && chunkSize >= 16 && available >= 16) {
            // fmt の並び: 形式(0) チャンネル数(2) レート(4) バイトレート(8)
            //             ブロック長(12) ビット深度(14)
            format = int(readU16(body));
            channels = int(readU16(body + 2));
            rate = int(readU32(body + 4));
            bits = int(readU16(body + 14));
            if (format == 0xFFFE && chunkSize >= 26) {
                format = int(readU16(body + 24));  // WAVE_FORMAT_EXTENSIBLE の実形式
            }
        } else if (std::memcmp(data + at, "data", 4) == 0) {
            pcm = body;
            pcmBytes = chunkSize < available ? chunkSize : available;
        }
        at += 8 + size_t(chunkSize) + (chunkSize & 1);
    }

    const bool isFloat = format == 3;
    const bool supported = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
                           (isFloat && bits == 32);
    if (!pcm || !supported || channels < 1 || channels > 2 || rate <= 0) {
        return false;
    }

    // --- ステレオ float へ展開 ---------------------------------------------
    const int bytesPerSample = bits / 8;
    const int frameBytes = bytesPerSample * channels;
    const int frames = int(pcmBytes / size_t(frameBytes));
    if (frames < 2) {
        return false;
    }
    Pcm result;
    result.rate = rate;
    result.samples.resize(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const unsigned char* frame = pcm + size_t(i) * size_t(frameBytes);
        const float left = sampleToFloat(frame, bits, isFloat);
        const float right = channels == 2 ? sampleToFloat(frame + bytesPerSample, bits, isFloat) : left;
        result.samples[size_t(i) * 2] = left;
        result.samples[size_t(i) * 2 + 1] = right;
    }
    out = std::move(result);
    return true;
}

Pcm makeSeamlessLoop(const Pcm& src, float startSec, float crossfadeSec) {
    if (src.empty()) {
        return src;
    }
    const int total = src.frames();
    int start = int(startSec * float(src.rate));
    int fade = int(crossfadeSec * float(src.rate));
    if (start < 0 || start > total / 4) start = 0;  // 捨てすぎない
    const int usable = total - start;
    if (fade > usable / 3) fade = usable / 3;        // 重ねる長さは全体の 1/3 まで
    const int loopFrames = usable - fade;
    if (loopFrames < 2 || fade < 1) {
        return src;
    }

    // --- 直流成分 (平均値) を求める ---------------------------------------
    double meanL = 0, meanR = 0;
    for (int i = start; i < total; ++i) {
        meanL += src.samples[size_t(i) * 2];
        meanR += src.samples[size_t(i) * 2 + 1];
    }
    meanL /= double(usable);
    meanR /= double(usable);

    // --- ループ本体 ---------------------------------------------------------
    // out[i] = src[start + i]                       (i >= fade)
    // out[i] = src[start + i] * 入り + src[start + loopFrames + i] * 抜け (i < fade)
    // 末尾 out[loopFrames-1] = src[start+loopFrames-1] の次は
    // src[start+loopFrames] で、これは out[0] で重みが全量の側。
    // つまり「ループの終わり → 頭」は元の波形どおりに連続する。
    Pcm out;
    out.rate = src.rate;
    out.samples.resize(size_t(loopFrames) * 2);
    for (int i = 0; i < loopFrames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            const float mean = float(ch == 0 ? meanL : meanR);
            float v = src.samples[size_t(start + i) * 2 + ch] - mean;
            if (i < fade) {
                // 等パワー (sin/cos) で重ねると、重なり中も音量が凹まない。
                const float t = (float(i) + 0.5f) / float(fade);
                const float fadeIn = std::sin(t * 1.5707963f);
                const float fadeOut = std::cos(t * 1.5707963f);
                const float tail = src.samples[size_t(start + loopFrames + i) * 2 + ch] - mean;
                v = v * fadeIn + tail * fadeOut;
            }
            out.samples[size_t(i) * 2 + ch] = v;
        }
    }
    return out;
}
