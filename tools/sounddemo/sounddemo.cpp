// ============================================================================
// sounddemo.cpp ― 音作りの確認用ツール (ゲーム本体には含まれない)
// 役割: ゲームと同じ EngineSim + SoundMixer で「走行の台本」を鳴らし、
//       結果を WAV ファイルへ書き出す。ゲームを起動せずに、エンジン音の
//       回転の上がり方・シフト・ドリフト音のつながりを耳で確かめられる。
//       音声デバイスも SDL も使わない (ミキサーがプラットフォーム非依存で
//       あることの確認も兼ねる)。
//
// 使い方 (ネイティブビルドで):
//   cmake --build build-native --target sounddemo
//   ./build-native/sounddemo assets/sound sound_demo.wav
// ============================================================================

#include "enginesim.hpp"
#include "pcm.hpp"
#include "soundmixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kRate = 48000;         // 書き出すサンプリングレート
constexpr int kBlock = 800;          // 1 回の合成量 (= 60fps の 1 フレーム分)
constexpr float kDt = float(kBlock) / float(kRate);

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// float ステレオを 16bit WAV で保存する。
bool writeWav(const std::string& path, const std::vector<float>& samples) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    auto put16 = [&](uint32_t v) { out.put(char(v & 0xFF)); out.put(char((v >> 8) & 0xFF)); };
    auto put32 = [&](uint32_t v) { put16(v & 0xFFFF); put16(v >> 16); };
    const uint32_t dataBytes = uint32_t(samples.size() * 2);
    out << "RIFF"; put32(36 + dataBytes); out << "WAVE";
    out << "fmt "; put32(16); put16(1); put16(2); put32(kRate); put32(kRate * 4); put16(4); put16(16);
    out << "data"; put32(dataBytes);
    for (float v : samples) {
        put16(uint16_t(int16_t(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f)));
    }
    return true;
}

// 台本の 1 区間: この秒数のあいだ、アクセルとドリフトの深さをこうする。
struct Step {
    float seconds;
    float throttle;  // 0..1
    float drift;     // 0..1 (ドリフトの深さ)
    bool countdown;  // カウントダウン中 (空ぶかし)
    const char* label;
};

}  // namespace

int main(int argc, char** argv) {
    const std::string soundDir = argc > 1 ? argv[1] : "assets/sound";
    const std::string outPath = argc > 2 ? argv[2] : "sound_demo.wav";

    // --- 素材の読み込み (ゲームの audio.cpp と同じ加工) ----------------------
    SoundMixer mixer;
    mixer.setOutputRate(kRate);
    std::string bytes;
    Pcm pcm;
    if (readFile(soundDir + "/engine.wav", bytes) && decodeWav(bytes, pcm)) {
        mixer.setEngineLoop(makeSeamlessLoop(pcm, 0.0f, 0.25f));
    }
    if (!mixer.hasEngine()) {
        std::fprintf(stderr, "音素材を読めません: %s\n", soundDir.c_str());
        return 1;
    }

    // --- 走行の台本 ---------------------------------------------------------
    const Step script[] = {
        {1.0f, 0.0f, 0.0f, true, "idle"},
        {0.6f, 1.0f, 0.0f, true, "rev (countdown)"},
        {0.6f, 0.0f, 0.0f, true, "release"},
        {0.6f, 1.0f, 0.0f, true, "rev to limiter"},
        {9.0f, 1.0f, 0.0f, false, "full throttle through the gears"},
        {1.5f, 0.0f, 0.0f, false, "lift off (overrun)"},
        {4.0f, 0.8f, 0.85f, false, "long drift"},
        {1.0f, 0.6f, 0.3f, false, "shallow drift"},
        {2.0f, 0.5f, 0.0f, false, "grip"},
        {2.0f, 0.0f, 0.0f, false, "coast"},
    };

    EngineSim engine;
    engine.reset(82.0f);
    float speed = 0.0f;
    float slideTime = 0.0f;
    bool wasCountdown = true;
    // カウントダウン音: 台本の空ぶかし区間 (計 2.8 秒) の終わり 1.65 秒で
    // 0.55 秒ごとに 3 回鳴らし、区間が終わったら GO (ゲームと同じ間隔)。
    const float tickTimes[3] = {1.15f, 1.70f, 2.25f};
    float countdownElapsed = 0.0f;
    int ticksPlayed = 0;
    std::vector<float> all;
    std::vector<float> block(kBlock * 2);

    for (const Step& step : script) {
        std::printf("%5.1fs  %s\n", float(all.size() / 2) / float(kRate), step.label);
        for (float t = 0.0f; t < step.seconds; t += kDt) {
            // カウントダウンの合図 (ゲームと同じく 3・2・1 と GO)。
            if (step.countdown) {
                countdownElapsed += kDt;
                while (ticksPlayed < 3 && countdownElapsed >= tickTimes[ticksPlayed]) {
                    mixer.playCue(SoundCue::CountdownTick);
                    ++ticksPlayed;
                }
            } else if (wasCountdown) {
                mixer.playCue(SoundCue::CountdownGo);
            }
            wasCountdown = step.countdown;

            // ごく簡単な車速モデル (アクセルで加速、抗力と転がり抵抗で減速)。
            if (!step.countdown) {
                const float accel = step.throttle * 11.0f * (1.0f - speed / 82.0f) - 0.0004f * speed * speed - 0.9f -
                                    step.drift * 2.0f;
                speed = std::max(0.0f, speed + accel * kDt);
            }
            engine.update(kDt, speed, step.throttle, step.drift, step.countdown);
            mixer.setEngine(engine.sound(true));

            slideTime = step.drift > 0.12f ? slideTime + kDt : 0.0f;
            TireSound tire;
            tire.depth = step.drift;
            tire.duration = slideTime;
            tire.speed = speed;
            mixer.setTires(tire);

            mixer.render(block.data(), kBlock);
            all.insert(all.end(), block.begin(), block.end());
        }
        std::printf("        speed %.0f km/h  gear %d  rpm %.0f\n", speed * 3.6f, engine.gear(), engine.rpm());
    }

    // --- フェードアウトして書き出す -------------------------------------------
    mixer.setEngine(EngineSound{});
    mixer.setTires(TireSound{});
    for (int i = 0; i < 40; ++i) {
        mixer.render(block.data(), kBlock);
        all.insert(all.end(), block.begin(), block.end());
    }
    if (!writeWav(outPath, all)) {
        std::fprintf(stderr, "書き出せません: %s\n", outPath.c_str());
        return 1;
    }
    std::printf("wrote %s (%.1f s)\n", outPath.c_str(), float(all.size() / 2) / float(kRate));
    return 0;
}
