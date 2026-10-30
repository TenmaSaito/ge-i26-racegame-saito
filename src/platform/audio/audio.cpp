// ============================================================================
// audio.cpp ― 音声ポート (IAudio) の SDL 実装
// 役割: SDL の音声デバイスを開き、その要求に応じて SoundMixer (domain/sound)
//       に波形を書かせるだけの薄い層。音作りはすべて SoundMixer 側にある。
//
// プラットフォームごとの違い:
//   - Windows / Linux / Android: SDL が OS の音声 API (WASAPI / PulseAudio /
//     AAudio・OpenSL ES) を仲介するので、このファイルはそのまま使える。
//   - ブラウザ (Emscripten): SDL が Web Audio を使う。ブラウザは「ユーザーが
//     操作するまで音を出さない」(自動再生制限) ため、下の unlock 処理で
//     クリック・キー・タッチ・ゲームパッドのどれでも音が出るようにする。
//     タブが裏に回ったら一時停止し、戻ったら再開する。
// 依存: SDL2 本体のみ (SDL_mixer は使わない)。
// ============================================================================

#include "audio.hpp"

#include "pcm.hpp"
#include "platform.hpp"  // platform::readTextFile (アセットの読み込み)
#include "soundmixer.hpp"

#include <SDL.h>

#include <cstdio>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// ブラウザの自動再生制限を解除する仕掛けを 1 回だけ取り付ける。
// SDL が作る AudioContext (Module.SDL2.audioContext) を、あらゆる操作の
// タイミングで resume() する。ゲームパッドのボタンはブラウザによって
// 「ユーザー操作」に数えられないこともあるが、試す価値はあるので含める。
EM_JS(void, installAudioUnlockJs, (), {
    if (Module['raceAudioUnlockInstalled']) return;
    Module['raceAudioUnlockInstalled'] = true;
    function context() {
        var sdl = Module['SDL2'];
        return sdl && sdl.audioContext ? sdl.audioContext : null;
    }
    function resume() {
        var ctx = context();
        if (ctx && ctx.state !== 'running' && !document.hidden) {
            ctx.resume().catch(function () {});
        }
    }
    ['pointerdown', 'pointerup', 'mousedown', 'keydown', 'keyup', 'touchstart', 'touchend', 'click']
        .forEach(function (type) { window.addEventListener(type, resume, true); });
    setInterval(function () {
        try {
            var pads = navigator.getGamepads ? navigator.getGamepads() : [];
            for (var i = 0; i < pads.length; i++) {
                var pad = pads[i];
                if (!pad) continue;
                for (var j = 0; j < pad.buttons.length; j++) {
                    if (pad.buttons[j].pressed) { resume(); return; }
                }
            }
        } catch (e) {}
    }, 250);
    // タブが裏に回ったら止める (エンジン音が鳴りっぱなしにならないように)。
    document.addEventListener('visibilitychange', function () {
        var ctx = context();
        if (!ctx) return;
        if (document.hidden) {
            if (ctx.state === 'running') { ctx.suspend(); Module['raceAudioHiddenPause'] = true; }
        } else if (Module['raceAudioHiddenPause']) {
            Module['raceAudioHiddenPause'] = false;
            ctx.resume().catch(function () {});
        }
    });
});

// AudioContext の状態: 1 = 再生中、0 = 停止中 (操作待ち)、-1 = まだ無い。
EM_JS(int, audioContextStateJs, (), {
    var sdl = Module['SDL2'];
    var ctx = sdl && sdl.audioContext;
    if (!ctx) return -1;
    return ctx.state === 'running' ? 1 : 0;
});
#endif

namespace {

// 音素材を読む (実行場所によって基準フォルダが違うので候補を順に試す)。
bool loadSound(const char* name, Pcm& out) {
    const std::string rel = std::string("assets/sound/") + name;
    const char* prefixes[] = {"", "../", "/"};
    for (const char* prefix : prefixes) {
        std::string bytes;
        if (platform::readTextFile(prefix + rel, bytes)) {
            return decodeWav(bytes, out);
        }
    }
    return false;
}

class SdlAudio final : public IAudio {
public:
    SdlAudio() {
        // --- 素材の読み込みとループ加工 (デバイスを開く前に済ませる) -------
        Pcm engine, drift;
        if (loadSound("engine.wav", engine)) {
            // エンジン音は全体が定常音。継ぎ目を 0.25 秒重ねて輪にする。
            mixer_.setEngineLoop(makeSeamlessLoop(engine, 0.0f, 0.25f));
        }
        if (loadSound("drift.wav", drift)) {
            // ドリフト音は先頭 0.06 秒がフェードインなので捨て、0.35 秒重ねる。
            mixer_.setTireLoop(makeSeamlessLoop(drift, 0.06f, 0.35f));
        }

        // --- 音声デバイスを開く --------------------------------------------
        if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SDL_Log("SDL audio init failed: %s", SDL_GetError());
            return;
        }
        SDL_AudioSpec want{};
        want.freq = 48000;
        want.format = AUDIO_F32SYS;  // ミキサーは float で書く (変換は SDL が行う)
        want.channels = 2;
#ifdef __EMSCRIPTEN__
        want.samples = 2048;  // ブラウザはメインスレッドで回るため余裕を持たせる
#else
        want.samples = 1024;  // 約 21ms。操作への反応と途切れにくさの兼ね合い
#endif
        want.callback = &SdlAudio::callback;
        want.userdata = this;
        SDL_AudioSpec have{};
        // 周波数とバッファ長は端末に合わせてよい (ブラウザは 44.1k/48k 固定等)。
        device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                                      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
        if (device_ == 0) {
            SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
            return;
        }
        outRate_ = have.freq;
        mixer_.setOutputRate(have.freq);
        SDL_PauseAudioDevice(device_, 0);  // 再生開始 (以降 callback が呼ばれる)
#ifdef __EMSCRIPTEN__
        installAudioUnlockJs();
#endif
    }

    ~SdlAudio() override {
        if (device_ != 0) {
            SDL_CloseAudioDevice(device_);  // callback が止まってから戻る
        }
    }

    void setEngine(const EngineSound& state) override { mixer_.setEngine(state); }
    void setTires(const TireSound& state) override { mixer_.setTires(state); }
    void playCue(SoundCue cue) override { mixer_.playCue(cue); }

    bool waitingForUserGesture() const override {
#ifdef __EMSCRIPTEN__
        return device_ != 0 && audioContextStateJs() == 0;
#else
        return false;
#endif
    }

    std::string diag() const override {
        char buf[128];
        const char* state = device_ == 0 ? "NO DEVICE" : (waitingForUserGesture() ? "WAIT CLICK" : "ON");
        std::snprintf(buf, sizeof(buf), "%s %dHZ ENG %s %.2f TIRE %s %.2f", state, outRate_,
                      mixer_.hasEngine() ? "OK" : "NG", mixer_.engineLevel(),
                      mixer_.hasTire() ? "OK" : "NG", mixer_.tireLevel());
        return buf;
    }

private:
    // SDL の音声スレッド (ブラウザではメインスレッド) から呼ばれる。
    static void SDLCALL callback(void* userdata, Uint8* stream, int len) {
        auto* self = static_cast<SdlAudio*>(userdata);
        const int frames = len / int(sizeof(float) * 2);
        self->mixer_.render(reinterpret_cast<float*>(stream), frames);
    }

    SoundMixer mixer_;               // 音の合成 (プラットフォーム非依存)
    SDL_AudioDeviceID device_ = 0;   // 開いた音声デバイス (0 は失敗)
    int outRate_ = 0;                // 実際の出力サンプリングレート
};

}  // namespace

IAudio* createAudio() {
    return new SdlAudio();
}

void destroyAudio(IAudio* audio) {
    delete audio;
}
