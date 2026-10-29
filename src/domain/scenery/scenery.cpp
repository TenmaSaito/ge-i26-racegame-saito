#include "scenery.hpp"

#include <string>

#include <cmath>

namespace {

constexpr unsigned treeHash(int index, float side) {
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
    const Vec3 leafA{0.08f, 0.30f, 0.10f};
    const Vec3 leafB{0.12f, 0.40f, 0.14f};
    // 個体差: 背の高さ (hMul) と樹冠の広がり (cMul) を別々に振る。
    const float hMul = 0.8f + hash01(seed ^ 0x33u) * 0.7f;   // 0.8〜1.5
    const float cMul = 0.85f + hash01(seed ^ 0x44u) * 0.5f;  // 0.85〜1.35
    const float w = 0.16f * scale;
    const float trunkH = 1.5f * scale * hMul;
    mesh.addBox({base.x - w, base.y, base.z - w},
                {base.x + w, base.y + trunkH, base.z + w}, trunk);
    const float lean = (hash01(seed ^ 0x51u) - 0.5f) * 0.5f * scale;
    const Vec3 top{base.x + lean, base.y, base.z};
    if ((seed >> 3) % 10u < 3u) {
        // 3 割は広葉樹 (箱の樹冠 2 段)。森の単調さを崩す。
        const float r1 = 1.05f * scale * cMul;
        const float r2 = 0.72f * scale * cMul;
        mesh.addBox({top.x - r1, top.y + trunkH, top.z - r1},
                    {top.x + r1, top.y + trunkH + 1.9f * scale * hMul, top.z + r1}, leafA);
        mesh.addBox({top.x - r2, top.y + trunkH + 1.5f * scale * hMul, top.z - r2},
                    {top.x + r2, top.y + trunkH + 3.1f * scale * hMul, top.z + r2}, leafB);
        return;
    }
    // 針葉樹: 円錐を 3 段重ねる。8 面の三角ファンでローポリの稜を出す。
    auto cone = [&](float radius, float y0, float y1, Vec3 col) {
        const int seg = 8;
        const float rot = hash01(seed ^ 0x62u) * 3.14159f;
        for (int k = 0; k < seg; ++k) {
            float a0 = rot + float(k) / float(seg) * 6.2831853f;
            float a1 = rot + float(k + 1) / float(seg) * 6.2831853f;
            Vec3 p0{top.x + std::cos(a0) * radius, y0, top.z + std::sin(a0) * radius};
            Vec3 p1{top.x + std::cos(a1) * radius, y0, top.z + std::sin(a1) * radius};
            Vec3 apex{top.x, y1, top.z};
            Vec3 n = normalize(cross(p1 - p0, apex - p0));
            mesh.addTriangle(p0, p1, apex, n, col);
        }
    };
    const float u = scale * hMul;
    cone(1.55f * scale * cMul, base.y + trunkH - 0.25f * u, base.y + trunkH + 1.9f * u, leafA);
    cone(1.12f * scale * cMul, base.y + trunkH + 1.15f * u, base.y + trunkH + 3.0f * u, leafB);
    cone(0.66f * scale * cMul, base.y + trunkH + 2.25f * u, base.y + trunkH + 4.1f * u, leafA);
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
            // 木 (近景): 約 6m 間隔で密に。ガードレールのすぐ外から斜面へ。
            // 高さは地形に合わせ、斜面に立つ木も浮かせない。
            const unsigned h = treeHash(i, side);
            if (i % 3 == 0 && (h % 100u) < 92u) {
                const float lat = grassOuter + 1.5f + float((h >> 8) % 100u) / 100.0f * 36.0f;
                Vec3 pos = pointAt(i, side * lat);
                pos.y = terrainHeightAt(pos.x, pos.z) - 0.15f;
                if (clearOfOtherRoad(pos, 6.0f, pos.y, i)) {
                    // 大きさは 0.6〜2.1 倍まで大きく振る（形の個体差は TreeObject 側）。
                    const float scale = 0.6f + float((h >> 16) % 100u) / 100.0f * 1.5f;
                    objects.add<SceneryInstance>("Tree#" + std::to_string(treeNo++),
                                                 SceneryKind::Tree, pos, scale, h, ci);
                }
            }
            // 木 (中景): 斜面の森。約 8m 間隔、横 50〜130m。
            if (i % 3 == 0 && (h % 100u) < 86u) {
                const float lat = 55.0f + float((h >> 6) % 100u) / 100.0f * 110.0f;
                Vec3 pos = pointAt(i, side * lat);
                pos.y = terrainHeightAt(pos.x, pos.z) - 0.15f;
                if (clearOfOtherRoad(pos, 6.0f, pos.y, i)) {
                    const float scale = 0.8f + float((h >> 16) % 100u) / 100.0f * 1.6f;
                    objects.add<SceneryInstance>("Tree#" + std::to_string(treeNo++),
                                                 SceneryKind::Tree, pos, scale, h ^ 0xABu, ci);
                }
            }
            // 丘の木: 平らな帯の外 (180〜680m) にもまばらに生やす。
            // 地形の上に立つので、高さは terrainHeightAt で決める。
            if (i % 6 == 0 && (h % 100u) < 80u) {
                const float lat = 130.0f + float((h >> 3) % 100u) / 100.0f * 390.0f;
                Vec3 pos = pointAt(i, side * lat);
                pos.y = terrainHeightAt(pos.x, pos.z);
                if (clearOfOtherRoad(pos, 6.0f, pos.y, i)) {
                    const float scale = 0.9f + float((h >> 16) % 100u) / 100.0f * 1.8f;
                    objects.add<SceneryInstance>("Tree#" + std::to_string(treeNo++),
                                                 SceneryKind::Tree, pos, scale, h ^ 0x51u, ci);
                }
            }
            // 建物: 約 110m 間隔の候補から 6 割、芝の外 20〜50m
            if (i % 55 == 0 && (h % 100u) < 60u) {
                const float lat = grassOuter + 27.0f + float((h >> 5) % 100u) / 100.0f * 28.0f;
                Vec3 pos = pointAt(i, side * lat);
                pos.y = baseY;
                if (std::fabs(terrainHeightAt(pos.x, pos.z) - baseY) < 5.0f &&
                    clearOfOtherRoad(pos, 17.0f, baseY, i)) {
                    objects.add<SceneryInstance>("Building#" + std::to_string(buildingNo++),
                                                 SceneryKind::Building, pos, 1.0f, h ^ 0x77u, ci);
                }
            }
            // 住宅街: 約 180m 間隔の候補に、5〜9軒の小さな集落を作る。
            if (i % 90 == 0 && (h % 100u) < 55u) {
                const float clat = grassOuter + 55.0f + float((h >> 11) % 100u) / 100.0f * 55.0f;
                Vec3 cpos = pointAt(i, side * clat);
                const float cbaseY = edgeY(i, side);
                if (std::fabs(terrainHeightAt(cpos.x, cpos.z) - cbaseY) >= 4.0f) {
                    continue;  // 集落は平らな場所だけ (斜面には建てない)
                }
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
        // 基礎の届き先は、その場所の地形の高さ (一枚板の時代の名残の
        // 全体一律ではなく、場所ごとに合わせる)。
        const float g = terrainHeightAt(inst->pos().x, inst->pos().z) - 0.4f;
        inst->bake(chunks[size_t(inst->chunk())].mesh, g);
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

// ============================================================================
// 地形 (山) ― コースに沿うリボン状の高度場メッシュ
// 考え方: 高さは「最寄りのコース地点からの横距離」で決める。近くは路面
// 中心の高さに張り付いて平ら (木・建物・住宅が立つ帯)、そこから先は
// 滑らかな関数で盛り上がって丘陵になり、視界の奥を山が塞ぐ。
// 頂点の高さは必ず terrainHeightAt (最寄り基準) で決めるので、コースが
// 自分に近づく場所 (鈴鹿のクロスオーバー等) でも山が路面を突き破らない。
// ============================================================================

// 丘の配置を決める。コースに沿って間引きした地点の左右に、木が生える
// ような辺り (横 150〜530m) へ、大きさと高さをばらつかせて置く。
// ハッシュで決定的に決まるので、呼ぶたびに同じ地形になる。
void SceneryBuilder::ensureBumps() const {
    if (bumpsReady_) {
        return;
    }
    bumpsReady_ = true;
    const int n = int(track_.samples.size());
    for (int i = 24; i < n; i += 23) {
        for (int s = -1; s <= 1; s += 2) {
            const unsigned h = treeHash(i * 13 + 7, float(s));
            if ((h % 100u) >= 78u) {
                continue;  // 2 割強は空けて、丘の無い開けた場所も残す
            }
            const float lat = 150.0f + float((h >> 3) % 100u) / 100.0f * 380.0f;
            Vec3 p = pointAt(i, float(s) * lat);
            Bump b;
            b.x = p.x;
            b.z = p.z;
            b.radius = 100.0f + float((h >> 9) % 100u) / 100.0f * 120.0f;
            b.height = 12.0f + float((h >> 15) % 100u) / 100.0f * 24.0f;
            bumps_.push_back(b);
        }
    }
}

// 地形の高さ。土台は最寄りコース地点の中心高 (平らな帯はコース脇だけ)、
// その上に「丘の集合」の盛り上がりと、遠景を閉じる緩い隆起を重ねる。
// 丘の影響は最寄りの路面からの距離で絞る (回廊フェード) ので、コースが
// 近くを通る場所では丘が路面を埋めない。
float SceneryBuilder::legProfile(const TrackPose& q) const {
    // 1 区間だけを見たときの地形高。中心高 + その区間の脇の山肌 + 遠景。
    const float lat = std::fabs(q.lateral);
    float y = q.centerY;
    const float flatStart = 22.0f;
    if (lat > flatStart) {
        const float d = lat - flatStart;
        const float t = q.s / track_.length;
        const float phase = q.lateral >= 0.0f ? 0.0f : 2.1f;  // 左右で位相をずらす
        float vf = 0.62f + 0.38f * std::sin(6.2831853f * 2.0f * t + phase) +
                   0.18f * std::sin(6.2831853f * 5.0f * t + phase * 2.0f);
        vf = vf < 0.25f ? 0.25f : (vf > 1.15f ? 1.15f : vf);
        // 立ち上がりは S 字に: 麓 (d 小) では緩くして芝の縁へ被せない。
        const float riseBase = 1.0f - std::exp(-d / 110.0f);
        y += 95.0f * std::pow(riseBase, 1.6f) * vf;
        if (lat > 500.0f) {
            const float u = std::min((lat - 500.0f) / 1100.0f, 1.0f);
            const float nn = std::sin(6.2831853f * 3.0f * t + 2.0f * std::sin(lat * 0.004f));
            y += std::pow(u, 1.6f) * 45.0f * (0.82f + 0.33f * nn);
        }
    }
    return y;
}

float SceneryBuilder::terrainHeightAt(float x, float z) const {
    // 区間選びは平面距離のみ (queryPlanar)。高さで選ぶ query だと
    // 立体交差で上下を取り違え、地形が路面を覆う事故が起きた。
    TrackPose q1 = track_.queryPlanar(x, z);
    if (!q1.found) {
        return 0.0f;
    }
    // 第 2 区間 (周回位置が十分に離れた最寄り) も引き、2 つの地形高を
    // なめらかにつなぐ。最寄りだけで決めると、近接した 2 区間の境界で
    // 高さが階段状に跳ね、メッシュが壁になって路面や車を覆ってしまう。
    int idx2 = -1;
    float d2min = 1.0e30f;
    const int n = int(track_.samples.size());
    for (int i = 0; i < n; ++i) {
        float ds = std::fabs(track_.samples[i].s - q1.s);
        if (ds > track_.length * 0.5f) {
            ds = track_.length - ds;
        }
        if (ds < 80.0f) {
            continue;  // 同じ区間の続きは第 2 候補にしない
        }
        const float dx = x - track_.samples[i].x;
        const float dz = z - track_.samples[i].z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < d2min) {
            d2min = d2;
            idx2 = i;
        }
    }
    float y;
    float fadeLat = std::fabs(q1.lateral);  // 丘フェード用の横距離 (近い方)
    const float p1 = legProfile(q1);
    if (idx2 < 0) {
        y = p1;
    } else {
        TrackPose q2 = track_.queryPlanarNear(x, z, idx2);
        fadeLat = std::min(fadeLat, std::fabs(q2.lateral));
        const float p2 = legProfile(q2);
        const float d1 = q1.xzDist;
        const float dd2 = q2.xzDist;
        if (d1 < 26.0f && dd2 < 26.0f) {
            // 立体交差・並走の密着部: 低い方の地形に付け、高い方は橋にする。
            // 高い方に付けると、その頂点を共有する三角形が低い方の路面へ
            // 覆い被さってしまう (高度場では断面の段差を表せないため)。
            y = q1.centerY <= q2.centerY ? p1 : p2;
        } else if (d1 < 22.0f || dd2 >= d1 + 25.0f) {
            y = p1;  // 自分の回廊の中、または第 2 区間が十分に遠い
        } else {
            // 近接する 2 区間の間では「低い方」に付ける。平均 (ブレンド) だと
            // 高い方の斜面が低い方の路面へ覆い被さってしまうため。
            // どちらの路面から見ても、その区間自身の地形以下にしかならない。
            y = std::min(p1, p2);
        }
    }
    // 丘の集合 (ワールド座標の起伏なので区間合成の後に足す)。
    // 路面から 40m までは効かせず 110m で全開にする。横距離は合成に
    // 使った 2 区間のうち近い方で見る (隣の路面の脇に丘を盛らない)。
    const float lat = fadeLat;
    float fade = (lat - 40.0f) / 70.0f;
    fade = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade);
    fade = fade * fade * (3.0f - 2.0f * fade);  // smoothstep
    if (fade > 0.0f) {
        ensureBumps();
        float bumps = 0.0f;
        for (const Bump& b : bumps_) {
            const float dx = x - b.x;
            const float dz = z - b.z;
            const float rr = b.radius * 2.4f;
            if (dx > rr || dx < -rr || dz > rr || dz < -rr) {
                continue;
            }
            const float d2 = dx * dx + dz * dz;
            bumps += b.height * std::exp(-2.0f * d2 / (b.radius * b.radius));
        }
        bumps = 40.0f * std::tanh(bumps / 40.0f);  // 重なりの頭打ち
        y += bumps * fade;
    }
    return y;
}

CpuMesh SceneryBuilder::buildBackdrop() const {
    // 地形は「コースに沿う帯」ではなく、ワールド全体の高度場グリッドで作る。
    // 帯方式だと、別の区間へ伸びた帯がコースの手前で交差し、頂点間の補間で
    // 巨大な三角形が路面を切り裂いてしまう (実際に埋まった)。高度場なら
    // どの (x, z) でも高さは地形関数の値そのものになり、路面を突かない。
    CpuMesh mesh;
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const TrackSample& s : track_.samples) {
        minX = std::min(minX, s.x); maxX = std::max(maxX, s.x);
        minZ = std::min(minZ, s.z); maxZ = std::max(maxZ, s.z);
    }
    // 格子 1 枚を焼く共通処理。法線は格子の差分から作る (滑らかな陰影)。
    auto buildGrid = [&](float x0, float z0, float x1, float z1, float cell, float sink) {
        const int nx = int((x1 - x0) / cell) + 1;
        const int nz = int((z1 - z0) / cell) + 1;
        std::vector<Vec3> pos(size_t(nx) * size_t(nz));
        for (int j = 0; j < nz; ++j) {
            for (int i = 0; i < nx; ++i) {
                Vec3 p{x0 + cell * float(i), 0.0f, z0 + cell * float(j)};
                p.y = terrainHeightAt(p.x, p.z) + sink;
                // 路面のすぐ脇では少しだけ窪ませ、芝や路盤の下に隠す
                // (バンクで縁が下がる側でも地形が顔を出さないため)。
                TrackPose q = track_.queryPlanar(p.x, p.z);
                if (q.found && std::fabs(q.lateral) < 45.0f) {
                    p.y -= 0.30f * (1.0f - std::fabs(q.lateral) / 45.0f);
                }
                pos[size_t(j) * size_t(nx) + size_t(i)] = p;
            }
        }
        const std::uint32_t base = uint32_t(mesh.vertices.size() / 9);
        for (int j = 0; j < nz; ++j) {
            for (int i = 0; i < nx; ++i) {
                Vec3 p = pos[size_t(j) * size_t(nx) + size_t(i)];
                const int ip = i > 0 ? i - 1 : i, in = i + 1 < nx ? i + 1 : i;
                const int jp = j > 0 ? j - 1 : j, jn = j + 1 < nz ? j + 1 : j;
                Vec3 along = pos[size_t(jn) * size_t(nx) + size_t(i)] - pos[size_t(jp) * size_t(nx) + size_t(i)];
                Vec3 across = pos[size_t(j) * size_t(nx) + size_t(in)] - pos[size_t(j) * size_t(nx) + size_t(ip)];
                Vec3 normal = cross(along, across);
                if (normal.y < 0.0f) {
                    normal = normal * -1.0f;
                }
                normal = normalize(normal);
                // 色: 平地は草、森の斑、斜面と高所は岩、頂上は雪。
                TrackPose q = track_.queryPlanar(p.x, p.z);
                float relief = p.y - q.centerY;
                float slope = 1.0f - normal.y;
                Vec3 col{0.16f, 0.34f, 0.13f};
                float forest = std::sin(p.x * 0.013f) * std::sin(p.z * 0.017f);
                if (forest > 0.3f) {
                    col = Vec3{0.11f, 0.27f, 0.10f};
                }
                // 中規模の斑: 斜面が一色の壁に見えないよう、明暗の群れを作る。
                float patch = std::sin(p.x * 0.055f + 1.7f) * std::sin(p.z * 0.047f + 0.4f);
                if (patch > 0.25f) {
                    col = col * 0.82f + Vec3{0.05f, 0.10f, 0.02f};
                } else if (patch < -0.45f) {
                    col = col * 1.13f;
                }
                float rockMix = std::min(1.0f, std::max(0.0f, (slope - 0.10f) * 3.0f));
                if (relief > 50.0f) {
                    rockMix = std::max(rockMix, std::min(1.0f, (relief - 50.0f) / 60.0f));
                }
                col = col * (1.0f - rockMix) + Vec3{0.47f, 0.44f, 0.40f} * rockMix;
                if (relief > 95.0f) {
                    float snow = std::min(1.0f, (relief - 95.0f) / 40.0f);
                    col = col * (1.0f - snow) + Vec3{0.87f, 0.89f, 0.93f} * snow;
                }
                unsigned jh = treeHash(i * 3 + j, 1.0f);
                float jitter = 0.96f + float((jh >> 12) % 100u) / 100.0f * 0.08f;
                mesh.addVertex(p, normal, col * jitter);
            }
        }
        for (int j = 0; j + 1 < nz; ++j) {
            for (int i = 0; i + 1 < nx; ++i) {
                std::uint32_t a = base + uint32_t(size_t(j) * size_t(nx) + size_t(i));
                std::uint32_t b = a + 1;
                std::uint32_t d = base + uint32_t(size_t(j + 1) * size_t(nx) + size_t(i));
                std::uint32_t cc = d + 1;
                mesh.addRawTriangle(a, d, cc);
                mesh.addRawTriangle(a, cc, b);
            }
        }
    };
    // 内側: 細かい格子 (12m、道路脇の斜面をなめらかに)。
    const float ix0 = minX - 260.0f, iz0 = minZ - 260.0f;
    const float ix1 = maxX + 260.0f, iz1 = maxZ + 260.0f;
    buildGrid(ix0, iz0, ix1, iz1, 12.0f, 0.0f);
    // 外側: 粗い格子 (80m) を内側の四辺の外にだけ敷く。全面に敷くと、
    // 丘の上の頂点同士を結ぶ巨大な三角形が内側の谷や路面を屋根のように
    // 覆ってしまう (実際に車が埋まった)。帯は必ず内側に触れない。
    const float ox0 = minX - 1500.0f, oz0 = minZ - 1500.0f;
    const float ox1 = maxX + 1500.0f, oz1 = maxZ + 1500.0f;
    buildGrid(ox0, oz0, ox1, iz0, 80.0f, -0.3f);   // 北
    buildGrid(ox0, iz1, ox1, oz1, 80.0f, -0.3f);   // 南
    buildGrid(ox0, iz0, ix0, iz1, 80.0f, -0.3f);   // 西
    buildGrid(ix1, iz0, ox1, iz1, 80.0f, -0.3f);   // 東
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
