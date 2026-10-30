#pragma once
// ============================================================================
// pcm.hpp ― 波形データ (PCM) の読み込みと加工
// 役割: WAV のバイト列を「ステレオ float の波形」へ変換し、ループ再生用に
//       継ぎ目の無い形 (シームレスループ) へ加工する。
// 依存: 標準ライブラリのみ。ファイルの読み込み自体は呼び出し側 (platform)
//       の仕事で、ここはメモリ上のバイト列だけを扱う。
// ============================================================================

#include <cstddef>
#include <string>
#include <vector>

// ステレオ (L, R の交互並び) の float 波形。値の範囲は概ね -1..1。
struct Pcm {
    int rate = 0;                // サンプリングレート (Hz)
    std::vector<float> samples;  // L0, R0, L1, R1, ...

    int frames() const { return static_cast<int>(samples.size() / 2); }
    bool empty() const { return samples.size() < 4 || rate <= 0; }
};

// WAV (RIFF) のバイト列を読む。対応形式: PCM 8/16/24/32bit、float 32bit、
// 1〜2ch、任意のサンプリングレート。モノラルは L=R のステレオにする。
// 失敗したら false (out は変更しない)。
bool decodeWav(const std::string& bytes, Pcm& out);

// 継ぎ目の無いループを作る。
//   startSec     先頭から捨てる秒数 (立ち上がりのフェードイン部分を避ける)
//   crossfadeSec 末尾を先頭へ重ねる長さ。長いほど継ぎ目が目立たない
// 末尾の crossfadeSec 分を先頭に「等パワー」で重ね合わせるので、ループの
// 最後のサンプルから最初のサンプルへ波形が連続につながる。
// 直流成分 (波形の偏り) も取り除く (ループ時のプチ音対策)。
Pcm makeSeamlessLoop(const Pcm& src, float startSec, float crossfadeSec);
