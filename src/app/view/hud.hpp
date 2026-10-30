#pragma once
// ============================================================================
// hud.hpp ― HUD 描画クラス (Hud) の宣言
// 役割: 速度・ラップ・ミニマップ・デバッグ・チューニング画面の合成。
//       値の計算はせず、Game の状態を Renderer の 2D 描画へ写すだけ。
//       表示専用の状態 (ミニマップの範囲キャッシュ) はこのクラスが持つ。
// ============================================================================

class Game;
struct SelectMenu;  // scene.hpp (セレクト画面の選択状態)

// 秒数を "M:SS.mmm" 形式にする (負数なら "--:--.---")。
void formatTime(char* buf, int size, float seconds);

class Hud {
public:
    // HUD 全体を描く (レースシーンの最後に呼ばれる)。
    void draw(Game& app);
    // タイトル画面の文字 (t は点滅用の経過秒)。
    void drawTitle(Game& app, float t);
    // セレクト画面の文字 (ステージ・車種・対戦相手の数・AI の強さ)。
    void drawSelect(Game& app, const SelectMenu& menu, float t);
    // リザルト画面の文字 (タイム・順位・ランキングと行き先の案内)。
    void drawResult(Game& app, float t, int rank);

private:
    void drawMinimap(Game& app);       // コース全体図と自車マーカー
    void drawCountdown(Game& app);     // スタートシグナル (ランプ) と 3・2・1 / GO!
    void drawTachometer(Game& app);    // ギアと回転数のバー (右下)
    void drawStandings(Game& app);     // 順位と順位表 (右上、対戦相手がいるとき)
    void drawSoundHint(Game& app, float x, float y);  // 「クリックで音が出ます」の案内
    void drawDriftDebug(Game& app);    // ドリフト状態の内訳 (ネイティブのみ)
    void drawTuning(Game& app);        // チューニングパネル (T キー)
    void sendDebugToPage(Game& app);   // ブラウザ版: ページ側パネルへ送る

    // ミニマップの表示範囲キャッシュ。コース固定なので初回だけ全サンプルを
    // 走査して求め、以降は使い回す (毎フレームの全数走査を避けるため)。
    float mmMinX_ = 0, mmMaxX_ = 0, mmMinZ_ = 0, mmMaxZ_ = 0;
    bool mmReady_ = false;
    float hintBlink_ = 0;  // 音の案内の点滅用タイマー (秒)
};
