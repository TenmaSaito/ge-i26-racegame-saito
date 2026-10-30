#pragma once
// ============================================================================
// audio.hpp ― 音声ポート (IAudio)
// 役割: アプリケーション層は「音の状態」(soundparams.hpp) だけを伝え、
//       音の合成 (SoundMixer) と出力デバイスの扱いは外側に任せるための口。
//       ゲーム側はどのプラットフォームで鳴っているかを知らない。
//
//   アプリ層 (racescene) ──EngineSound / TireSound / SoundCue──▶ IAudio
//                                                                 │
//   platform 層 (audio.cpp): SDL の音声デバイス ◀── SoundMixer (domain/sound)
//
// どの関数も「音が出せない環境では黙って何もしない」のが契約。
// ============================================================================

#include "soundparams.hpp"

#include <string>

struct IAudio {
    virtual ~IAudio() = default;

    // エンジン音の状態。毎フレーム呼んでよい (音声側で滑らかにつなぐ)。
    virtual void setEngine(const EngineSound& state) = 0;
    // タイヤ (ドリフト) 音の状態。毎フレーム呼んでよい。
    virtual void setTires(const TireSound& state) = 0;
    // 単発の効果音を鳴らす。
    virtual void playCue(SoundCue cue) = 0;

    // ブラウザの自動再生制限などで、まだ音が出せない状態なら true。
    // (画面に「クリックで音が出ます」と案内するために使う)
    virtual bool waitingForUserGesture() const = 0;
    // 診断用: 素材の読み込み可否・再生状態を短い文字列で返す (デバッグ表示用)。
    virtual std::string diag() const = 0;
};

// 音声実装の生成と破棄 (platform 層が定義する)。
IAudio* createAudio();
void destroyAudio(IAudio* audio);
