#pragma once
// ============================================================================
// selectscene.hpp ― セレクトシーン (SelectScene) の宣言
// 役割: タイトルの次に挟まる、ステージ (コース) 選択と車種選択。
//       背景には現在のコースのアトラクト表示が流れ、決定でコースを
//       読み直してからレースへ遷移する。
// ============================================================================

#include "scene.hpp"

class Game;

class SelectScene final : public IScene {
public:
    explicit SelectScene(Game& game) : game_(game) {}

    void enter() override;          // 現在のコースと車種を選択位置に合わせる
    void frame(double dt) override; // 選択操作と表示

private:
    Game& game_;       // 共有の置き場
    double t_ = 0;     // 経過秒 (カメラ周回と点滅用)
    int course_ = 0;   // 選択中のコース番号 (kCourses の添字)
    int spec_ = 0;     // 選択中の車種番号 (0/1/2)
};
