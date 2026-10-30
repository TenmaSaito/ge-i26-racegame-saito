// ============================================================================
// selectscene.cpp ― セレクトシーン (SelectScene) の実装
// 操作: ↑↓ (またはパッド上下) で項目、←→ で値を変える。1/2/3 でも車種。
//       決定で設定を反映してレースへ、Esc でタイトルへ戻る。
//   項目: STAGE (コース) / CAR (車種) / RIVALS (SOLO・1〜3 台) / AI LEVEL
// ============================================================================

#include "selectscene.hpp"

#include "carobject.hpp"  // CarObject (Find の結果)
#include "game.hpp"
#include "hud.hpp"  // Hud::drawSelect
#include "input.hpp"

#include <cmath>

void SelectScene::enter() {
    game_.silenceAudio();  // レース外ではエンジン音・ドリフト音を止める
    t_ = 0;
    // 現在の設定から選択位置を初期化する (2 回目以降も前回の設定のまま)。
    menu_ = SelectMenu{};
    for (int i = 0; i < kCourseCount; ++i) {
        if (game_.courseStem() == kCourses[i].stem) {
            menu_.course = i;
        }
    }
    menu_.spec = game_.carSpec();
    menu_.rivals = game_.rivalCount();
    menu_.level = int(game_.aiLevel());
    game_.requestSharedRecords();  // 対戦相手のモデルにする共有記録を先に取りに行く
}

void SelectScene::adjust(int row, int dir) {
    auto wrap = [](int v, int n) { return (v % n + n) % n; };
    switch (row) {
    case 0: menu_.course = wrap(menu_.course + dir, kCourseCount); break;
    case 1: menu_.spec = wrap(menu_.spec + dir, kCarSpecCount); break;
    case 2: menu_.rivals = wrap(menu_.rivals + dir, Game::kMaxRivals + 1); break;
    case 3: menu_.level = wrap(menu_.level + dir, kAiLevelCount); break;
    default: break;
    }
}

void SelectScene::frame(double dt) {
    UiActions ui = input::pollUi(false);
    if (ui.quit) {
        game_.requestScene(SceneId::Title);  // Esc はタイトルへ戻る
        return;
    }
    // 上下 = 項目、左右 = 値 (pollUi の調整系チャンネルを流用)。
    // ソロのときは AI の強さの行を飛ばす (選んでも意味がないため)。
    const int rows = menu_.rivals > 0 ? SelectMenu::kRows : SelectMenu::kRows - 1;
    if (ui.tuneSelect != 0) {
        menu_.row = (menu_.row + (ui.tuneSelect > 0 ? 1 : -1) + rows) % rows;
    }
    if (menu_.row >= rows) {
        menu_.row = rows - 1;
    }
    if (ui.tuneAdjust != 0) {
        adjust(menu_.row, ui.tuneAdjust > 0 ? 1 : -1);
    }
    if (ui.carSpec >= 0) {
        menu_.spec = ui.carSpec;
    }
    if (ui.confirm) {
        // 車種と対戦設定を適用し、コースが変わっていれば読み直してからレースへ。
        // (レース開始時の resetRace が、この設定で対戦相手を並べる)
        game_.setRivalCount(menu_.rivals);
        game_.setAiLevel(AiLevel(menu_.level));
        if (game_.carSpec() != menu_.spec) {
            game_.applyCarSpec(menu_.spec);
        }
        if (game_.courseStem() != kCourses[menu_.course].stem) {
            game_.loadCourse(kCourses[menu_.course].stem);
        }
        game_.requestScene(SceneId::Race);
        return;
    }
    t_ += dt;

    // 背景はタイトルと同じアトラクト周回 (現在のコースの車を映す)。
    const CarObject* player = static_cast<const CarObject*>(game_.objects().find("Player"));
    const Car& car = player ? player->car() : game_.car();
    float ang = float(t_) * 0.22f;
    Orbit& camera = game_.camera();
    camera.eye = {car.x + std::sin(ang) * 8.0f, car.y + 2.4f, car.z + std::cos(ang) * 8.0f};
    camera.look = {car.x, car.y + 0.7f, car.z};
    camera.ready = true;

    game_.beginWorldDraw();
    game_.drawWorld({car.x, car.y, car.z}, car.yaw);
    game_.hud().drawSelect(game_, menu_, float(t_));
}
