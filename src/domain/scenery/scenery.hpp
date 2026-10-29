#pragma once

#include "meshbuild.hpp"
#include "object.hpp"  // IObject / ObjectManager (一括管理)
#include "track.hpp"

#include <vector>

// 路傍オブジェクト（木・建物など）の基底クラス。
// base は接地位置、scale は大きさ、seed はバリエーション決定用のハッシュ、
// groundY は背景の地面（一枚板）の高さ。建物・家は groundY まで届く基礎を
// 付けて、坂の上に置いても空中に浮いて見えないようにする。
class SceneryObject {
public:
    virtual ~SceneryObject() = default;
    virtual void build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed, float groundY) const = 0;

protected:
    static constexpr float hash01(unsigned h) {
        h ^= h >> 16;
        h *= 2246822519u;
        h ^= h >> 13;
        return float(h & 0xFFFFFFu) / float(0x1000000u);
    }
};

class TreeObject : public SceneryObject {
public:
    void build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed, float groundY) const override;
};

class BuildingObject : public SceneryObject {
public:
    void build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed, float groundY) const override;
};

// 住宅（壁 + 切妻屋根）。住宅街はこれをまとめて配置する。
class HouseObject : public SceneryObject {
public:
    void build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed, float groundY) const override;
};

// 景観の種類。インスタンスの判別と、焼き込み時の形状生成器の選択に使う。
enum class SceneryKind { Tree, Building, House };

// 景観 1 件分の「実体」(IObject)。木・家・建物を個別の変数や配列で
// バラバラに持たず、ObjectManager に登録して一括管理するための単位。
// 位置・大きさ・バリエーション種・所属チャンクだけを持ち、頂点化は
// bake() が種類に応じた生成器 (TreeObject 等) へ委譲する。
class SceneryInstance : public IObject {
public:
    SceneryInstance(SceneryKind kind, Vec3 pos, float scale, unsigned seed, int chunk)
        : kind_(kind), pos_(pos), scale_(scale), seed_(seed), chunk_(chunk) {}

    SceneryKind kind() const { return kind_; }  // 種類 (木/建物/家)
    Vec3 pos() const { return pos_; }           // 接地位置
    int chunk() const { return chunk_; }        // 所属チャンク番号

    // 自分の形状をチャンクのメッシュへ焼き込む。groundY は基礎の到達先。
    void bake(CpuMesh& mesh, float groundY) const;

private:
    SceneryKind kind_;   // 種類
    Vec3 pos_;           // 接地位置 (base)
    float scale_;        // 大きさ倍率
    unsigned seed_;      // バリエーション決定用のハッシュ
    int chunk_;          // 焼き込み先チャンクの番号
};

// 種類ごとの個数 (デバッグ表示用)。
struct SceneryCounts {
    int trees = 0;
    int houses = 0;
    int buildings = 0;
};
SceneryCounts countScenery(const ObjectManager& objects);

// 景観メッシュのひとかたまり（チャンク）。中心は描画カリング用。
struct SceneryChunk {
    CpuMesh mesh;
    Vec3 center{0, 0, 0};
};

// コース全体の景観（木・建物・ガードレール）を、サンプル区間ごとに
// チャンク分割して構築する。チャンク単位で距離カリングできるので、
// 全周を 1 メッシュで毎フレーム描くより頂点処理が大幅に減る。
class SceneryBuilder {
public:
    explicit SceneryBuilder(const Track& track) : track_(track) {}

    // 配置した景観は 1 件ずつ SceneryInstance として objects へ登録し
    // (一括管理の実体)、その実体からチャンクメッシュを焼き込む。
    std::vector<SceneryChunk> build(ObjectManager& objects, int samplesPerChunk = 120) const;
    // 背景の山並み（コースをぐるりと囲む稜線リング）。スカイボックス代わり。
    CpuMesh buildBackdrop() const;
    // 地形の高さ (x, z 地点)。コース近傍は路面中心の高さに張り付き、
    // 離れるほど滑らかな丘陵が盛り上がる。木や基礎の接地にも使う。
    float terrainHeightAt(float x, float z) const;
    // 1 区間だけを見た地形高 (terrainHeightAt が 2 区間をブレンドする)。
    float legProfile(const TrackPose& q) const;

private:
    const Track& track_;

    // 丘 1 つの配置。木のある辺りに滑らかな盛り上がりを作るための素。
    struct Bump {
        float x, z;      // 中心 (ワールド座標)
        float radius;    // 広がり (m)
        float height;    // 高さ (m)
    };
    mutable std::vector<Bump> bumps_;   // 丘の配置 (初回に決めて使い回す)
    mutable bool bumpsReady_ = false;   // 丘の配置が済んだか
    void ensureBumps() const;           // 丘の配置を決める (決定的)

    Vec3 rightAt(int index) const;
    Vec3 pointAt(int index, float lateral) const;
    float edgeY(int index, float side) const;  // 芝リボンの高さ（縁石基準）
    // コースの別区間が近くを通る場所（立体交差など）には置かない。
    bool clearOfOtherRoad(Vec3 pos, float radius, float baseY, int selfIndex) const;
    void buildGuardrails(CpuMesh& mesh, int from, int to) const;
};
