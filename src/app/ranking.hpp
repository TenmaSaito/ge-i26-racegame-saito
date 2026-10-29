#pragma once
// ============================================================================
// ranking.hpp ― ローカルランキング (コース別の合計タイム上位)
// 役割: ゴールした合計タイムをコースごとに上位 5 件まで保持する。
//       保存は Game の差し込み口経由 (ブラウザは localStorage、
//       ネイティブはファイル) で、このクラスは並び替えと文字列化だけ。
//       オンラインランキングは別途案を docs/ONLINE.md に提示してある。
// ============================================================================

#include <string>
#include <vector>

// 保存キー (platform の保存シンクがこの名前で出し入れする)。
inline constexpr const char* kRankingStorageKey = "racegameRanking";

class Ranking {
public:
    // 結果を登録し、順位 (0 始まり) を返す。圏外なら -1。
    int addResult(const std::string& courseStem, float totalSeconds);
    // そのコースのベスト合計タイム (無ければ -1)。
    float best(const std::string& courseStem) const;
    // そのコースの上位一覧 (昇順)。無ければ空。
    const std::vector<float>& entries(const std::string& courseStem) const;
    // 保存用の文字列化と復元 (1 行 = "stem 秒数")。
    std::string serialize() const;
    void deserialize(const std::string& text);

private:
    static constexpr size_t kMaxEntries = 5;  // コースごとに残す件数
    // コース stem → 合計タイムの昇順リスト。
    std::vector<std::pair<std::string, std::vector<float>>> table_;
    std::vector<float>* findTable(const std::string& stem);
    const std::vector<float>* findTable(const std::string& stem) const;
};
