// ============================================================================
// effects.cpp ― 演出層（タイヤ煙・テールランプの光跡）
// 役割: ドリフトに付随する一時的な視覚効果の生成・更新・描画。
//       物理には一切干渉しない。
// ============================================================================

#include "effects.hpp"

#include "game.hpp"
#include "render.hpp"

#include <algorithm>
#include <cmath>

void updateSmoke(Game& app, float dt) {
    Car& car = app.car;
    for (SmokePuff& p : app.smoke) {
        p.age += dt;
        p.vel.y += 0.7f * dt;
        float drag = std::max(0.0f, 1.0f - 0.9f * dt);
        p.vel = p.vel * drag;
        p.pos = p.pos + p.vel * dt;
    }
    app.smoke.erase(std::remove_if(app.smoke.begin(), app.smoke.end(),
                                   [](const SmokePuff& p) { return p.age >= p.life; }),
                    app.smoke.end());
    auto rnd = [&app]() {
        app.smokeSeed = app.smokeSeed * 1664525u + 1013904223u;
        return float((app.smokeSeed >> 8) & 0xFFFFu) / 65535.0f;
    };
    if (car.drifting && car.driftAmount > 0.45f && std::fabs(car.slipAngle) > 0.12f &&
        std::fabs(car.speed) > 6.0f) {
        float rate = 45.0f + std::min(45.0f, std::fabs(car.lateralSpeed) * 3.0f);
        app.smokeAcc += rate * dt;
        Vec3 fwd{std::sin(car.yaw), 0, std::cos(car.yaw)};
        Vec3 right{std::cos(car.yaw), 0, -std::sin(car.yaw)};
        while (app.smokeAcc >= 1.0f) {
            app.smokeAcc -= 1.0f;
            float side = (app.smoke.size() % 2 == 0) ? 1.0f : -1.0f;
            SmokePuff p;
            p.pos = Vec3{car.x, car.y + 0.25f, car.z} + right * (side * 0.82f) + fwd * -1.38f;
            p.pos.x += (rnd() - 0.5f) * 0.3f;
            p.pos.z += (rnd() - 0.5f) * 0.3f;
            p.vel = Vec3{-car.velX * 0.12f + (rnd() - 0.5f) * 0.8f,
                         1.0f + rnd() * 0.8f,
                         -car.velZ * 0.12f + (rnd() - 0.5f) * 0.8f};
            p.life = 0.8f + rnd() * 0.5f;
            app.smoke.push_back(p);
            if (app.smoke.size() > 260) {
                app.smoke.erase(app.smoke.begin());
            }
        }
    } else {
        app.smokeAcc = 0;
    }
}

// 煙をカメラ向きの円盤として描く（八角形のビルボードを毎フレーム構築）。
void drawSmoke(Game& app) {
    if (app.smoke.empty()) {
        return;
    }
    Vec3 fwd = normalize(app.camera.look - app.camera.eye);
    Vec3 right = normalize(cross(fwd, Vec3{0, 1, 0}));
    Vec3 up = cross(right, fwd);
    // 法線は真上固定にする。カメラ向きだと太陽との内積で真っ黒になり、
    // 煙ではなく黒い塊に見えてしまうため（上面光で明るく保つ）。
    Vec3 n{0, 1, 0};
    CpuMesh mesh;
    for (const SmokePuff& p : app.smoke) {
        float t = p.age / p.life;
        float radius = 0.35f + 1.35f * t;
        Vec3 color = Vec3{0.52f, 0.52f, 0.55f} * (1.0f - t) + Vec3{0.88f, 0.90f, 0.92f} * t;
        std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size() / 9);
        mesh.addVertex(p.pos, n, color);
        for (int k = 0; k < 8; ++k) {
            float a = float(k) * 0.7853981f;
            mesh.addVertex(p.pos + right * (std::cos(a) * radius) + up * (std::sin(a) * radius),
                           n, color);
        }
        for (int k = 0; k < 8; ++k) {
            mesh.addRawTriangle(base, base + 1 + static_cast<std::uint32_t>(k),
                                base + 1 + static_cast<std::uint32_t>((k + 1) % 8));
        }
    }
    app.renderer->drawDynamic(mesh);
}


Vec3 lampWorldPos(const Game& app, float side) {
    Vec3 fwd{std::sin(app.car.yaw), 0, std::cos(app.car.yaw)};
    Vec3 right{fwd.z, 0, -fwd.x};
    Vec3 p{app.car.x, app.car.y + 0.47f, app.car.z};
    p = p + right * (side * 0.55f);
    p = p + fwd * -2.1f;
    return p;
}

// ドリフト中、テールランプの位置履歴を残す（長時間露光の光跡になる）。
void updateTrails(Game& app, float dt) {
    for (auto* trail : {&app.trailL, &app.trailR}) {
        for (auto& pt : *trail) pt.age += dt;
        trail->erase(std::remove_if(trail->begin(), trail->end(),
                                    [](const TrailPoint& pt) { return pt.age > 1.15f; }),
                     trail->end());
    }
    if (app.car.drifting && app.car.driftAmount > 0.3f) {
        const float sides[2] = {-1.0f, 1.0f};
        std::vector<TrailPoint>* trails[2] = {&app.trailL, &app.trailR};
        for (int i = 0; i < 2; ++i) {
            Vec3 pos = lampWorldPos(app, sides[i]);
            if (trails[i]->empty() || length(pos - trails[i]->back().pos) > 0.5f) {
                trails[i]->push_back({pos, 0.0f});
            }
        }
    }
}

void drawTrails(Game& app) {
    CpuMesh mesh;
    Vec3 camDir = app.camera.look - app.camera.eye;
    if (length(camDir) < 0.001f) return;
    camDir = normalize(camDir);
    std::vector<TrailPoint>* trails[2] = {&app.trailL, &app.trailR};
    for (auto* trail : trails) {
        for (size_t i = 1; i < trail->size(); ++i) {
            const TrailPoint& a = (*trail)[i - 1];
            const TrailPoint& b = (*trail)[i];
            Vec3 seg = b.pos - a.pos;
            if (length(seg) < 0.001f) continue;
            // 幅方向は「進行方向 × 上」で取る。カメラ方向と交差させると
            // 後方カメラで進行と視線が平行になり、帯が暴れてしまう。
            Vec3 w = normalize(cross(seg, Vec3{0, 1, 0}));
            const float hw = 0.085f;
            auto col = [](float age) {
                float f = clampf(1.0f - age / 1.15f, 0.0f, 1.0f);
                return Vec3{1.9f * f, 0.16f * f, 0.16f * f};
            };
            Vec3 a0 = a.pos + w * -hw, a1 = a.pos + w * hw;
            Vec3 b0 = b.pos + w * -hw, b1 = b.pos + w * hw;
            // 2 三角形を色フェード付きで直接積む（addQuad は単色なので手書き）
            auto tri = [&](Vec3 p, Vec3 q, Vec3 r, Vec3 c1, Vec3 c2, Vec3 c3) {
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
    if (!mesh.vertices.empty()) app.renderer->drawDynamic(mesh);
}

