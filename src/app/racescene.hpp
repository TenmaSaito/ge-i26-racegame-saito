#pragma once
// ============================================================================
// racescene.hpp ― レースシーン (RaceScene) の宣言
// 役割: レース本編 1 フレームの進行 (入力→物理→演出→カメラ→描画) と、
//       レース専用の状態 (物理のアキュムレータ、描画の補間ポーズ) を持つ。
//       共有物 (車・コース・描画・HUD) は Game から借りる。
// ============================================================================

#include "scene.hpp"

class Game;

class RaceScene final : public IScene {
public:
    explicit RaceScene(Game& game) : game_(game) {}

    void enter() override;          // レースを初期状態から始める
    void frame(double dt) override; // 1 フレームの進行

private:
    Game& game_;  // 共有の置き場 (車・コース・描画など)

    // 物理固定ステップ (60Hz) の端数貯金。表示レートによらず挙動を
    // 一定に保つため、溜まった分だけ物理を進める。
    double accumulator_ = 0;

    // 描画用の補間ポーズ。物理は固定 60Hz で進むため、そのまま描くと
    // 表示タイミングとのずれで前後にガクつく。1 ステップ前と現在を
    // アキュムレータの端数で補間してから描く。
    float prevX_ = 0, prevY_ = 0, prevZ_ = 0, prevYaw_ = 0, prevSlip_ = 0;  // 1 ステップ前
    float viewX_ = 0, viewY_ = 0, viewZ_ = 0, viewYaw_ = 0, viewSlip_ = 0;  // 補間結果
    bool viewReady_ = false;  // 補間ポーズが有効か

    // ゴースト記録の状態管理用 (ラップ開始と完了の検出)。
    bool wasTiming_ = false;   // 前ステップで計測中だったか
    int prevCompleted_ = 0;    // 前ステップの完了周回数
};
