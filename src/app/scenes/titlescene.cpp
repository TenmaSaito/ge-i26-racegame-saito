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
    game_.silenceAudio();  // レース外ではエンジン音・ドリフト音を止める
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
    // カメラの目: 車の周りを回す。ただし隣の区間の斜面へ出ると車が隠れる
    // (スイッチバックでは特に起きる) ので、車のいる路面から横に 17m 以上
    // 離れたら車側へ寄せ、上限付きで地形より少しだけ持ち上げる。
    const float carS = game_.track().queryPlanar(car.x, car.z).s;
    float dist = 9.0f;
    float ground = car.y;
    for (int k = 0; k < 6; ++k) {
        camera.eye = {car.x + std::sin(ang) * dist, car.y + 3.2f, car.z + std::cos(ang) * dist};
        TrackPose q = game_.track().query(camera.eye.x, camera.eye.z, car.y, carS);
        ground = game_.terrainHeightAt(camera.eye.x, camera.eye.z);
        // 条件: 車の路面から横に離れすぎず、足元の地形が高すぎないこと。
        // どちらか崩れたら車へ寄せ直す (車の真上は必ず平らな回廊)。
        if ((!q.found || std::fabs(q.lateral) <= 17.0f) && ground <= car.y + 6.0f) {
            break;
        }
        dist *= 0.65f;
    }
    if (camera.eye.y < ground + 2.2f) {
        camera.eye.y = std::min(ground + 2.2f, car.y + 8.0f);
    }
    camera.look = {car.x, car.y + 0.7f, car.z};
    camera.ready = true;

    // 描画: ワールドはレースと共通、文字は Hud のタイトル表示。
    game_.beginWorldDraw();
    game_.drawWorld({car.x, car.y, car.z}, car.yaw);
    game_.hud().drawTitle(game_, float(t_));
}
