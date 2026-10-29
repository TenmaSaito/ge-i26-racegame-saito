#pragma once
// ============================================================================
// scene.hpp ― シーンの基底インターフェース (IScene) とシーン識別子
// 役割: タイトル／レース／リザルトといった「画面の状態」を、共通の
//       インターフェースで差し替えられるようにする。Game は現在の
//       シーンを 1 つ持ち、毎フレーム frame() を呼ぶだけ。シーンの
//       切り替えは Game::requestScene で予約し、フレームの頭で安全に
//       行う (描画の途中でシーンが消えないようにするため)。
// ============================================================================

// シーンの識別子。Game::requestScene に渡す。
enum class SceneId {
    Title,   // タイトル (アトラクトカメラ＋スタート待ち)
    Select,  // ステージ選択 + 車種選択
    Race,    // レース本編
    Result,  // リザルト (タイム表示)
};

// 選択できるコースの一覧 (stem はファイル名と保存キーを兼ねる)。
struct CourseInfo {
    const char* stem;  // コースファイルの stem (例: "suzuka")
    const char* name;  // 表示名 (例: "Suzuka Circuit")
};
inline constexpr CourseInfo kCourses[] = {
    {"suzuka", "Suzuka Circuit"},
    {"flow", "Flow Circuit"},
    {"coast", "Coast Circuit"},
};
inline constexpr int kCourseCount = 3;

// レースの周回数 (この周回数を完了したらリザルトへ)。
inline constexpr int kRaceLaps = 3;

// 全シーン共通の基底。状態は派生クラスの中に閉じる。
class IScene {
public:
    virtual ~IScene() = default;
    // シーンに入った瞬間に 1 回呼ばれる (状態のリセット等)。
    virtual void enter() {}
    // シーンを出る瞬間に 1 回呼ばれる (後片付けがあれば)。
    virtual void exit() {}
    // 1 フレームの進行 (入力→更新→描画)。dt は経過秒。
    virtual void frame(double dt) = 0;
};
