// ============================================================================
// selectscene.cpp ― セレクトシーン (SelectScene) の実装
// 操作: ←→ (またはパッド左右) でコース、↑↓ で車種、1/2/3 でも車種、
//       決定で読み直してレースへ、Esc でタイトルへ戻る。
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
    // 現在のコースと車種から選択位置を初期化する (2 回目以降も自然に)。
    course_ = 0;
    for (int i = 0; i < kCourseCount; ++i) {
        if (game_.courseStem() == kCourses[i].stem) {
            course_ = i;
        }
    }
    spec_ = game_.carSpec();
}

void SelectScene::frame(double dt) {
    UiActions ui = input::pollUi(false);
    if (ui.quit) {
        game_.requestScene(SceneId::Title);  // Esc はタイトルへ戻る
        return;
    }
    // 左右 = コース、上下 = 車種 (pollUi の調整系チャンネルを流用)。
    if (ui.tuneAdjust > 0) {
        course_ = (course_ + 1) % kCourseCount;
    } else if (ui.tuneAdjust < 0) {
        course_ = (course_ + kCourseCount - 1) % kCourseCount;
    }
    if (ui.tuneSelect > 0) {
        spec_ = (spec_ + 1) % 3;
    } else if (ui.tuneSelect < 0) {
        spec_ = (spec_ + 2) % 3;
    }
    if (ui.carSpec >= 0) {
        spec_ = ui.carSpec;
    }
    if (ui.confirm) {
        // コースが変わっていれば読み直し、車種を適用してからレースへ。
        if (game_.courseStem() != kCourses[course_].stem) {
            game_.loadCourse(kCourses[course_].stem);
        }
        if (game_.carSpec() != spec_) {
            game_.applyCarSpec(spec_);
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
    game_.hud().drawSelect(game_, course_, spec_, float(t_));
}
