// ============================================================================
// recordservice.cpp ― 走行記録のオンライン共有 (IRecordService) の実装
// 役割:
//   - ブラウザ版: ページの fetch() でサーバー (server/worker.js) と JSON で
//     やり取りする。送り先はページ側の設定 window.RACEGAME_API (URL)。
//     未設定なら「手元だけ (LOCAL ONLY)」で動く。
//   - ネイティブ版 (Windows / Linux / Android): HTTP ライブラリを持たないため
//     今は手元だけ。対応するときはこのファイルに libcurl 等で同じ 4 つの
//     関数を実装すればよい (ゲーム側は変更不要)。
// 安全のための方針: 送るのは名前・コース・タイム・区間速度だけ。鍵や
//   トークンはクライアントに置かない (書き込みの検証はサーバー側が行う)。
// ============================================================================

#include "recordservice.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// 注意: EM_JS の中身は C のプリプロセッサを通るため、JS の正規表現リテラル
// (// を含むもの) やシングルクォートの空文字列は使わず、文字列は "..." で書く。

// 送り先 URL (末尾の / を除いたもの)。設定が無い・http(s) でなければ空文字列。
EM_JS(char*, netApiBaseJs, (), {
    var api = (typeof window !== "undefined" && typeof window.RACEGAME_API === "string") ? window.RACEGAME_API : "";
    if (api.indexOf("https://") !== 0 && api.indexOf("http://") !== 0) api = "";
    while (api.length > 0 && api.charAt(api.length - 1) === "/") api = api.slice(0, -1);
    var n = lengthBytesUTF8(api) + 1;
    var p = _malloc(n);
    stringToUTF8(api, p, n);
    return p;
});

// 記録 1 行 (R1|名前|コース|車種|強さ|合計|ベスト|速度,...) を JSON にして POST する。
EM_JS(void, netSubmitJs, (const char* base, const char* line), {
    var f = UTF8ToString(line).split("|");
    if (f.length !== 8) return;
    var body = {
        name: f[1], course: f[2], car: Number(f[3]), level: Number(f[4]),
        total: Number(f[5]), bestLap: Number(f[6]), profile: f[7].split(",").map(Number)
    };
    Module["raceNetState"] = "SENDING";
    fetch(UTF8ToString(base) + "/records", {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body)
    }).then(function (r) {
        Module["raceNetState"] = r.ok ? "SHARED" : (r.status === 429 ? "WAIT" : "REJECTED");
    }).catch(function () { Module["raceNetState"] = "OFFLINE"; });
});

// コースの共有記録を GET し、届いたら 1 行形式に直して受け箱へ置く。
// 区切り文字 (| , 改行) が紛れ込まないよう、名前とコースは英数字だけにする。
EM_JS(void, netRequestJs, (const char* base, const char* course), {
    Module["raceNetState"] = "SYNCING";
    fetch(UTF8ToString(base) + "/records?course=" + encodeURIComponent(UTF8ToString(course)))
        .then(function (r) { if (!r.ok) throw new Error(String(r.status)); return r.json(); })
        .then(function (list) {
            var clean = function (s) { return String(s).toUpperCase().replace(/[^A-Z0-9_-]/g, "").slice(0, 12); };
            var lines = (Array.isArray(list) ? list : []).filter(function (r) {
                return r && Array.isArray(r.profile);
            }).map(function (r) {
                return ["R1", clean(r.name), clean(r.course).toLowerCase(), r.car | 0, r.level | 0,
                        Number(r.total).toFixed(3), Number(r.bestLap).toFixed(3),
                        r.profile.map(function (v) { return Number(v).toFixed(1); }).join(",")].join("|");
            });
            var inbox = Module["raceNetInbox"] || "";
            Module["raceNetInbox"] = (inbox ? inbox + "\n" : "") + lines.join("\n");
            Module["raceNetState"] = "ONLINE";
        })
        .catch(function () { Module["raceNetState"] = "OFFLINE"; });
});

// 受け箱の中身を取り出す (C++ 側で free する)。空なら 0。
EM_JS(char*, netPollJs, (), {
    var text = Module["raceNetInbox"];
    if (!text) return 0;
    Module["raceNetInbox"] = "";
    var n = lengthBytesUTF8(text) + 1;
    var p = _malloc(n);
    stringToUTF8(text, p, n);
    return p;
});

EM_JS(char*, netStateJs, (), {
    var text = Module["raceNetState"] || "ONLINE";
    var n = lengthBytesUTF8(text) + 1;
    var p = _malloc(n);
    stringToUTF8(text, p, n);
    return p;
});
#endif

namespace {

#ifdef __EMSCRIPTEN__
class WebRecordService final : public IRecordService {
public:
    bool available() const override { return !apiBase().empty(); }
    void submit(const std::string& line) override {
        const std::string base = apiBase();
        if (!base.empty()) netSubmitJs(base.c_str(), line.c_str());
    }
    void requestCourse(const std::string& course) override {
        const std::string base = apiBase();
        if (!base.empty()) netRequestJs(base.c_str(), course.c_str());
    }
    bool poll(std::vector<std::string>& lines) override {
        char* text = netPollJs();
        if (!text) return false;
        std::string all = text;
        std::free(text);
        size_t start = 0;
        while (start <= all.size()) {
            size_t end = all.find('\n', start);
            if (end == std::string::npos) end = all.size();
            if (end > start) lines.push_back(all.substr(start, end - start));
            start = end + 1;
        }
        return !lines.empty();
    }
    std::string status() const override {
        if (!available()) return "LOCAL ONLY";
        char* text = netStateJs();
        std::string out = text ? text : "ONLINE";
        std::free(text);
        return out;
    }

private:
    // ページで設定された送り先 URL (末尾の / なし)。未設定なら空。
    static std::string apiBase() {
        char* text = netApiBaseJs();
        std::string out = text ? text : "";
        std::free(text);
        return out;
    }
};
#endif

// 通信手段の無い環境用: 何もしない (手元の記録だけで動く)。
class LocalRecordService final : public IRecordService {
public:
    bool available() const override { return false; }
    void submit(const std::string&) override {}
    void requestCourse(const std::string&) override {}
    bool poll(std::vector<std::string>&) override { return false; }
    std::string status() const override { return "LOCAL ONLY"; }
};

}  // namespace

IRecordService* createRecordService() {
#ifdef __EMSCRIPTEN__
    return new WebRecordService();
#else
    return new LocalRecordService();
#endif
}

void destroyRecordService(IRecordService* service) {
    delete service;
}
