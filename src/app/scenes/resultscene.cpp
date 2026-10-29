// ============================================================================
// resultscene.cpp ― リザルトシーン (ResultScene) の実装
// ============================================================================

#include "resultscene.hpp"

#include "game.hpp"
#include "carobject.hpp"  // CarObject (Find の結果)
#include "hud.hpp"  // Hud (drawTitle / drawResult / draw)
#include "input.hpp"

#include <cmath>

void ResultScene::enter() {
    t_ = 0;
    rank_ = game_.recordResult();  // ゴール結果をランキングへ登録 (保存まで)
    // 車はゴール地点で止まったまま。カメラは次のフレームから周回させる。
    game_.camera().ready = true;
}

void ResultScene::frame(double dt) {
    // 入力: 決定 = もう一度レース、T = タイトルへ、Esc = 終了。
    UiActions ui = input::pollUi(false);
    if (ui.quit) {
        game_.requestQuit();
    }
    if (ui.confirm) {
        game_.requestScene(SceneId::Race);
        return;
    }
    if (ui.tuningToggle) {
        game_.requestScene(SceneId::Title);
        return;
    }
    t_ += dt;

    // 背景カメラ: ゴール地点の車の周りをゆっくり回す (タイトルと同じ動き)。
    // プレイヤー車は名前で引く (オブジェクトの一括管理から検索)。
    const CarObject* player = static_cast<const CarObject*>(game_.objects().find("Player"));
    const Car& car = player ? player->car() : game_.car();
    float ang = float(t_) * 0.22f;
    Orbit& camera = game_.camera();
    camera.eye = {car.x + std::sin(ang) * 8.0f, car.y + 2.4f, car.z + std::cos(ang) * 8.0f};
    camera.look = {car.x, car.y + 0.7f, car.z};
    camera.ready = true;

    game_.beginWorldDraw();
    game_.drawWorld({car.x, car.y, car.z}, car.yaw);
    game_.hud().drawResult(game_, float(t_), rank_);
}
