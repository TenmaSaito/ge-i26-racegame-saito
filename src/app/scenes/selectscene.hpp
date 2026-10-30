#pragma once
// ============================================================================
// selectscene.hpp ― セレクトシーン (SelectScene) の宣言
// 役割: タイトルの次に挟まる、レースの設定画面。ステージ (コース)・車種・
//       対戦相手の数 (ソロも可)・AI の強さを選ぶ。背景には現在のコースの
//       アトラクト表示が流れ、決定でコースを読み直してからレースへ遷移する。
// ============================================================================

#include "scene.hpp"

class Game;

class SelectScene final : public IScene {
public:
    explicit SelectScene(Game& game) : game_(game) {}

    void enter() override;          // 現在の設定を選択位置に合わせる
    void frame(double dt) override; // 選択操作と表示

private:
    // 行 row の値を dir (-1 / +1) だけ動かす。
    void adjust(int row, int dir);

    Game& game_;       // 共有の置き場
    double t_ = 0;     // 経過秒 (カメラ周回と点滅用)
    SelectMenu menu_;  // 選択状態
};
