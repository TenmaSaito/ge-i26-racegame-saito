#pragma once
// ============================================================================
// object.hpp ― ゲームオブジェクトの基底 (IObject) と一括管理 (ObjectManager)
// 役割: 車・木・家・建物のように種類の違うオブジェクトを、共通の基底で
//       ひとつの入れ物に集めて扱えるようにする。個別に変数で持つと
//       「あれはどこにある？」が種類ごとにバラバラになるため、名前で
//       探す (find)、型でまとめて取る (findAllOfType)、まとめて更新・
//       描画する (updateAll / drawAll) をここに集約する。
// 注意: 煙のパフのような「数千個の短命な粒子」はヒープ確保の単体
//       オブジェクトにせず、Effects のプール (構造体の配列) で管理する。
//       オブジェクト化が向くのは、名前や種類で参照したい実体の方。
// ============================================================================

#include <memory>
#include <string>
#include <utility>
#include <vector>

struct IRenderer;  // 描画ポート (render.hpp)。draw の引数にだけ使う

// 全オブジェクト共通の基底。状態は派生クラスの中に閉じる。
class IObject {
public:
    virtual ~IObject() = default;

    // 毎フレームの更新。動かない物 (景観など) は既定の何もしないでよい。
    virtual void update(float /*dt*/) {}
    // 描画。メッシュ焼き込み済みの物 (景観) は既定の何もしないでよい。
    virtual void draw(IRenderer& /*renderer*/) {}

    // find 用の名前。マネージャへの登録時に付けられる。
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    // 有効フラグ。false の間は updateAll / drawAll から外れる。
    bool active() const { return active_; }
    void setActive(bool v) { active_ = v; }

    // 破棄予約。実際の削除はマネージャが安全なタイミング (一括処理の後)
    // で行うので、更新の最中に自分や他者を消しても壊れない。
    void destroy() { dead_ = true; }
    bool dead() const { return dead_; }

private:
    std::string name_;     // find のキー
    bool active_ = true;   // 一括処理の対象か
    bool dead_ = false;    // 破棄予約が入っているか
};

// オブジェクトの一括管理。所有はここ (unique_ptr) で、利用側は生ポインタ
// の借り物として扱う。シーンを跨いで生きる物は Game がこれを保持する。
class ObjectManager {
public:
    // オブジェクトを生成して登録し、参照を返す。名前は find のキー。
    // 同名が複数あってもよい (find は最初の 1 つを返す)。
    template <typename T, typename... Args>
    T& add(const std::string& name, Args&&... args) {
        auto obj = std::make_unique<T>(std::forward<Args>(args)...);
        obj->setName(name);
        T& ref = *obj;
        objects_.push_back(std::move(obj));
        return ref;
    }
    // 既に作られたオブジェクトの所有を引き取って登録する。
    IObject& addOwned(std::unique_ptr<IObject> obj, const std::string& name) {
        obj->setName(name);
        IObject& ref = *obj;
        objects_.push_back(std::move(obj));
        return ref;
    }

    // 名前で探す (Unity の GameObject.Find に相当)。無ければ nullptr。
    IObject* find(const std::string& name) const;
    // 型で探す (最初の 1 つ)。無ければ nullptr。
    template <typename T>
    T* findOfType() const {
        for (const auto& obj : objects_) {
            if (auto* hit = dynamic_cast<T*>(obj.get())) {
                return hit;
            }
        }
        return nullptr;
    }
    // 型でまとめて全部取る (木だけ、家だけ、といった一括処理用)。
    template <typename T>
    std::vector<T*> findAllOfType() const {
        std::vector<T*> out;
        for (const auto& obj : objects_) {
            if (auto* hit = dynamic_cast<T*>(obj.get())) {
                out.push_back(hit);
            }
        }
        return out;
    }
    // 型ごとの個数 (確保なしの軽い集計。デバッグ表示用)。
    template <typename T>
    int countOfType() const {
        int n = 0;
        for (const auto& obj : objects_) {
            if (dynamic_cast<const T*>(obj.get())) {
                ++n;
            }
        }
        return n;
    }

    // 名前で探して破棄予約を入れる (実際の削除は次の sweep)。
    void destroy(const std::string& name);
    // 登録されている全オブジェクト数。
    size_t count() const { return objects_.size(); }
    // 一括更新 (有効な物だけ)。終わりに破棄予約済みを掃除する。
    void updateAll(float dt);
    // 一括描画 (有効な物だけ)。
    void drawAll(IRenderer& renderer);
    // 全部消す。
    void clear() { objects_.clear(); }

private:
    // 破棄予約 (dead) 済みのオブジェクトを実際に取り除く。
    void sweep();

    std::vector<std::unique_ptr<IObject>> objects_;  // 所有する全オブジェクト
};
