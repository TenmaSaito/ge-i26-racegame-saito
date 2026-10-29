// ============================================================================
// ranking.cpp ― Ranking の実装 (ranking.hpp の宣言に対応)
// ============================================================================

#include "ranking.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

std::vector<float>* Ranking::findTable(const std::string& stem) {
    for (auto& kv : table_) {
        if (kv.first == stem) return &kv.second;
    }
    return nullptr;
}

const std::vector<float>* Ranking::findTable(const std::string& stem) const {
    for (const auto& kv : table_) {
        if (kv.first == stem) return &kv.second;
    }
    return nullptr;
}

int Ranking::addResult(const std::string& courseStem, float totalSeconds) {
    if (totalSeconds <= 0) return -1;
    std::vector<float>* list = findTable(courseStem);
    if (!list) {
        table_.push_back({courseStem, {}});
        list = &table_.back().second;
    }
    // 昇順に挿入し、上位だけ残す。挿入位置がそのまま順位になる。
    auto it = std::lower_bound(list->begin(), list->end(), totalSeconds);
    int rank = int(it - list->begin());
    list->insert(it, totalSeconds);
    if (list->size() > kMaxEntries) list->resize(kMaxEntries);
    return rank < int(kMaxEntries) ? rank : -1;
}

float Ranking::best(const std::string& courseStem) const {
    const std::vector<float>* list = findTable(courseStem);
    return (list && !list->empty()) ? list->front() : -1.0f;
}

const std::vector<float>& Ranking::entries(const std::string& courseStem) const {
    static const std::vector<float> kEmpty;
    const std::vector<float>* list = findTable(courseStem);
    return list ? *list : kEmpty;
}

std::string Ranking::serialize() const {
    std::string out;
    char line[96];
    for (const auto& kv : table_) {
        for (float t : kv.second) {
            std::snprintf(line, sizeof(line), "%s %.3f\n", kv.first.c_str(), t);
            out += line;
        }
    }
    return out;
}

void Ranking::deserialize(const std::string& text) {
    table_.clear();
    std::istringstream in(text);
    std::string stem;
    float seconds = 0;
    while (in >> stem >> seconds) {
        addResult(stem, seconds);  // 挿入順に整列されるのでそのまま使える
    }
}
