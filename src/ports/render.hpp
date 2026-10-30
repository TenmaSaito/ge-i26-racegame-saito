#pragma once

// ============================================================================
// render.hpp - 描画の「ポート」(アプリケーション層が外側へ求める能力の宣言)
// ----------------------------------------------------------------------------
// レイヤードアーキテクチャでは、内側のコードは外側の実装を直接知らない。
// このヘッダは「描画に必要な操作」だけを抽象インターフェースとして宣言し、
// OpenGL による実装は render.cpp の GlRenderer (外側の層) が担う。
// 生成は createRenderer() で外側 (platform) が行い、Game へポインタで差し込む。
//
// 依存: geom.hpp / meshbuild.hpp のみ。SDL や GL のヘッダはここに現れない。
// ============================================================================

#include "geom.hpp"
#include "meshbuild.hpp"

// GPU 側メッシュの不透明ハンドル。中身 (VAO 等) の意味は実装側だけが知る。
struct GpuMesh {
    unsigned vao = 0;
    unsigned vbo = 0;
    unsigned ibo = 0;
    int indexCount = 0;
};

// ヘッドライト 1 灯分 (スポットライト)。
struct SpotLight {
    Vec3 pos;    // 光源の位置 (ワールド)
    Vec3 dir;    // 照らす向き (正規化済み)
    Vec3 color;  // 色と強さ (1 前後)
};

// ワールドの照明の設定。昼はすべて既定値のまま。
// 夜は太陽の代わりに月明かりと車のヘッドライトで照らし、色が 1 を超える
// 頂点 (テールランプ・ヘッドランプ) は自ら光るものとして明るく描く。
struct SceneLighting {
    static constexpr int kMaxLights = 6;
    float night = 0.0f;     // 0 = 昼、1 = 夜
    float exposure = 1.0f;  // 全体の明るさの倍率 (夜のバックミラーで上げる)
    Vec3 ghostGlow{0, 0, 0};  // 半透明描画 (ゴースト) に足す発光色 (夜に見やすくする)
    int lightCount = 0;
    SpotLight lights[kMaxLights];
};

// 描画ポート。ゲーム側 (game / hud / effects) はこの型だけを見て描く。
struct IRenderer {
    virtual ~IRenderer() = default;

    virtual bool init() = 0;
    virtual void resize(int width, int height) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;

    virtual void clearFrame() = 0;
    // 画面内の一部分だけに描く (バックミラー等の子画面用)。この間は
    // width()/height() が子画面の大きさを返し、clear も範囲内だけに効く。
    // yFromBottom は GL の慣例で下端からの位置。終わったら clearViewport。
    virtual void setViewport(int x, int yFromBottom, int w, int h) = 0;
    virtual void clearViewport() = 0;
    // 照明を設定する (次の drawSky / beginWorld から効く)。
    virtual void setLighting(const SceneLighting& lighting) = 0;
    virtual void drawSky() = 0;
    virtual void beginWorld(const Mat4& viewProj, Vec3 cameraPos) = 0;
    virtual void draw(const GpuMesh& mesh, const Mat4& model) = 0;
    // 半透明で描く (ゴースト用)。alpha は 0..1、奥の物が透けて見える。
    virtual void drawAlpha(const GpuMesh& mesh, const Mat4& model, float alpha) = 0;
    // 毎フレーム作り直す小さなメッシュ（タイヤ煙・光跡）用。バッファを使い回す。
    virtual void drawDynamic(const CpuMesh& mesh) = 0;

    virtual void beginHud() = 0;
    virtual void addQuad(float x0, float y0, float x1, float y1,
                         float r, float g, float b, float a) = 0;
    virtual void drawLine(float x0, float y0, float x1, float y1, float thickness,
                          float r, float g, float b, float a) = 0;
    virtual void drawText(float x, float y, float scale, const char* text,
                          float r, float g, float b, float a) = 0;
    virtual void flushHud() = 0;

    virtual GpuMesh upload(const CpuMesh& mesh) = 0;
};

// 実装の生成と破棄 (外側の層だけが呼ぶ)。
IRenderer* createRenderer();
void destroyRenderer(IRenderer* renderer);
