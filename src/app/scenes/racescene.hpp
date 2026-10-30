#pragma once
// ============================================================================
// racescene.hpp ― レースシーン (RaceScene) の宣言
// 役割: レース本編 1 フレームの進行 (入力→物理→カメラ→音→振動→描画) と、
//       レース専用の状態 (物理のアキュムレータ、描画の補間ポーズ) を持つ。
//       共有物 (車・コース・描画・HUD・エンジン) は Game から借りる。
// ============================================================================

#include "car.hpp"  // DriveInput
#include "scene.hpp"

class Game;

class RaceScene final : public IScene {
public:
    explicit RaceScene(Game& game) : game_(game) {}

    void enter() override;          // レースを初期状態から始める
    void frame(double dt) override; // 1 フレームの進行

private:
    // カウントダウンでこのフレームに起きたこと (音と振動が共有する)。
    struct CountdownEvents {
        bool tick = false;  // 3・2・1 の数字が切り替わった
        bool go = false;    // スタートした
    };

    // --- frame() を構成する各段階 -------------------------------------------
    // UI 操作 (ポーズ・カメラ・調整パネル等) を反映する。
    // タイトルへ戻るなど、このフレームの処理を打ち切るなら false。
    bool handleUi(bool& resetRequested);
    // 運転入力を読む。raw へ生の入力を返し、戻り値は物理へ渡す入力
    // (調整パネル表示中とカウントダウン中は空)。
    DriveInput readDriveInput(DriveInput& raw, bool& resetRequested);
    // 物理を固定ステップで進め、描画用の補間ポーズを更新する。
    void stepPhysics(double elapsed, const DriveInput& input);
    // カメラの追従。
    void updateCamera(float elapsed);
    // カウントダウンの数字の切り替わりと GO を検出する。
    CountdownEvents detectCountdown();
    // エンジン音・タイヤ音・合図音。
    void updateSound(float elapsed, const DriveInput& raw, const DriveInput& input,
                     const CountdownEvents& countdown);
    // コントローラーの振動。
    void updateRumble(float elapsed, const CountdownEvents& countdown);
    // ワールドと HUD を描く。
    void draw();
    // 補間ポーズとカメラ用の平滑化値を、車の現在位置へ揃える (開始・リセット時)。
    void syncViewToCar();

    Game& game_;  // 共有の置き場 (車・コース・描画など)

    // 物理固定ステップ (60Hz) の端数貯金。表示レートによらず挙動を
    // 一定に保つため、溜まった分だけ物理を進める。
    double accumulator_ = 0;

    // 描画用の補間ポーズ。物理は固定 60Hz で進むため、そのまま描くと
    // 表示タイミングとのずれで前後にガクつく。1 ステップ前と現在を
    // アキュムレータの端数で補間してから描く。
    float prevX_ = 0, prevY_ = 0, prevZ_ = 0, prevYaw_ = 0, prevSlip_ = 0;  // 1 ステップ前
    float viewX_ = 0, viewY_ = 0, viewZ_ = 0, viewYaw_ = 0, viewSlip_ = 0;  // 補間結果
    float camSlip_ = 0;    // カメラ専用に平滑化したスリップ角 (物理値は使わない)
    float camAmount_ = 0;  // カメラ専用に平滑化したドリフト量
    bool viewReady_ = false;  // 補間ポーズが有効か

    // ゴースト記録の状態管理用 (ラップ開始と完了の検出)。
    bool wasTiming_ = false;   // 前ステップで計測中だったか
    int prevCompleted_ = 0;    // 前ステップの完了周回数

    // カウントダウンの変化検出用。
    bool wasCounting_ = false;  // 前フレームがカウント中だったか
    int prevCountNum_ = 0;      // 前フレームのカウント数字

    // タイヤ音: 滑りが続いている時間 (出だしと持続で音色を変えるため)。
    float slideTime_ = 0;  // 現在の滑りの継続秒
    float slideGap_ = 0;   // 滑りが途切れてからの秒 (短い途切れは継続扱い)

    // コントローラー振動の状態 (パルス発火のエッジ検出と送信間引き)。
    float rumbleTimer_ = 0;     // 継続振動の再送までの残り秒
    float prevWallImpact_ = 0;  // 前フレームの壁衝突の強さ
    bool wasPaused_ = false;    // 前フレームがポーズ中だったか
};
