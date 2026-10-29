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
