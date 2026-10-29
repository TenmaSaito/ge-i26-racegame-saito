#pragma once
// ============================================================================
// titlescene.hpp ― タイトルシーン (TitleScene) の宣言
// 役割: スタート地点の車の周りをカメラがゆっくり回るアトラクト表示と、
//       スタート待ち。決定 (Enter / クリック / パッド) でレースへ遷移する。
// ============================================================================

#include "scene.hpp"

class Game;

class TitleScene final : public IScene {
public:
    explicit TitleScene(Game& game) : game_(game) {}

    void enter() override;          // 車をスタート地点へ置き直す
    void frame(double dt) override; // 周回カメラとタイトル表示

private:
    Game& game_;   // 共有の置き場
    double t_ = 0; // シーン経過秒 (カメラの周回と文字の点滅用)
};
