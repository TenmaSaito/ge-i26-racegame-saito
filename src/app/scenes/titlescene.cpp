// ============================================================================
// titlescene.cpp ― タイトルシーン (TitleScene) の実装
// ============================================================================

#include "titlescene.hpp"

#include "game.hpp"
#include "carobject.hpp"  // CarObject (Find の結果)
#include "hud.hpp"  // Hud (drawTitle / drawResult / draw)
#include "input.hpp"

#include <cmath>

void TitleScene::enter() {
    game_.resetRace();  // 車をスタート地点へ (カメラも次のフレームで置き直す)
    t_ = 0;
}

void TitleScene::frame(double dt) {
    // 入力: 決定でレースへ、終了要求 (Esc) はアプリ終了。
    UiActions ui = input::pollUi(false);
    if (ui.quit) {
        game_.requestQuit();
    }
    if (ui.confirm || ui.click) {
        // タイトルだけはクリックでも進める (最初のフォーカス取りを兼ねる)。
        game_.requestScene(SceneId::Select);  // まずステージ/車種選択へ
        return;
    }
    t_ += dt;

    // アトラクトカメラ: スタート地点の車の周りをゆっくり 1 周する。
    // レースの追跡カメラ (Orbit) は使わず、目と注視点を直接置く。
    // プレイヤー車は名前で引く (オブジェクトの一括管理から検索)。
    const CarObject* player = static_cast<const CarObject*>(game_.objects().find("Player"));
    const Car& car = player ? player->car() : game_.car();
    float ang = float(t_) * 0.22f;
    Orbit& camera = game_.camera();
    camera.eye = {car.x + std::sin(ang) * 8.0f, car.y + 2.4f, car.z + std::cos(ang) * 8.0f};
    camera.look = {car.x, car.y + 0.7f, car.z};
    camera.ready = true;

    // 描画: ワールドはレースと共通、文字は Hud のタイトル表示。
    game_.beginWorldDraw();
    game_.drawWorld({car.x, car.y, car.z}, car.yaw);
    game_.hud().drawTitle(game_, float(t_));
}
