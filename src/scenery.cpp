#include "scenery.hpp"

#include <cmath>

namespace {

unsigned treeHash(int index, float side) {
    unsigned h = unsigned(index + 1) * 2654435761u;
    if (side > 0) h ^= 0x9E3779B9u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

}  // namespace

void TreeObject::build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed) const {
    const Vec3 trunk{0.32f, 0.2f, 0.12f};
    const Vec3 leafA{0.10f, 0.34f, 0.12f};
    const Vec3 leafB{0.13f, 0.42f, 0.15f};
    const float w = 0.16f * scale;
    const float trunkH = 1.6f * scale;
    mesh.addBox({base.x - w, base.y, base.z - w},
                {base.x + w, base.y + trunkH, base.z + w}, trunk);
    const float r1 = 1.05f * scale;
    const float r2 = 0.72f * scale;
    const float lean = (hash01(seed ^ 0x51u) - 0.5f) * 0.5f * scale;
    mesh.addBox({base.x - r1, base.y + trunkH, base.z - r1},
                {base.x + r1, base.y + trunkH + 1.9f * scale, base.z + r1}, leafA);
    mesh.addBox({base.x + lean - r2, base.y + trunkH + 1.5f * scale, base.z - r2},
                {base.x + lean + r2, base.y + trunkH + 3.1f * scale, base.z + r2}, leafB);
}

void BuildingObject::build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed) const {
    // 静かな色の箱ビル。本体 + 少し大きい屋根キャップの 2 段構成。
    static const Vec3 palette[4] = {
        {0.62f, 0.64f, 0.66f},  // 白グレー
        {0.52f, 0.55f, 0.62f},  // 青グレー
        {0.66f, 0.60f, 0.52f},  // ベージュ
        {0.45f, 0.52f, 0.55f},  // ティールグレー
    };
    const Vec3 body = palette[seed % 4u];
    const Vec3 roof{body.x * 0.55f, body.y * 0.55f, body.z * 0.6f};
    const float w = (4.5f + hash01(seed ^ 0xA1u) * 3.0f) * scale;   // 半幅
    const float d = (4.0f + hash01(seed ^ 0xB2u) * 2.5f) * scale;   // 半奥行
    const float h = (5.0f + hash01(seed ^ 0xC3u) * 7.0f) * scale;   // 高さ
    mesh.addBox({base.x - w, base.y, base.z - d}, {base.x + w, base.y + h, base.z + d}, body);
    mesh.addBox({base.x - w - 0.3f, base.y + h, base.z - d - 0.3f},
                {base.x + w + 0.3f, base.y + h + 0.5f, base.z + d + 0.3f}, roof);
}

Vec3 SceneryBuilder::rightAt(int index) const {
    const TrackSample& a = track_.samples[size_t(index)];
    const TrackSample& b = track_.samples[size_t((index + 1) % int(track_.samples.size()))];
    Vec3 fwd{b.x - a.x, 0, b.z - a.z};
    fwd = normalize(fwd);
    return Vec3{fwd.z, 0, -fwd.x};
}

Vec3 SceneryBuilder::pointAt(int index, float lateral) const {
    const TrackSample& s = track_.samples[size_t(index)];
    Vec3 r = rightAt(index);
    return {s.x + r.x * lateral, s.y + lateral * s.cross, s.z + r.z * lateral};
}

float SceneryBuilder::edgeY(int index, float side) const {
    const float outer = track_.halfWidth + track_.curbWidth + track_.runoff;
    return pointAt(index, side * outer).y - 0.4f;
}

bool SceneryBuilder::clearOfOtherRoad(Vec3 pos, float radius, float baseY, int selfIndex) const {
    const int n = int(track_.samples.size());
    const float clear = track_.halfWidth + radius;
    for (int j = 0; j < n; j += 4) {
        int d = std::abs(j - selfIndex);
        d = std::min(d, n - d);
        if (d < 24) continue;
        const TrackSample& o = track_.samples[size_t(j)];
        const float dx = o.x - pos.x;
        const float dz = o.z - pos.z;
        if (dx * dx + dz * dz < clear * clear && std::fabs(o.y - baseY) < 4.0f) return false;
        if (dx * dx + dz * dz < 14.0f * 14.0f) return false;
    }
    return true;
}

// コーナー区間だけ、コンクリート壁の手前に金属ガードレールを立てる。
// ビームは帯状クワッド、支柱は約 8m ごとの小箱。
void SceneryBuilder::buildGuardrails(CpuMesh& mesh, int from, int to) const {
    const int n = int(track_.samples.size());
    const Vec3 steel{0.72f, 0.75f, 0.80f};
    const Vec3 post{0.42f, 0.44f, 0.47f};
    const float lat = track_.halfWidth + track_.curbWidth + track_.runoff - 0.55f;
    auto cornerNear = [&](int i) {
        for (int k = -2; k <= 2; ++k)
            if (track_.samples[size_t((i + k + n) % n)].corner != 0) return true;
        return false;
    };
    for (int i = from; i < to; ++i) {
        if (!cornerNear(i)) continue;
        const int j = (i + 1) % n;
        for (int s = -1; s <= 1; s += 2) {
            const float side = float(s);
            Vec3 a = pointAt(i, side * lat);
            Vec3 b = pointAt(j, side * lat);
            a.y -= 0.05f;
            b.y -= 0.05f;
            Vec3 r = rightAt(i);
            if (side < 0) r = r * -1.0f;
            // ビーム（外向きの面 + 上面）
            mesh.addQuad({a.x, a.y + 0.32f, a.z}, {b.x, b.y + 0.32f, b.z},
                         {b.x, b.y + 0.74f, b.z}, {a.x, a.y + 0.74f, a.z}, r, steel);
            mesh.addQuad({a.x, a.y + 0.74f, a.z}, {b.x, b.y + 0.74f, b.z},
                         {b.x + r.x * 0.06f, b.y + 0.74f, b.z + r.z * 0.06f},
                         {a.x + r.x * 0.06f, a.y + 0.74f, a.z + r.z * 0.06f},
                         {0, 1, 0}, steel);
            if (i % 4 == 0) {
                mesh.addBox({a.x - 0.07f, a.y, a.z - 0.07f},
                            {a.x + 0.07f, a.y + 0.6f, a.z + 0.07f}, post);
            }
        }
    }
}

std::vector<SceneryChunk> SceneryBuilder::build(int samplesPerChunk) const {
    const int n = int(track_.samples.size());
    const int chunkCount = (n + samplesPerChunk - 1) / samplesPerChunk;
    std::vector<SceneryChunk> chunks;
    chunks.resize(size_t(chunkCount));
    const float grassOuter = track_.halfWidth + track_.curbWidth + track_.runoff + 0.8f;
    TreeObject tree;
    BuildingObject building;
    for (int i = 0; i < n; ++i) {
        const int ci = i / samplesPerChunk;
        CpuMesh& mesh = chunks[size_t(ci)].mesh;
        for (int s = -1; s <= 1; s += 2) {
            const float side = float(s);
            const float baseY = edgeY(i, side);
            // 木: 約 14m 間隔、壁の外 2.5〜15.5m
            const unsigned h = treeHash(i, side);
            if (i % 7 == 0 && (h % 100u) < 88u) {
                const float lat = grassOuter + 2.5f + float((h >> 8) % 100u) / 100.0f * 13.0f;
                Vec3 pos = pointAt(i, side * lat);
                pos.y = baseY;
                if (clearOfOtherRoad(pos, 6.0f, baseY, i)) {
                    const float scale = 0.85f + float((h >> 16) % 100u) / 100.0f * 0.7f;
                    tree.build(mesh, pos, scale, h);
                }
            }
            // 建物: 約 110m 間隔の候補から 6 割、芝の外 20〜50m
            if (i % 55 == 0 && (h % 100u) < 60u) {
                const float lat = grassOuter + 27.0f + float((h >> 5) % 100u) / 100.0f * 28.0f;
                Vec3 pos = pointAt(i, side * lat);
                pos.y = baseY;
                if (clearOfOtherRoad(pos, 17.0f, baseY, i)) {
                    building.build(mesh, pos, 1.0f, h ^ 0x77u);
                }
            }
        }
    }
    for (int c = 0; c < chunkCount; ++c) {
        const int from = c * samplesPerChunk;
        const int to = std::min(n, from + samplesPerChunk);
        buildGuardrails(chunks[size_t(c)].mesh, from, to);
        const TrackSample& mid = track_.samples[size_t((from + to) / 2)];
        chunks[size_t(c)].center = {mid.x, mid.y, mid.z};
    }
    return chunks;
}
