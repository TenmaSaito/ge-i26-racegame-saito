#include "scenery.hpp"

#include <string>

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

void TreeObject::build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed, float groundY) const {
    (void)groundY;  // 木は幹が細く接地が目立たないので基礎は付けない
    const Vec3 trunk{0.32f, 0.2f, 0.12f};
    const Vec3 leafA{0.10f, 0.34f, 0.12f};
    const Vec3 leafB{0.13f, 0.42f, 0.15f};
    // 個体差: 背の高さ (hMul) と樹冠の広がり (cMul) を別々に振る。
    // 全体スケールだけだと「同じ形の大小」にしかならず、並んだときに
    // コピペ感が出るため、縦横比も変えてシルエットをばらけさせる。
    const float hMul = 0.8f + hash01(seed ^ 0x33u) * 0.7f;   // 0.8〜1.5
    const float cMul = 0.85f + hash01(seed ^ 0x44u) * 0.5f;  // 0.85〜1.35
    const float w = 0.16f * scale;
    const float trunkH = 1.6f * scale * hMul;
    mesh.addBox({base.x - w, base.y, base.z - w},
                {base.x + w, base.y + trunkH, base.z + w}, trunk);
    const float r1 = 1.05f * scale * cMul;
    const float r2 = 0.72f * scale * cMul;
    const float lean = (hash01(seed ^ 0x51u) - 0.5f) * 0.5f * scale;
    mesh.addBox({base.x - r1, base.y + trunkH, base.z - r1},
                {base.x + r1, base.y + trunkH + 1.9f * scale * hMul, base.z + r1}, leafA);
    mesh.addBox({base.x + lean - r2, base.y + trunkH + 1.5f * scale * hMul, base.z - r2},
                {base.x + lean + r2, base.y + trunkH + 3.1f * scale * hMul, base.z + r2}, leafB);
}

void BuildingObject::build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed, float groundY) const {
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
    // 基礎: 建物の底面は平らなので、地面まで届く土台がないと坂の上で
    // 空中に浮いて見える。フットプリントより少し大きいコンクリートの
    // 箱を地面 (groundY) まで伸ばし、上端を床として見せる。
    if (groundY < base.y - 0.05f) {
        const Vec3 concrete{0.48f, 0.47f, 0.44f};
        mesh.addBox({base.x - w - 0.5f, groundY, base.z - d - 0.5f},
                    {base.x + w + 0.5f, base.y + 0.12f, base.z + d + 0.5f}, concrete);
    }
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

std::vector<SceneryChunk> SceneryBuilder::build(ObjectManager& objects, int samplesPerChunk) const {
    const int n = int(track_.samples.size());
    // 背景の地面（一枚板）の高さ。buildBackdrop と同じ基準 (最低標高 - 4m)
    // より 1m 下まで基礎を伸ばせば、どこから見ても必ず地面に届く。
    float minY = 1e9f;
    for (const TrackSample& s : track_.samples) {
        minY = std::min(minY, s.y);
    }
    const float groundY = minY - 5.0f;
    const int chunkCount = (n + samplesPerChunk - 1) / samplesPerChunk;
    std::vector<SceneryChunk> chunks;
    chunks.resize(size_t(chunkCount));
    const float grassOuter = track_.halfWidth + track_.curbWidth + track_.runoff + 0.8f;
    // 第 1 パス: 配置の決定だけを行い、1 件ずつ SceneryInstance として
    // マネージャへ登録する (名前は種類 + 連番で、Find でも引ける)。
    int treeNo = 0, buildingNo = 0, houseNo = 0;  // 名前付け用の連番
    for (int i = 0; i < n; ++i) {
        const int ci = i / samplesPerChunk;
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
                    // 大きさは 0.6〜2.1 倍まで大きく振る（形の個体差は TreeObject 側）。
                    const float scale = 0.6f + float((h >> 16) % 100u) / 100.0f * 1.5f;
                    objects.add<SceneryInstance>("Tree#" + std::to_string(treeNo++),
                                                 SceneryKind::Tree, pos, scale, h, ci);
                }
            }
            // 建物: 約 110m 間隔の候補から 6 割、芝の外 20〜50m
            if (i % 55 == 0 && (h % 100u) < 60u) {
                const float lat = grassOuter + 27.0f + float((h >> 5) % 100u) / 100.0f * 28.0f;
                Vec3 pos = pointAt(i, side * lat);
                pos.y = baseY;
                if (clearOfOtherRoad(pos, 17.0f, baseY, i)) {
                    objects.add<SceneryInstance>("Building#" + std::to_string(buildingNo++),
                                                 SceneryKind::Building, pos, 1.0f, h ^ 0x77u, ci);
                }
            }
            // 住宅街: 約 180m 間隔の候補に、5〜9軒の小さな集落を作る。
            if (i % 90 == 0 && (h % 100u) < 55u) {
                const float clat = grassOuter + 55.0f + float((h >> 11) % 100u) / 100.0f * 55.0f;
                Vec3 cpos = pointAt(i, side * clat);
                const float cbaseY = edgeY(i, side);
                const int count = 5 + int((h >> 20) % 5u);
                for (int u = 0; u < count; ++u) {
                    unsigned hh = treeHash(i * 31 + u * 7, side);
                    Vec3 hp{cpos.x + (float((hh >> 8) % 1000u) / 1000.0f - 0.5f) * 46.0f, cbaseY - 0.3f,
                            cpos.z + (float((hh >> 18) % 1000u) / 1000.0f - 0.5f) * 46.0f};
                    if (clearOfOtherRoad(hp, 12.0f, cbaseY, i)) {
                        objects.add<SceneryInstance>("House#" + std::to_string(houseNo++),
                                                     SceneryKind::House, hp, 1.0f, hh, ci);
                    }
                }
            }
        }
    }
    // 第 2 パス: 登録済みインスタンスをチャンクごとに集めて焼き込む。
    // 実体の一覧が唯一の正で、メッシュはそこから作る従属物という関係。
    const std::vector<SceneryInstance*> instances = objects.findAllOfType<SceneryInstance>();
    for (SceneryInstance* inst : instances) {
        inst->bake(chunks[size_t(inst->chunk())].mesh, groundY);
    }
    for (int c = 0; c < chunkCount; ++c) {
        const int from = c * samplesPerChunk;
        const int to = std::min(n, from + samplesPerChunk);
        buildGuardrails(chunks[size_t(c)].mesh, from, to);  // レールは線状地物なので実体化しない
        const TrackSample& mid = track_.samples[size_t((from + to) / 2)];
        chunks[size_t(c)].center = {mid.x, mid.y, mid.z};
    }
    return chunks;
}

void HouseObject::build(CpuMesh& mesh, Vec3 base, float scale, unsigned seed, float groundY) const {
    static const Vec3 wallPalette[4] = {
        {0.82f, 0.80f, 0.76f}, {0.78f, 0.72f, 0.64f},
        {0.72f, 0.76f, 0.78f}, {0.84f, 0.78f, 0.70f}};
    static const Vec3 roofPalette[3] = {
        {0.36f, 0.38f, 0.44f}, {0.48f, 0.30f, 0.24f}, {0.26f, 0.36f, 0.38f}};
    const Vec3 wall = wallPalette[seed % 4u];
    const Vec3 roof = roofPalette[(seed >> 3) % 3u];
    const float w = (3.0f + hash01(seed ^ 0xD4u) * 1.4f) * scale;   // 半幅
    const float d = (2.6f + hash01(seed ^ 0xE5u) * 1.1f) * scale;   // 半奥行
    const float h = (3.0f + hash01(seed ^ 0xF6u) * 1.4f) * scale;   // 壁の高さ
    const float rh = (1.7f + hash01(seed ^ 0x87u) * 1.0f) * scale;  // 屋根の高さ
    mesh.addBox({base.x - w, base.y, base.z - d}, {base.x + w, base.y + h, base.z + d}, wall);
    const float top = base.y + h;
    const float ridge = top + rh;
    // 切妻屋根: 2 枚の傾斜面 + 両端の三角壁。棟は x 方向。
    Vec3 e0{base.x - w, top, base.z - d}, e1{base.x + w, top, base.z - d};
    Vec3 e2{base.x + w, top, base.z + d}, e3{base.x - w, top, base.z + d};
    Vec3 r0{base.x - w, ridge, base.z}, r1{base.x + w, ridge, base.z};
    mesh.addQuad(e0, e1, r1, r0, {0, 0.5f, -1}, roof);
    mesh.addQuad(e2, e3, r0, r1, {0, 0.5f, 1}, roof);
    mesh.addTriangle(e1, e2, r1, {1, 0, 0}, wall);
    mesh.addTriangle(e3, e0, r0, {-1, 0, 0}, wall);
    // 基礎と床: 住宅街は一軒ごとに地形の高さが違うため、土台なしだと
    // 集落ごと空中に浮く。石積み調の基礎を地面 (groundY) まで伸ばし、
    // 上端の張り出しを「床」として見せる（擁壁のある山側の住宅の見た目）。
    if (groundY < base.y - 0.05f) {
        const Vec3 stone{0.44f, 0.41f, 0.36f};
        mesh.addBox({base.x - w - 0.5f, groundY, base.z - d - 0.5f},
                    {base.x + w + 0.5f, base.y + 0.10f, base.z + d + 0.5f}, stone);
    }
}

// コースを囲む山並み。中心から見た稜線をギザギザの帯で作る。
// 近い側は霞越しに稜線が見え、遠い側は霧に溶ける（スカイボックス相当の役割）。
CpuMesh SceneryBuilder::buildBackdrop() const {
    CpuMesh mesh;
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f, minY = 1e9f;
    for (const TrackSample& s : track_.samples) {
        minX = std::min(minX, s.x); maxX = std::max(maxX, s.x);
        minZ = std::min(minZ, s.z); maxZ = std::max(maxZ, s.z);
        minY = std::min(minY, s.y);
    }
    // 地面の大きな一枚板（路面チャンク側から移設。常時描く）。
    {
        const float pad = 120.0f;
        const float gy = minY - 4.0f;
        const Vec3 gcol{0.12f, 0.27f, 0.11f};
        Vec3 a{minX - pad, gy, minZ - pad};
        Vec3 b{maxX + pad, gy, minZ - pad};
        Vec3 c{maxX + pad, gy, maxZ + pad};
        Vec3 d{minX - pad, gy, maxZ + pad};
        mesh.addQuad(a, d, c, b, {0, 1, 0}, gcol);
    }
    const Vec3 center{(minX + maxX) * 0.5f, 0, (minZ + maxZ) * 0.5f};
    const float baseRadius = std::max(maxX - minX, maxZ - minZ) * 0.5f;
    const int seg = 72;
    const Vec3 colA{0.38f, 0.47f, 0.46f};
    const Vec3 colB{0.33f, 0.42f, 0.44f};
    auto ridgeAt = [&](int k) {
        unsigned h = treeHash(k * 7 + 3, 1.0f);
        float ang = float(k) / float(seg) * 6.2831853f;
        float r = baseRadius + 950.0f + float((h >> 9) % 100u) / 100.0f * 300.0f;
        float y = minY + 55.0f + float((h >> 17) % 100u) / 100.0f * 95.0f;
        return Vec3{center.x + std::sin(ang) * r, y, center.z + std::cos(ang) * r};
    };
    for (int k = 0; k < seg; ++k) {
        Vec3 p0 = ridgeAt(k);
        Vec3 p1 = ridgeAt(k + 1);
        Vec3 b0{p0.x, minY - 40.0f, p0.z};
        Vec3 b1{p1.x, minY - 40.0f, p1.z};
        Vec3 n = normalize(Vec3{-(p0.x + p1.x) * 0.5f + center.x, 0,
                                -(p0.z + p1.z) * 0.5f + center.z});
        const Vec3 c = (k % 2 == 0) ? colA : colB;
        mesh.addQuad(b0, b1, p1, p0, n, c);
    }
    return mesh;
}

// 形状の焼き込み。種類ごとに担当の生成器へ委譲する (生成器は無状態)。
void SceneryInstance::bake(CpuMesh& mesh, float groundY) const {
    static const TreeObject kTree;
    static const BuildingObject kBuilding;
    static const HouseObject kHouse;
    switch (kind_) {
        case SceneryKind::Tree: kTree.build(mesh, pos_, scale_, seed_, groundY); break;
        case SceneryKind::Building: kBuilding.build(mesh, pos_, scale_, seed_, groundY); break;
        case SceneryKind::House: kHouse.build(mesh, pos_, scale_, seed_, groundY); break;
    }
}

// 種類ごとの個数を数える (マネージャから型でまとめて取って振り分ける)。
SceneryCounts countScenery(const ObjectManager& objects) {
    SceneryCounts counts;
    for (const SceneryInstance* inst : objects.findAllOfType<SceneryInstance>()) {
        switch (inst->kind()) {
            case SceneryKind::Tree: counts.trees += 1; break;
            case SceneryKind::Building: counts.buildings += 1; break;
            case SceneryKind::House: counts.houses += 1; break;
        }
    }
    return counts;
}
