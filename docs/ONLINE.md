# オンライン機能 (走行記録の共有と AI の学習)

## 構成

```
ゲーム (GitHub Pages 上の Wasm)                       Cloudflare Workers + KV (無料枠)
  RaceScene ─ ProfileRecorder (区間速度を記録)
  Game::recordResult ─ RecordBook (手元の記録・学習)
        │  1 行形式 (RaceRecord::serialize)
        ▼
  IRecordService (ports/recordservice.hpp)
        │  ブラウザ: fetch (platform/net/recordservice.cpp)
        ▼
  POST /records  ───────────────────────────────▶  server/worker.js (検証・連投制限)
  GET  /records?course=…  ◀──────────────────────  コースごとに速い 30 件
```

- GitHub Pages は静的ホスティングなので、書き込み先として Cloudflare Workers + KV を使う
  （無料枠: 1 日 10 万リクエスト・KV 書き込み 1,000 回。個人の作品なら十分）。
- ゲーム側に鍵を置かず、サーバーが値を検証する。詳細と公開手順は [server/README.md](../server/README.md)。
- 送り先 URL はページの `window.RACEGAME_API` で設定する。空ならオンライン機能は使わず、
  記録は手元（ブラウザの localStorage / ネイティブは設定ファイル）だけに残る。
- ネイティブ版（Windows / Linux / Android）は今は手元だけ。HTTP ライブラリ（libcurl や
  プラットフォームの API）で `recordservice.cpp` の 4 つの関数を実装すれば、ゲーム側は変更なしで対応できる。

## 走行記録 (RaceRecord)

ゴールするたびに 1 件作る。

| 項目 | 内容 |
|---|---|
| name | プレイヤー名（ページの入力欄。英大文字・数字・_- の 12 文字まで） |
| course / car / level | コース・車種・対戦した AI の強さ（ソロは -1） |
| total / bestLap | 3 周の合計タイム・ベストラップ |
| profile | ベストラップの区間速度。コースを 100 区間に分け、各区間の平均速度 (m/s) |

## 学習のしくみ

1. **速度の学習**: コースごとに、手元と共有の記録を速い順に並べ、上位 5 件の区間速度を
   重み付きで平均する（`RecordBook::learnedProfile`）。AI はコースの曲率から求めた
   速度計画に、これを強さに応じた割合（EASY 30% / NORMAL 60% / HARD 90%）で混ぜる。
   人間が実際に速く走っている区間では AI も攻め、慎重な区間では AI も控える。
2. **相手のモデル化**: 共有された他の人の記録のうち、自分のベストに近い速さの人を選び、
   その人の名前で出走させる。その AI はその人の区間速度を強く真似る（`pickRivalModels`）。
3. **強さの調整**: レース結果から、難易度ごとのペースを ±1.5% ずつ動かす
   （勝てば速く、最下位なら遅く、上限 ±8%）。同じ難易度でも接戦になりやすくなる。

## 対戦の記録

`RecordBook` は、これまでに戦った相手の名前（重複なし）・対戦した延べ人数・1 位になった回数を持つ。
タイトル・選択・リザルトの各画面に「OPPONENTS FACED（戦った相手の人数）」として表示する。
