#pragma once

#include "meshbuild.hpp"
#include "track.hpp"

#include <vector>

// 路傍オブジェクト（木・建物など）の基底クラス。
// base は接地位置、scale は大きさ、seed はバリエーション決定用のハッシュ。
class SceneryObject {
public:
    virtual ~SceneryObject() = default;
    virtual void build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed) const = 0;

protected:
    static float hash01(unsigned h) {
        h ^= h >> 16;
        h *= 2246822519u;
        h ^= h >> 13;
        return float(h & 0xFFFFFFu) / float(0x1000000u);
    }
};

class TreeObject : public SceneryObject {
public:
    void build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed) const override;
};

class BuildingObject : public SceneryObject {
public:
    void build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed) const override;
};

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

    std::vector<SceneryChunk> build(int samplesPerChunk = 120) const;

private:
    const Track& track_;

    Vec3 rightAt(int index) const;
    Vec3 pointAt(int index, float lateral) const;
    float edgeY(int index, float side) const;  // 芝リボンの高さ（縁石基準）
    // コースの別区間が近くを通る場所（立体交差など）には置かない。
    bool clearOfOtherRoad(Vec3 pos, float radius, float baseY, int selfIndex) const;
    void buildGuardrails(CpuMesh& mesh, int from, int to) const;
};
