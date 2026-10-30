// ============================================================================
// effects.cpp ― 演出クラス (Effects) の実装
// 役割: タイヤ煙・テールランプの光跡・タイヤ痕の生成／更新／描画。
//       物理には一切干渉しない (Car の状態を読むだけ)。
// ============================================================================

#include "effects.hpp"

#include "camera.hpp"  // Orbit
#include "car.hpp"     // Car
#include "render.hpp"  // IRenderer

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
// タイヤ痕の寿命 (秒)。過ぎた痕から路面色へ溶けて消える。
constexpr float kMarkLife = 26.0f;
// タイヤ痕の最大区間数。超えたら古いものから捨てる (メモリと描画の上限)。
constexpr size_t kMarkMax = 2000;
}  // namespace

void Effects::clear() {
    sparks_.clear();
    smoke_.clear();
    trailL_.clear();
    trailR_.clear();
    marks_.clear();
    marksDirty_ = true;
    markChain_ = false;
    smokeAcc_ = 0;
}

void Effects::update(const Car& car, float dt) {
    if (!enabled_) {
        return;  // ベンチで無効化されている間は生成も経過も止める
    }
    // --- タイヤ煙: 既存の粒を漂わせ、寿命が来たものを消す -----------------
    for (SmokePuff& p : smoke_) {
        p.age += dt;
        p.vel.y += 0.7f * dt;  // 煙は軽いので少しずつ上昇する
        float drag = std::max(0.0f, 1.0f - 0.9f * dt);
        p.vel = p.vel * drag;
        p.pos = p.pos + p.vel * dt;
    }
    smoke_.erase(std::remove_if(smoke_.begin(), smoke_.end(),
                                [](const SmokePuff& p) { return p.age >= p.life; }),
                 smoke_.end());
    // --- タイヤ煙: ドリフト中だけ後輪から新しい粒を出す -------------------
    auto rnd = [this]() {
        smokeSeed_ = smokeSeed_ * 1664525u + 1013904223u;
        return float((smokeSeed_ >> 8) & 0xFFFFu) / 65535.0f;
    };
    if (car.drifting && car.driftAmount > 0.45f && std::fabs(car.slipAngle) > 0.12f &&
        std::fabs(car.speed) > 6.0f) {
        float rate = 45.0f + std::min(45.0f, std::fabs(car.lateralSpeed) * 3.0f);
        smokeAcc_ += rate * dt;
        Vec3 fwd{std::sin(car.yaw), 0, std::cos(car.yaw)};
        Vec3 right{std::cos(car.yaw), 0, -std::sin(car.yaw)};
        while (smokeAcc_ >= 1.0f) {
            smokeAcc_ -= 1.0f;
            float side = (smoke_.size() % 2 == 0) ? 1.0f : -1.0f;
            SmokePuff p;
            p.pos = Vec3{car.x, car.y + 0.25f, car.z} + right * (side * 0.82f) + fwd * -1.38f;
            p.pos.x += (rnd() - 0.5f) * 0.3f;
            p.pos.z += (rnd() - 0.5f) * 0.3f;
            p.vel = Vec3{-car.velX * 0.12f + (rnd() - 0.5f) * 0.8f,
                         1.0f + rnd() * 0.8f,
                         -car.velZ * 0.12f + (rnd() - 0.5f) * 0.8f};
            p.life = 0.8f + rnd() * 0.5f;
            smoke_.push_back(p);
            if (smoke_.size() > 210) {
                smoke_.erase(smoke_.begin());
            }
        }
    } else {
        smokeAcc_ = 0;
    }

    // --- 光跡: ランプ位置の履歴を残し、古い点を消す ------------------------
    for (auto* trail : {&trailL_, &trailR_}) {
        for (auto& pt : *trail) pt.age += dt;
        trail->erase(std::remove_if(trail->begin(), trail->end(),
                                    [](const TrailPoint& pt) { return pt.age > 1.15f; }),
                     trail->end());
    }
    if (car.drifting && car.driftAmount > 0.3f) {
        const float sides[2] = {-1.0f, 1.0f};
        std::vector<TrailPoint>* trails[2] = {&trailL_, &trailR_};
        for (int i = 0; i < 2; ++i) {
            Vec3 pos = lampWorldPos(car, sides[i]);
            if (trails[i]->empty() || length(pos - trails[i]->back().pos) > 0.5f) {
                trails[i]->push_back({pos, 0.0f});
            }
        }
    }

    // --- タイヤ痕: ドリフトで後輪が滑った帯を路面に残す -------------------
    for (auto& m : marks_) m.age += dt;
    {
        size_t before = marks_.size();
        marks_.erase(std::remove_if(marks_.begin(), marks_.end(),
                                    [](const SkidMark& m) { return m.age > kMarkLife; }),
                     marks_.end());
        if (marks_.size() != before) marksDirty_ = true;  // 消えたら作り直し
    }
    // 出す条件: ドリフト中・接地している・舗装の上・それなりの速度。
    const bool laying = car.drifting && car.driftAmount > 0.3f &&
                        car.onAsphalt && std::fabs(car.speed) > 5.0f;
    if (laying) {
        Vec3 fwd{std::sin(car.yaw), 0, std::cos(car.yaw)};
        Vec3 right{std::cos(car.yaw), 0, -std::sin(car.yaw)};
        Vec3* lasts[2] = {&lastMarkL_, &lastMarkR_};
        const float sides[2] = {-1.0f, 1.0f};
        for (int i = 0; i < 2; ++i) {
            // 後輪の接地位置 (車体中心から左右 0.82m、後方 1.38m、路面すれすれ)。
            Vec3 pos = Vec3{car.x, car.y + 0.04f, car.z} + right * (sides[i] * 0.82f) + fwd * -1.38f;
            if (markChain_) {
                float d2 = (pos.x - lasts[i]->x) * (pos.x - lasts[i]->x) +
                           (pos.z - lasts[i]->z) * (pos.z - lasts[i]->z);
                if (d2 > 0.36f) {  // 0.6m 以上進んだら 1 区間として刻む
                    if (d2 < 25.0f) {  // 5m 以上の跳びはリセット等なのでつながない
                        marks_.push_back({*lasts[i], pos, 0.0f, clampf(car.driftAmount, 0.0f, 1.0f)});
                        marksDirty_ = true;  // 増えたら作り直し
                    }
                    *lasts[i] = pos;
                }
            } else {
                *lasts[i] = pos;  // 出し始めは起点だけ置く
            }
        }
        if (marks_.size() > kMarkMax) {
            marks_.erase(marks_.begin(), marks_.begin() + (marks_.size() - kMarkMax));
            marksDirty_ = true;
        }
    }
    markChain_ = laying;
    // --- 火花: 既存分を進め、壁との接触から新規を出す ---------------------
    for (size_t i = 0; i < sparks_.size();) {
        Spark& s = sparks_[i];
        s.age += dt;
        if (s.age >= s.life) {
            sparks_[i] = sparks_.back();
            sparks_.pop_back();
            continue;
        }
        s.vel.y -= 6.5f * dt;  // 軽めの重力で放物線を描く
        s.pos = s.pos + s.vel * dt;
        ++i;
    }
    auto spawnSpark = [&](Vec3 origin, Vec3 baseVel, float spread) {
        if (sparks_.size() >= 420) return;
        sparkSeed_ = sparkSeed_ * 1664525u + 1013904223u;
        float r1 = float((sparkSeed_ >> 16) & 0x3FF) / 1024.0f - 0.5f;
        sparkSeed_ = sparkSeed_ * 1664525u + 1013904223u;
        float r2 = float((sparkSeed_ >> 16) & 0x3FF) / 1024.0f - 0.5f;
        Spark s;
        s.pos = origin;
        s.vel = baseVel + Vec3{r1 * spread, r2 * spread + 1.2f, r1 * spread * 0.7f};
        s.age = 0;
        s.life = 0.3f + 0.4f * (0.5f + r2);
        sparks_.push_back(s);
    };
    if (car.wallContact > 0 && std::fabs(car.speed) > 3.0f) {
        Vec3 fw{std::sin(car.yaw), 0, std::cos(car.yaw)};
        Vec3 rt{fw.z, 0, -fw.x};
        Vec3 origin{car.x + rt.x * car.wallSide * 0.95f, car.y + 0.32f,
                    car.z + rt.z * car.wallSide * 0.95f};
        // 擦り: 連続して細かく散らす。後方と外側へ跳ねる。
        sparkAcc_ += dt * 110.0f;
        while (sparkAcc_ >= 1.0f) {
            sparkAcc_ -= 1.0f;
            spawnSpark(origin + fw * ((sparkSeed_ >> 8 & 0xFF) / 255.0f - 0.5f) * 2.0f,
                       fw * -std::fabs(car.speed) * 0.22f + rt * -car.wallSide * 1.6f, 2.6f);
        }
        // 強い衝突: 一度に弾ける。
        if (car.wallImpact > 0.05f) {
            int burst = int(10.0f + car.wallImpact * 26.0f);
            for (int i = 0; i < burst; ++i) {
                spawnSpark(origin, fw * -std::fabs(car.speed) * 0.3f + rt * -car.wallSide * 3.0f,
                           5.0f);
            }
        }
    }
}

// テールランプのワールド位置 (光跡の記録用)。車体後端の少し内側・高さ 0.47m。
Vec3 Effects::lampWorldPos(const Car& car, float side) const {
    Vec3 fwd{std::sin(car.yaw), 0, std::cos(car.yaw)};
    Vec3 right{fwd.z, 0, -fwd.x};
    Vec3 p{car.x, car.y + 0.47f, car.z};
    p = p + right * (side * 0.55f);
    p = p + fwd * -2.1f;
    return p;
}

// 煙をカメラ向きの円盤として描く（八角形のビルボードを毎フレーム構築）。

void Effects::drawSmoke(IRenderer& renderer, const Orbit& camera) const {
    if (!enabled_) {
        return;
    }
    if (smoke_.empty()) {
        return;
    }
    Vec3 fwd = normalize(camera.look - camera.eye);
    Vec3 right = normalize(cross(fwd, Vec3{0, 1, 0}));
    Vec3 up = cross(right, fwd);
    // 法線は真上固定にする。カメラ向きだと太陽との内積で真っ黒になり、
    // 煙ではなく黒い塊に見えてしまうため（上面光で明るく保つ）。
    Vec3 n{0, 1, 0};
    CpuMesh mesh;
    for (const SmokePuff& p : smoke_) {
        float t = p.age / p.life;
        float radius = 0.30f + 1.00f * t;
        Vec3 color = Vec3{0.52f, 0.52f, 0.55f} * (1.0f - t) + Vec3{0.88f, 0.90f, 0.92f} * t;
        std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size() / 9);
        mesh.addVertex(p.pos, n, color);
        for (int k = 0; k < 6; ++k) {  // 六角形 (八角形から頂点を減らして軽く)
            float a = float(k) * 1.0471976f;
            mesh.addVertex(p.pos + right * (std::cos(a) * radius) + up * (std::sin(a) * radius),
                           n, color);
        }
        for (int k = 0; k < 6; ++k) {
            mesh.addRawTriangle(base, base + 1 + static_cast<std::uint32_t>(k),
                                base + 1 + static_cast<std::uint32_t>((k + 1) % 6));
        }
    }
    renderer.drawDynamic(mesh);
}

// 光跡を帯として描く共通処理。点列を幅 0.17m のリボンにし、古い点ほど
// 暗くする。age が負の点は区切り (その前後の区間は描かない)。
// baseCol は生まれたての色 (プレイヤーは HDR 赤、ゴーストは少し落とす)。
static void appendTrailRibbon(CpuMesh& mesh, const std::vector<TrailPoint>& pts, Vec3 baseCol) {
    for (size_t i = 1; i < pts.size(); ++i) {
        const TrailPoint& a = pts[i - 1];
        const TrailPoint& b = pts[i];
        if (a.age < 0.0f || b.age < 0.0f) continue;  // 区切りをまたがない
        Vec3 seg = b.pos - a.pos;
        if (length(seg) < 0.001f) continue;
        // 幅方向は「進行方向 × 上」で取る。カメラ方向と交差させると
        // 後方カメラで進行と視線が平行になり、帯が暴れてしまう。
        Vec3 w = normalize(cross(seg, Vec3{0, 1, 0}));
        const float hw = 0.085f;
        auto col = [baseCol](float age) {
            float f = clampf(1.0f - age / 1.15f, 0.0f, 1.0f);
            return baseCol * f;
        };
        Vec3 a0 = a.pos + w * -hw, a1 = a.pos + w * hw;
        Vec3 b0 = b.pos + w * -hw, b1 = b.pos + w * hw;
        // 2 三角形を色フェード付きで直接積む（addQuad は単色なので手書き）
        auto tri = [&mesh](Vec3 p, Vec3 q, Vec3 r, Vec3 c1, Vec3 c2, Vec3 c3) {
            std::uint32_t base = std::uint32_t(mesh.vertices.size() / 9);
            mesh.addVertex(p, {0, 1, 0}, c1);
            mesh.addVertex(q, {0, 1, 0}, c2);
            mesh.addVertex(r, {0, 1, 0}, c3);
            mesh.addRawTriangle(base, base + 1, base + 2);
        };
        tri(a0, b0, b1, col(a.age), col(b.age), col(b.age));
        tri(a0, b1, a1, col(a.age), col(b.age), col(a.age));
    }
}

// プレイヤーの光跡。
void Effects::drawTrails(IRenderer& renderer) const {
    if (!enabled_) {
        return;
    }
    CpuMesh mesh;
    appendTrailRibbon(mesh, trailL_, Vec3{1.9f, 0.16f, 0.16f});
    appendTrailRibbon(mesh, trailR_, Vec3{1.9f, 0.16f, 0.16f});
    if (!mesh.vertices.empty()) renderer.drawDynamic(mesh);
}

// ゴーストの残像。本体が半透明なので、帯の色も少し落として判別しやすくする。
void Effects::drawGhostTrails(IRenderer& renderer, const std::vector<TrailPoint>& left,
                              const std::vector<TrailPoint>& right) const {
    if (!enabled_) {
        return;
    }
    CpuMesh mesh;
    appendTrailRibbon(mesh, left, Vec3{1.15f, 0.30f, 0.30f});
    appendTrailRibbon(mesh, right, Vec3{1.15f, 0.30f, 0.30f});
    if (!mesh.vertices.empty()) renderer.drawDynamic(mesh);
}

// タイヤ痕を路面の帯として描く。新しい痕はほぼ黒、古くなるほど
// 路面色 (アスファルトのグレー) へ寄せて「溶けて消える」ように見せる。
// メッシュはキャッシュし、痕が増減した時と約 0.15 秒ごとのフェード
// 更新のときだけ作り直す (毎フレーム全区間の再構築が重かったため)。
void Effects::drawMarks(IRenderer& renderer) const {
    if (!enabled_) {
        return;
    }
    if (marks_.empty()) {
        return;
    }
    if (marksDirty_ || ++marksFrames_ >= 9) {
        marksDirty_ = false;
        marksFrames_ = 0;
        const Vec3 ink{0.05f, 0.05f, 0.055f};    // 焼けたゴムの色
        const Vec3 road{0.23f, 0.23f, 0.24f};    // 路面色 (フェードの行き先)
        CpuMesh mesh;
        for (const SkidMark& m : marks_) {
            Vec3 seg = m.to - m.from;
            seg.y = 0;
            if (length(seg) < 0.001f) continue;
            // 帯の幅方向 (進行と直交する水平方向)。タイヤ幅 ≒ 0.3m。
            Vec3 w = normalize(cross(seg, Vec3{0, 1, 0})) * 0.15f;
            float fade = clampf(1.0f - m.age / kMarkLife, 0.0f, 1.0f);
            float k = (0.25f + 0.75f * fade) * m.strength;  // 残り具合
            Vec3 col = road * (1.0f - k) + ink * k;
            Vec3 a0 = m.from - w, a1 = m.from + w;
            Vec3 b0 = m.to - w, b1 = m.to + w;
            std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size() / 9);
            mesh.addVertex(a0, {0, 1, 0}, col);
            mesh.addVertex(b0, {0, 1, 0}, col);
            mesh.addVertex(b1, {0, 1, 0}, col);
            mesh.addVertex(a1, {0, 1, 0}, col);
            mesh.addRawTriangle(base, base + 1, base + 2);
            mesh.addRawTriangle(base, base + 2, base + 3);
        }
        marksMesh_ = std::move(mesh);
    }
    if (!marksMesh_.indices.empty()) renderer.drawDynamic(marksMesh_);
}

// 火花を描く。カメラ向きの小さな四角を、熱い橙 (HDR) から暗い赤へ
// 冷ましながら出す。加算っぽく見えるよう色は明るめに振る。
void Effects::drawSparks(IRenderer& renderer, const Orbit& camera) const {
    if (!enabled_) {
        return;
    }
    if (sparks_.empty()) {
        return;
    }
    Vec3 fwd = normalize(camera.look - camera.eye);
    Vec3 right = normalize(cross(fwd, Vec3{0, 1, 0}));
    Vec3 up = cross(right, fwd);
    CpuMesh mesh;
    for (const Spark& s : sparks_) {
        float f = s.age / s.life;         // 0=生まれたて → 1=消える直前
        float size = 0.055f * (1.0f - f * 0.5f);
        Vec3 hot{3.2f, 2.3f, 0.9f};       // 生まれたての色 (白熱した橙)
        Vec3 cold{1.3f, 0.30f, 0.04f};    // 冷めた色 (暗い赤)
        Vec3 col = hot * (1.0f - f) + cold * f;
        Vec3 r = right * size;
        Vec3 u = up * size;
        Vec3 p = s.pos;
        mesh.addQuad(p - r - u, p + r - u, p + r + u, p - r + u, fwd, col);
    }
    renderer.drawDynamic(mesh);
}
