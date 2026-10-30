#pragma once
// ============================================================================
// recordservice.hpp ― 走行記録のオンライン共有ポート (IRecordService)
// 役割: アプリ層は「この記録を共有して」「このコースの記録をちょうだい」と
//       頼むだけで、通信の方法 (fetch / HTTP ライブラリ) は外側に任せる。
//       記録は RaceRecord::serialize() の 1 行文字列で受け渡す。
//
// 通信は非同期: request したら、後のフレームで poll すると届いた分が返る。
// 失敗してもゲームは止めない (手元の記録だけで動き続ける) のが契約。
// サーバー側 (無料の Cloudflare Workers) の実装は server/ にある。
// ============================================================================

#include <string>
#include <vector>

struct IRecordService {
    virtual ~IRecordService() = default;

    // オンラインの保存先が設定されているか (無ければ手元だけで動く)。
    virtual bool available() const = 0;
    // 記録を 1 件送る (送りっぱなし。結果は status に出る)。
    virtual void submit(const std::string& recordLine) = 0;
    // コースの共有記録を取りに行く。
    virtual void requestCourse(const std::string& course) = 0;
    // 届いた記録があれば lines に入れて true (同じものは 2 度返さない)。
    virtual bool poll(std::vector<std::string>& lines) = 0;
    // 状態の短い表示 ("ONLINE" / "LOCAL ONLY" / "SYNCING" / "ERROR" など)。
    virtual std::string status() const = 0;
};

// 実装の生成と破棄 (platform 層が定義する)。
IRecordService* createRecordService();
void destroyRecordService(IRecordService* service);
