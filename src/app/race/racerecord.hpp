#pragma once
// ============================================================================
// racerecord.hpp ― 走行記録と、記録からの学習 (RaceRecord / ProfileRecorder / RecordBook)
// 役割:
//   - RaceRecord: 1 レース分の記録 (名前・コース・タイム・区間ごとの速度)。
//     1 行の文字列にして保存・送信する (オンライン共有もこの形で受け渡す)。
//   - ProfileRecorder: 走行中に「コースのどの区間を何 m/s で走ったか」を集め、
//     最速ラップの速度プロファイルを作る。
//   - RecordBook: 手元の記録 (自分の記録＋オンラインで共有された他人の記録) と、
//     そこから学習した内容 (AI の目標速度・難易度ごとのペース補正) と、
//     これまでに戦った対戦相手の一覧を持つ。
// 学習のしくみ:
//   1. 速度の学習: コースごとに速い記録を上位から数件選び、区間ごとの速度を
//      平均して「人間が実際に出している速度」を作る。AI はこれを目標速度に混ぜる。
//   2. 強さの学習: レースの結果 (プレイヤーが勝ったか負けたか) から、
//      難易度ごとのペースを少しずつ上げ下げする (勝ちすぎれば速く、負けすぎれば遅く)。
// 依存: 標準ライブラリのみ。保存・送信の実務は外側 (platform) の仕事。
// ============================================================================

#include <string>
#include <vector>

// 速度プロファイルの区間数 (コース全長をこの数に等分する)。
inline constexpr int kProfileBins = 100;
// 保存キー (platform の保存口がこの名前で出し入れする)。
inline constexpr const char* kRecordStorageKey = "racegameRecords";
inline constexpr const char* kPlayerNameStorageKey = "racegamePlayerName";

// 名前を「英大文字・数字・_- だけ、最大 12 文字」に整える (空なら fallback)。
// ゲーム内フォントで表示でき、保存形式の区切り文字も混ざらないようにするため。
std::string sanitizeName(const std::string& raw, const std::string& fallback = "PLAYER");

struct RaceRecord {
    std::string name;            // 走った人の名前
    std::string course;          // コース stem (例: "suzuka")
    int car = 0;                 // 車種 (0..2)
    int level = -1;              // 対戦した AI の強さ (-1 はソロ)
    int laps = 3;                // 周回数 (1〜20)
    float total = 0.0f;          // 全周回の合計タイム (秒)
    float bestLap = 0.0f;        // ベストラップ (秒)
    std::vector<float> profile;  // 区間ごとの速度 (m/s)、kProfileBins 個

    // 1 行の文字列にする / 戻す。
    //   形式: R2|名前|コース|車種|強さ|周回数|合計|ベスト|速度,速度,...
    //   (周回数の無い旧形式 R1 も 3 周として読める)
    // 記録どうしの速さは、周回数に左右されないベストラップで比べる。
    std::string serialize() const;
    static bool parse(const std::string& line, RaceRecord& out);
    // 値が妥当か (壊れた記録・ありえない記録を学習に使わないため)。
    bool valid() const;
};

class ProfileRecorder {
public:
    // レース開始時に呼ぶ (コース全長を覚えて空にする)。
    void reset(float trackLength);
    // 物理ステップごとに、いまの位置 s (m) と速さ (m/s) を記録する。
    void sample(float s, float speed);
    // ラップ完了時に呼ぶ。最速ラップならその速度プロファイルを残す。
    void finishLap(float lapTime);
    // 最速ラップの速度プロファイル (まだ無ければ空)。
    const std::vector<float>& bestProfile() const { return best_; }

private:
    float length_ = 1.0f;
    std::vector<float> sum_;   // 現在ラップの区間ごとの速度の合計
    std::vector<int> count_;   // 現在ラップの区間ごとのサンプル数
    std::vector<float> best_;  // 最速ラップのプロファイル
    float bestTime_ = -1.0f;
};

class RecordBook {
public:
    // --- 記録 ----------------------------------------------------------------
    // 自分の記録を加える (コースごとに速い順 5 件まで残す)。
    void addOwn(const RaceRecord& record);
    // オンラインから届いた他人の記録を加える (コースごとに速い順 30 件まで)。
    // 同じ名前・同じタイムの重複は無視する。加えた件数を返す。
    int addShared(const RaceRecord& record);
    int sharedCount(const std::string& course) const;

    // --- 学習 ----------------------------------------------------------------
    // 学習した区間速度 (速い記録の上位を平均したもの)。記録が無ければ空。
    std::vector<float> learnedProfile(const std::string& course) const;
    // 対戦相手のモデルにする記録を count 件まで選ぶ (自分以外の人の記録を、
    // 自分のベストに近い速さの順に)。足りない分は返らない。
    std::vector<RaceRecord> pickRivalModels(const std::string& course, const std::string& self, int count) const;
    // 難易度ごとのペース補正 (勝敗から学習した値、±8% まで)。
    float paceAdjust(int level) const;
    // レース結果を学ぶ: 対戦相手の名前・プレイヤーの順位 (1 始まり)・出走数。
    void noteRace(int level, const std::vector<std::string>& opponents, int position, int entrants);

    // --- 対戦の記録 ------------------------------------------------------------
    int opponentsFaced() const { return static_cast<int>(opponents_.size()); }  // 戦った相手の人数 (重複なし)
    int battles() const { return battles_; }    // 対戦した延べ人数
    int wins() const { return wins_; }          // AI 相手に 1 位になった回数

    // --- 保存 ----------------------------------------------------------------
    std::string serialize() const;
    void deserialize(const std::string& text);

private:
    std::vector<RaceRecord> own_;     // 自分の記録
    std::vector<RaceRecord> shared_;  // 他人の記録 (オンライン)
    std::vector<std::string> opponents_;  // 戦った相手の名前 (重複なし、昇順)
    float paceAdjust_[3] = {0, 0, 0};     // 難易度ごとのペース補正
    int battles_ = 0;
    int wins_ = 0;
};
