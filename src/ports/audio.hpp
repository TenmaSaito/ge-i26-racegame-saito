#pragma once
// ============================================================================
// audio.hpp ― 音声ポート (IAudio)
// 役割: アプリケーション層が「音の状態」だけを伝え、再生の実務
//       (SDL_mixer、波形生成) はプラットフォーム層に閉じるための口。
//       今は仮のタイヤスリップ音だけだが、エンジン音や衝突音も
//       ここへ足していく想定。
// ============================================================================

// 音声の差し込み口。実装は platform 層 (createAudio が返す)。
struct IAudio {
    virtual ~IAudio() = default;
    // ドリフト音の強さ (0..1)。ドリフトの強さで音量が変わる。
    // 毎フレーム呼んでよい (変化がなければ内部で何もしない)。
    virtual void setSlip(float amount) = 0;
    // エンジン音。rpm (0..1) で音程の段が、load (0..1) で音量が変わる。
    // 両方 0 で停止 (レース外の合図)。毎フレーム呼んでよい。
    virtual void setEngine(float rpm, float load) = 0;
};

// 音声実装の生成と破棄 (platform 層が定義する)。
IAudio* createAudio();
void destroyAudio(IAudio* audio);
