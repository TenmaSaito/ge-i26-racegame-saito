#pragma once
// ============================================================================
// resultscene.hpp ― リザルトシーン (ResultScene) の宣言
// 役割: ゴール後のタイム表示と、次の行き先 (もう一度レース／タイトル) の
//       選択待ち。背景ではゴール地点の車の周りをカメラが回る。
// ============================================================================

#include "scene.hpp"

class Game;

class ResultScene final : public IScene {
public:
    explicit ResultScene(Game& game) : game_(game) {}

    void enter() override;          // 表示用のタイマをリセットするだけ
    void frame(double dt) override; // 周回カメラとリザルト表示

private:
    Game& game_;   // 共有の置き場 (タイムは Game の Race が保持)
    double t_ = 0; // シーン経過秒 (カメラの周回と文字の点滅用)
    int rank_ = -1;  // 今回の順位 (0 始まり、圏外は -1)。enter で登録する
};
