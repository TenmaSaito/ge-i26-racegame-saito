// ============================================================================
// racerecord.cpp ― 走行記録と学習の実装
// ============================================================================

#include "racerecord.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {

constexpr size_t kMaxOwnPerCourse = 5;
constexpr size_t kMaxSharedPerCourse = 30;
constexpr size_t kMaxOpponents = 1000;   // 戦った相手の名前を覚える上限
constexpr float kPaceStep = 0.015f;      // 1 レースで動かすペース補正の幅
constexpr float kPaceLimit = 0.08f;      // ペース補正の上限 (±8%)

// 文字列を区切り文字で分ける。
std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

bool toFloat(const std::string& s, float& out) {
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && end != s.c_str() && *end == '\0' && std::isfinite(out);
}

bool toInt(const std::string& s, int& out) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    out = int(v);
    return end && end != s.c_str() && *end == '\0';
}

// コースごとに速い順へ並べ、上限件数を超えた分を捨てる。
void trimPerCourse(std::vector<RaceRecord>& list, size_t perCourse) {
    std::stable_sort(list.begin(), list.end(), [](const RaceRecord& a, const RaceRecord& b) {
        return a.course != b.course ? a.course < b.course : a.total < b.total;
    });
    std::vector<RaceRecord> kept;
    size_t run = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        run = (i > 0 && list[i].course == list[i - 1].course) ? run + 1 : 0;
        if (run < perCourse) kept.push_back(list[i]);
    }
    list.swap(kept);
}

}  // namespace

std::string sanitizeName(const std::string& raw, const std::string& fallback) {
    std::string out;
    for (char c : raw) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out += c;
        } else if (c == ' ' && !out.empty() && out.back() != '_') {
            out += '_';
        }
        if (out.size() >= 12) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? fallback : out;
}

// ============================================================================
// RaceRecord
// ============================================================================
std::string RaceRecord::serialize() const {
    std::ostringstream ss;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f|%.3f", total, bestLap);
    ss << "R1|" << sanitizeName(name) << '|' << course << '|' << car << '|' << level << '|' << buf << '|';
    for (size_t i = 0; i < profile.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%.1f", profile[i]);
        ss << (i ? "," : "") << buf;
    }
    return ss.str();
}

bool RaceRecord::parse(const std::string& line, RaceRecord& out) {
    const std::vector<std::string> f = split(line, '|');
    if (f.size() != 8 || f[0] != "R1") {
        return false;
    }
    RaceRecord r;
    r.name = sanitizeName(f[1], "");
    r.course = sanitizeName(f[2], "");
    for (char& c : r.course) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);  // stem は小文字
    if (!toInt(f[3], r.car) || !toInt(f[4], r.level) || !toFloat(f[5], r.total) || !toFloat(f[6], r.bestLap)) {
        return false;
    }
    for (const std::string& v : split(f[7], ',')) {
        float speed;
        if (!toFloat(v, speed)) return false;
        r.profile.push_back(speed);
    }
    if (!r.valid()) {
        return false;
    }
    out = std::move(r);
    return true;
}

bool RaceRecord::valid() const {
    if (name.empty() || course.empty() || car < 0 || car > 2 || level < -1 || level > 2) return false;
    if (!(total > 0.0f && total < 7200.0f && bestLap > 0.0f && bestLap <= total)) return false;
    if (profile.size() != size_t(kProfileBins)) return false;
    for (float v : profile) {
        if (!(v >= 0.0f && v <= 120.0f)) return false;
    }
    return true;
}

// ============================================================================
// ProfileRecorder
// ============================================================================
void ProfileRecorder::reset(float trackLength) {
    length_ = std::max(1.0f, trackLength);
    sum_.assign(kProfileBins, 0.0f);
    count_.assign(kProfileBins, 0);
    best_.clear();
    bestTime_ = -1.0f;
}

void ProfileRecorder::sample(float s, float speed) {
    if (sum_.empty()) return;
    int bin = int(s / length_ * float(kProfileBins));
    bin = std::max(0, std::min(kProfileBins - 1, bin));
    sum_[size_t(bin)] += speed;
    count_[size_t(bin)] += 1;
}

void ProfileRecorder::finishLap(float lapTime) {
    if (sum_.empty()) return;
    // 区間の平均速度。記録が抜けた区間 (高速で 1 サンプルも落ちなかった等) は
    // 前の区間の値で埋める。
    std::vector<float> lap(kProfileBins, 0.0f);
    int filled = 0;
    for (int i = 0; i < kProfileBins; ++i) {
        if (count_[size_t(i)] > 0) {
            lap[size_t(i)] = sum_[size_t(i)] / float(count_[size_t(i)]);
            ++filled;
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < kProfileBins; ++i) {
            if (count_[size_t(i)] == 0) lap[size_t(i)] = lap[size_t((i + kProfileBins - 1) % kProfileBins)];
        }
    }
    // 区間の 8 割以上が埋まった (ほぼ 1 周走った) ラップだけを採用する。
    if (filled >= kProfileBins * 8 / 10 && (bestTime_ < 0.0f || lapTime < bestTime_)) {
        best_ = lap;
        bestTime_ = lapTime;
    }
    sum_.assign(kProfileBins, 0.0f);
    count_.assign(kProfileBins, 0);
}

// ============================================================================
// RecordBook
// ============================================================================
void RecordBook::addOwn(const RaceRecord& record) {
    if (!record.valid()) return;
    own_.push_back(record);
    trimPerCourse(own_, kMaxOwnPerCourse);
}

int RecordBook::addShared(const RaceRecord& record) {
    if (!record.valid()) return 0;
    for (const RaceRecord& r : shared_) {
        if (r.course == record.course && r.name == record.name && std::fabs(r.total - record.total) < 0.001f) {
            return 0;  // 同じ記録
        }
    }
    shared_.push_back(record);
    trimPerCourse(shared_, kMaxSharedPerCourse);
    return 1;
}

int RecordBook::sharedCount(const std::string& course) const {
    return int(std::count_if(shared_.begin(), shared_.end(),
                             [&](const RaceRecord& r) { return r.course == course; }));
}

std::vector<float> RecordBook::learnedProfile(const std::string& course) const {
    // 自分と他人の記録をまとめて速い順に並べ、上位 5 件を重み付きで平均する
    // (1 位の重み 5、2 位 4 … 5 位 1)。外れ値に引っ張られにくく、速い人ほど効く。
    std::vector<const RaceRecord*> pool;
    for (const RaceRecord& r : own_) if (r.course == course) pool.push_back(&r);
    for (const RaceRecord& r : shared_) if (r.course == course) pool.push_back(&r);
    if (pool.empty()) return {};
    std::sort(pool.begin(), pool.end(), [](const RaceRecord* a, const RaceRecord* b) { return a->total < b->total; });
    const size_t use = std::min<size_t>(5, pool.size());
    std::vector<float> out(kProfileBins, 0.0f);
    float weightSum = 0.0f;
    for (size_t k = 0; k < use; ++k) {
        const float w = float(5 - k);
        for (int i = 0; i < kProfileBins; ++i) out[size_t(i)] += pool[k]->profile[size_t(i)] * w;
        weightSum += w;
    }
    for (float& v : out) v /= weightSum;
    return out;
}

std::vector<RaceRecord> RecordBook::pickRivalModels(const std::string& course, const std::string& self,
                                                   int count) const {
    // 自分のベスト (無ければ中央値) に近い速さの人から選ぶ。同じ人は 1 回だけ。
    float reference = -1.0f;
    for (const RaceRecord& r : own_) {
        if (r.course == course && (reference < 0.0f || r.total < reference)) reference = r.total;
    }
    std::vector<const RaceRecord*> pool;
    for (const RaceRecord& r : shared_) {
        if (r.course == course && r.name != self) pool.push_back(&r);
    }
    if (pool.empty() || count <= 0) return {};
    if (reference < 0.0f) {
        std::vector<float> totals;
        for (const RaceRecord* r : pool) totals.push_back(r->total);
        std::nth_element(totals.begin(), totals.begin() + long(totals.size() / 2), totals.end());
        reference = totals[totals.size() / 2];
    }
    std::sort(pool.begin(), pool.end(), [&](const RaceRecord* a, const RaceRecord* b) {
        return std::fabs(a->total - reference) < std::fabs(b->total - reference);
    });
    std::vector<RaceRecord> out;
    for (const RaceRecord* r : pool) {
        bool dup = false;
        for (const RaceRecord& o : out) dup = dup || o.name == r->name;
        if (!dup) out.push_back(*r);
        if (int(out.size()) >= count) break;
    }
    return out;
}

float RecordBook::paceAdjust(int level) const {
    return (level >= 0 && level < 3) ? paceAdjust_[level] : 0.0f;
}

void RecordBook::noteRace(int level, const std::vector<std::string>& opponents, int position, int entrants) {
    if (opponents.empty()) return;  // ソロは対戦に数えない
    battles_ += int(opponents.size());
    for (const std::string& name : opponents) {
        auto it = std::lower_bound(opponents_.begin(), opponents_.end(), name);
        if ((it == opponents_.end() || *it != name) && opponents_.size() < kMaxOpponents) {
            opponents_.insert(it, name);
        }
    }
    if (position == 1) ++wins_;
    // 強さの学習: 勝てば次から少し速く、最下位なら少し遅く (接戦に寄せる)。
    if (level >= 0 && level < 3) {
        float& adj = paceAdjust_[level];
        if (position == 1) adj += kPaceStep;
        else if (position == entrants) adj -= kPaceStep;
        adj = std::max(-kPaceLimit, std::min(kPaceLimit, adj));
    }
}

std::string RecordBook::serialize() const {
    std::ostringstream ss;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "P %.4f %.4f %.4f\n", paceAdjust_[0], paceAdjust_[1], paceAdjust_[2]);
    ss << buf << "B " << battles_ << ' ' << wins_ << '\n';
    for (const std::string& name : opponents_) ss << "O " << name << '\n';
    for (const RaceRecord& r : own_) ss << "M " << r.serialize() << '\n';
    for (const RaceRecord& r : shared_) ss << "S " << r.serialize() << '\n';
    return ss.str();
}

void RecordBook::deserialize(const std::string& text) {
    *this = RecordBook{};
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 3 || line[1] != ' ') continue;
        const std::string body = line.substr(2);
        RaceRecord r;
        switch (line[0]) {
        case 'P': {
            std::istringstream ps(body);
            for (float& v : paceAdjust_) {
                ps >> v;
                if (!std::isfinite(v)) v = 0.0f;
                v = std::max(-kPaceLimit, std::min(kPaceLimit, v));
            }
            break;
        }
        case 'B': {
            std::istringstream bs(body);
            bs >> battles_ >> wins_;
            break;
        }
        case 'O': {
            const std::string name = sanitizeName(body, "");
            if (!name.empty() && opponents_.size() < kMaxOpponents) opponents_.push_back(name);
            break;
        }
        case 'M':
            if (RaceRecord::parse(body, r)) own_.push_back(r);
            break;
        case 'S':
            if (RaceRecord::parse(body, r)) shared_.push_back(r);
            break;
        default:
            break;
        }
    }
    std::sort(opponents_.begin(), opponents_.end());
    opponents_.erase(std::unique(opponents_.begin(), opponents_.end()), opponents_.end());
    trimPerCourse(own_, kMaxOwnPerCourse);
    trimPerCourse(shared_, kMaxSharedPerCourse);
}
