#include "render.hpp"

#include "glapi.hpp"  // gl 関数の入手口 (環境ごとの差はここで吸収)

#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

const char* kVersion =
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    "#version 300 es\nprecision highp float;\n";
#else
    "#version 330 core\n";
#endif

const char* kWorldVs = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vColor;
out vec3 vWorld;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    gl_Position = uViewProj * world;
}
)GLSL";

// ワールドのフラグメントシェーダ。
//   昼: 太陽の平行光 + 遠くほど空色に霞む。
//   夜: 青みがかった弱い月明かり + 車のヘッドライト (スポットライト) で照らし、
//       霞は暗い紺。色が 1 を超える頂点 (テールランプ等) は自ら光る扱い。
// uNight が 0 のときは昼の計算結果と完全に同じになる。
const char* kWorldFs = R"GLSL(
#define MAX_LIGHTS 6
in vec3 vNormal;
in vec3 vColor;
in vec3 vWorld;
uniform vec3 uCamPos;
uniform float uAlpha;
uniform float uNight;
uniform float uExposure;
uniform vec3 uGlow;
uniform int uLightCount;
uniform vec3 uLightPos[MAX_LIGHTS];
uniform vec3 uLightDir[MAX_LIGHTS];
uniform vec3 uLightColor[MAX_LIGHTS];
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    // 昼の太陽と夜の月 (青白く弱い)。
    vec3 sun = normalize(vec3(0.35, 0.86, 0.28));
    float dayLight = 0.47 + 0.53 * max(dot(n, sun), 0.0);
    vec3 moon = normalize(vec3(-0.3, 0.8, -0.5));
    vec3 nightLight = vec3(0.10, 0.12, 0.17) + vec3(0.10, 0.13, 0.20) * max(dot(n, moon), 0.0);
    vec3 light = mix(vec3(dayLight), nightLight, uNight);
    // ヘッドライト: 円錐の中だけ、距離で弱まる。路面のような斜めの面も
    // 見えるよう、向きによる明暗は弱めにしている。
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        vec3 toLight = uLightPos[i] - vWorld;
        float d = length(toLight);
        vec3 l = toLight / max(d, 0.001);
        float cone = smoothstep(0.82, 0.96, dot(-l, uLightDir[i]));
        float fall = clamp(1.0 - d / 95.0, 0.0, 1.0);
        float facing = 0.35 + 0.65 * max(dot(n, l), 0.0);
        light += uLightColor[i] * (cone * fall * fall * facing * uNight);
    }
    vec3 color = vColor * light;
    // 自ら光る部品 (色が 1 を超える頂点) は夜は照明に関係なく明るい。
    float emissive = step(1.001, max(vColor.r, max(vColor.g, vColor.b))) * uNight;
    color = mix(color, min(vColor, vec3(1.8)), emissive);
    color = color * uExposure + uGlow;
    float dist = distance(vWorld, uCamPos);
    float dayFog = clamp((dist - 90.0) / 1500.0, 0.0, 1.0);
    float nightFog = clamp((dist - 40.0) / 700.0, 0.0, 1.0);
    float fog = mix(dayFog, nightFog, uNight) * (1.0 - emissive * 0.7);
    vec3 haze = mix(vec3(0.62, 0.74, 0.84), vec3(0.025, 0.035, 0.07), uNight);
    fragColor = vec4(mix(color, haze, fog), uAlpha);
}
)GLSL";

const char* kSkyVs = R"GLSL(
out float vY;
void main() {
    vec2 pos = vec2((gl_VertexID == 2) ? 3.0 : -1.0, (gl_VertexID == 1) ? 3.0 : -1.0);
    vY = pos.y;
    gl_Position = vec4(pos, 1.0, 1.0);
}
)GLSL";

const char* kSkyFs = R"GLSL(
in float vY;
uniform float uNight;
out vec4 fragColor;
void main() {
    float t = clamp(vY * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = mix(vec3(0.62, 0.74, 0.84), vec3(0.025, 0.035, 0.07), uNight);
    vec3 zenith = mix(vec3(0.28, 0.48, 0.78), vec3(0.005, 0.01, 0.03), uNight);
    fragColor = vec4(mix(horizon, zenith, smoothstep(0.0, 1.0, t)), 1.0);
}
)GLSL";

const char* kHudVs = R"GLSL(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uOrtho;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);
}
)GLSL";

const char* kHudFs = R"GLSL(
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)GLSL";

// 5x7。bit4 が左端。並びは 0-9 A-Z . : - / % と、記号 < > ( ) ! # _ | + ,
// (記号は選択画面の < >、"GO!"、"(C)"、"#1"、名前の _ などで使う)
constexpr char kGlyphOrder[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-/%<>()!#_|+,";
const std::uint8_t kFont[][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00},
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, {0x01, 0x02, 0x04, 0x04, 0x08, 0x10, 0x10},
    {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13},
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08},  // < >
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},  // ( )
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A},  // ! #
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}, {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  // _ |
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08},  // + ,
};
static_assert(sizeof(kFont) / sizeof(kFont[0]) == sizeof(kGlyphOrder) - 1, "glyph count");

GLuint compileShader(GLenum type, const char* source) {
    const char* parts[] = {kVersion, source};
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 2, parts, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SDL_Log("shader compile failed: %s", log);
        return 0;
    }
    return shader;
}

GLuint linkProgram(const char* vs, const char* fs) {
    GLuint vertex = compileShader(GL_VERTEX_SHADER, vs);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!vertex || !fragment) {
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        SDL_Log("shader link failed: %s", log);
        return 0;
    }
    return program;
}

int glyphIndex(char c) {
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    const char* found = std::strchr(kGlyphOrder, c);
    if (!found) {
        return -1;
    }
    return static_cast<int>(found - kGlyphOrder);
}

}  // namespace


// ---------------------------------------------------------------------------
// GlRenderer: IRenderer の OpenGL 実装 (外側の層)。
// シェーダ文字列・GL オブジェクト・描画状態はすべてこの実装の中に閉じる。
// ---------------------------------------------------------------------------
struct GlRenderer final : IRenderer {
        int w = 1;
    int h = 1;
    int fullW = 1;  // 子画面中も覚えておく全画面サイズ
    int fullH = 1;

    bool init() override;
    void resize(int w, int h) override;
    void setViewport(int x, int yFromBottom, int w, int h) override;
    void clearViewport() override;
    int width() const override { return w; }
    int height() const override { return h; }
    void clearFrame() override;
    void setLighting(const SceneLighting& lighting) override { light = lighting; }
    void drawSky() override;
    void beginWorld(const Mat4& viewProj, Vec3 cameraPos) override;
    // 照明の uniform をワールドのシェーダへ送る。
    void applyLighting();
    void draw(const GpuMesh& mesh, const Mat4& model) override;
    void drawAlpha(const GpuMesh& mesh, const Mat4& model, float alpha) override;
    void drawDynamic(const CpuMesh& mesh) override;
    void beginHud() override;
    void addQuad(float x0, float y0, float x1, float y1, float r, float g, float b, float a) override;
    void drawLine(float x0, float y0, float x1, float y1, float thickness, float r, float g, float b, float a) override;
    void drawText(float x, float y, float scale, const char* text, float r, float g, float b, float a) override;
    void flushHud() override;
    GpuMesh upload(const CpuMesh& mesh) override;

    unsigned worldProgram = 0;
    unsigned skyProgram = 0;
    unsigned hudProgram = 0;
    unsigned dummyVao = 0;
    unsigned hudVao = 0;
    unsigned hudVbo = 0;
    unsigned hudIbo = 0;
    unsigned dynVao = 0;
    unsigned dynVbo = 0;
    unsigned dynIbo = 0;
    int uViewProj = -1;
    int uAlphaLoc = -1;  // 半透明描画用の不透明度 uniform
    int uModel = -1;
    int uCamPos = -1;
    int uOrtho = -1;
    // 照明 (夜・ヘッドライト) の uniform。
    SceneLighting light;
    int uNight = -1, uExposure = -1, uGlow = -1, uLightCount = -1;
    int uLightPos = -1, uLightDir = -1, uLightColor = -1;
    int uSkyNight = -1;
    std::vector<float> hudVerts;
    std::vector<unsigned> hudIndices;
};

bool GlRenderer::init() {
    if (!loadGlFunctions()) {
        return false;  // Windows で GL 3.3 の関数が取れない (ドライバが古い等)
    }
    glGenVertexArrays(1, &dummyVao);
    worldProgram = linkProgram(kWorldVs, kWorldFs);
    skyProgram = linkProgram(kSkyVs, kSkyFs);
    hudProgram = linkProgram(kHudVs, kHudFs);
    if (!worldProgram || !skyProgram || !hudProgram) {
        return false;
    }
    uViewProj = glGetUniformLocation(worldProgram, "uViewProj");
    uAlphaLoc = glGetUniformLocation(worldProgram, "uAlpha");
    uModel = glGetUniformLocation(worldProgram, "uModel");
    uCamPos = glGetUniformLocation(worldProgram, "uCamPos");
    uOrtho = glGetUniformLocation(hudProgram, "uOrtho");
    uNight = glGetUniformLocation(worldProgram, "uNight");
    uExposure = glGetUniformLocation(worldProgram, "uExposure");
    uGlow = glGetUniformLocation(worldProgram, "uGlow");
    uLightCount = glGetUniformLocation(worldProgram, "uLightCount");
    uLightPos = glGetUniformLocation(worldProgram, "uLightPos");
    uLightDir = glGetUniformLocation(worldProgram, "uLightDir");
    uLightColor = glGetUniformLocation(worldProgram, "uLightColor");
    uSkyNight = glGetUniformLocation(skyProgram, "uNight");

    glGenVertexArrays(1, &hudVao);
    glGenBuffers(1, &hudVbo);
    glGenBuffers(1, &hudIbo);
    glBindVertexArray(hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hudIbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(dummyVao);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    // lookAt は左右を直すために鏡映になっている。巻き方向が反転するので、表の判定も合わせる。
    glFrontFace(GL_CW);
    resize(w, h);
    return true;
}

void GlRenderer::clearFrame() {
    // 消去色は空の地平線の色 (昼は空色、夜は暗い紺)。
    const float n = light.night;
    glClearColor(0.62f + (0.025f - 0.62f) * n, 0.74f + (0.035f - 0.74f) * n, 0.84f + (0.07f - 0.84f) * n, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GlRenderer::resize(int newWidth, int newHeight) {
    w = newWidth > 0 ? newWidth : 1;
    h = newHeight > 0 ? newHeight : 1;
    fullW = w;
    fullH = h;
    glViewport(0, 0, w, h);
}

void GlRenderer::setViewport(int x, int yFromBottom, int vw, int vh) {
    glEnable(GL_SCISSOR_TEST);  // clear もこの範囲だけに効かせる
    glScissor(x, yFromBottom, vw, vh);
    glViewport(x, yFromBottom, vw, vh);
    w = vw > 0 ? vw : 1;  // 投影のアスペクト計算用に子画面サイズを返す
    h = vh > 0 ? vh : 1;
}

void GlRenderer::clearViewport() {
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, fullW, fullH);
    w = fullW;
    h = fullH;
}

void GlRenderer::drawSky() {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(skyProgram);
    glUniform1f(uSkyNight, light.night);
    glBindVertexArray(dummyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void GlRenderer::beginWorld(const Mat4& viewProj, Vec3 cameraPos) {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(worldProgram);
    glUniformMatrix4fv(uViewProj, 1, GL_FALSE, viewProj.m);
    glUniform3f(uCamPos, cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(uAlphaLoc, 1.0f);  // 通常描画は不透明 (ゴースト描画が戻し忘れても安全なよう毎回)
    applyLighting();
}

void GlRenderer::applyLighting() {
    glUniform1f(uNight, light.night);
    glUniform1f(uExposure, light.exposure);
    glUniform3f(uGlow, 0.0f, 0.0f, 0.0f);  // 発光の上乗せは半透明描画のときだけ
    const int count = light.lightCount < 0 ? 0
                      : (light.lightCount > SceneLighting::kMaxLights ? SceneLighting::kMaxLights : light.lightCount);
    glUniform1i(uLightCount, count);
    float pos[SceneLighting::kMaxLights * 3] = {};
    float dir[SceneLighting::kMaxLights * 3] = {};
    float col[SceneLighting::kMaxLights * 3] = {};
    for (int i = 0; i < count; ++i) {
        const SpotLight& l = light.lights[i];
        pos[i * 3] = l.pos.x; pos[i * 3 + 1] = l.pos.y; pos[i * 3 + 2] = l.pos.z;
        dir[i * 3] = l.dir.x; dir[i * 3 + 1] = l.dir.y; dir[i * 3 + 2] = l.dir.z;
        col[i * 3] = l.color.x; col[i * 3 + 1] = l.color.y; col[i * 3 + 2] = l.color.z;
    }
    if (count > 0) {
        glUniform3fv(uLightPos, count, pos);
        glUniform3fv(uLightDir, count, dir);
        glUniform3fv(uLightColor, count, col);
    }
}

// 半透明描画 (ゴースト用)。ブレンドを有効化し、深度書き込みを止めて
// 描くことで、重なった部分も自然に透ける。終わったら状態を戻す。
void GlRenderer::drawAlpha(const GpuMesh& mesh, const Mat4& model, float alpha) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUniform1f(uAlphaLoc, alpha);
    glUniform3f(uGlow, light.ghostGlow.x, light.ghostGlow.y, light.ghostGlow.z);  // 夜はゴーストを発光させる
    draw(mesh, model);
    glUniform3f(uGlow, 0.0f, 0.0f, 0.0f);
    glUniform1f(uAlphaLoc, 1.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void GlRenderer::draw(const GpuMesh& mesh, const Mat4& model) {
    if (mesh.indexCount <= 0) {
        return;
    }
    glBindVertexArray(mesh.vao);
    glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
}

void GlRenderer::drawDynamic(const CpuMesh& mesh) {
    if (mesh.indices.empty()) {
        return;
    }
    if (dynVao == 0) {
        glGenVertexArrays(1, &dynVao);
        glGenBuffers(1, &dynVbo);
        glGenBuffers(1, &dynIbo);
        glBindVertexArray(dynVao);
        glBindBuffer(GL_ARRAY_BUFFER, dynVbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dynIbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    }
    glBindVertexArray(dynVao);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
                 mesh.vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dynIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                 mesh.indices.data(), GL_DYNAMIC_DRAW);
    Mat4 identity;
    glUniformMatrix4fv(uModel, 1, GL_FALSE, identity.m);
    glDrawElements(GL_TRIANGLES, static_cast<int>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
}

void GlRenderer::beginHud() {
    hudVerts.clear();
    hudIndices.clear();
}

void GlRenderer::addQuad(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    unsigned base = static_cast<unsigned>(hudVerts.size() / 6);
    float verts[] = {
        x0, y0, r, g, b, a, x1, y0, r, g, b, a, x1, y1, r, g, b, a, x0, y1, r, g, b, a,
    };
    hudVerts.insert(hudVerts.end(), verts, verts + 24);
    unsigned idx[] = {base, base + 1, base + 2, base, base + 2, base + 3};
    hudIndices.insert(hudIndices.end(), idx, idx + 6);
}

void GlRenderer::drawLine(float x0, float y0, float x1, float y1, float thickness, float r, float g, float b, float a) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) {
        return;
    }
    dx /= len;
    dy /= len;
    float px = -dy * thickness * 0.5f;
    float py = dx * thickness * 0.5f;
    unsigned base = static_cast<unsigned>(hudVerts.size() / 6);
    float verts[] = {
        x0 + px, y0 + py, r, g, b, a, x0 - px, y0 - py, r, g, b, a,
        x1 - px, y1 - py, r, g, b, a, x1 + px, y1 + py, r, g, b, a,
    };
    hudVerts.insert(hudVerts.end(), verts, verts + 24);
    unsigned idx[] = {base, base + 1, base + 2, base, base + 2, base + 3};
    hudIndices.insert(hudIndices.end(), idx, idx + 6);
}

void GlRenderer::drawText(float x, float y, float scale, const char* text, float r, float g, float b, float a) {
    float pen = x;
    for (const char* cursor = text; *cursor; ++cursor) {
        char c = *cursor;
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        if (c == ' ') {
            pen += 4.0f * scale;
            continue;
        }
        int index = glyphIndex(c);
        if (index < 0) {
            pen += 6.0f * scale;
            continue;
        }
        const std::uint8_t* rows = kFont[index];
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (0x10 >> col)) == 0) {
                    continue;
                }
                float x0 = pen + float(col) * scale;
                float y0 = y + float(row) * scale;
                addQuad(x0, y0, x0 + scale, y0 + scale, r, g, b, a);
            }
        }
        pen += 6.0f * scale;
    }
}

void GlRenderer::flushHud() {
    if (hudIndices.empty()) {
        return;
    }
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hudProgram);
    Mat4 ortho = orthoTopLeft(float(w), float(h));
    glUniformMatrix4fv(uOrtho, 1, GL_FALSE, ortho.m);
    glBindVertexArray(hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(hudVerts.size() * sizeof(float)), hudVerts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hudIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(hudIndices.size() * sizeof(unsigned)), hudIndices.data(), GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(hudIndices.size()), GL_UNSIGNED_INT, nullptr);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

GpuMesh GlRenderer::upload(const CpuMesh& mesh) {
    GpuMesh gpu;
    if (mesh.indices.empty()) {
        return gpu;
    }
    glGenVertexArrays(1, &gpu.vao);
    glGenBuffers(1, &gpu.vbo);
    glGenBuffers(1, &gpu.ibo);
    glBindVertexArray(gpu.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)), mesh.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    gpu.indexCount = static_cast<int>(mesh.indices.size());
    glBindVertexArray(dummyVao);
    return gpu;
}

IRenderer* createRenderer() { return new GlRenderer(); }
void destroyRenderer(IRenderer* renderer) { delete renderer; }
