# オンライン機能の案 (ランキング / ゴースト共有)

GitHub Pages は**静的ホスティング専用**で、ページ側からデータを書き込む先が
ありません。そのため「Pages に置いたゲーム + 外部の小さな受け口」という
分担が現実解です。受け口の候補を比較します。

## 候補の比較

| 候補 | 書き込み | 費用 | 備考 |
|---|---|---|---|
| Cloudflare Workers + KV | REST API を自作 | 無料枠で十分 | CORS 制御可・JSON・高速。**本命** |
| Supabase (Postgres) | REST / SDK | 無料枠あり | テーブル設計が楽。ランキングの集計を SQL で書ける |
| Firebase (Firestore) | SDK | 無料枠あり | SDK が重め。Wasm からの利用は fetch 経由になる |
| Google Forms + Sheets 公開 CSV | フォームへ POST / CSV 読取 | 無料 | 手軽だが形式が粗く、改ざん・スパムに弱い。試作向き |
| GitHub Issues / API | 要トークン | 無料 | トークンをクライアントに埋め込むことになり**不可** |

## 推奨構成 (Cloudflare Workers + KV)

- ゲーム (GitHub Pages 上の Wasm) から `fetch` で Worker を叩く。
- Worker が KV に「コース別ランキング」と「ゴーストデータ」を保存する。
- ネイティブ版は当面ローカルランキングのまま (HTTP クライアントを持たないため)。
  将来やるなら libcurl 等の導入が別途必要。

### API の叩き台

```
POST /score    { course, totalMs, bestLapMs, name } -> { rank }
GET  /ranking?course=suzuka -> [ { name, totalMs, bestLapMs, date } ... ] (上位20)
POST /ghost    { course, lapMs, samples: [...] }     (最速ラップのポーズ列)
GET  /ghost?course=suzuka -> { lapMs, samples }      (世界最速 or 週間最速のゴースト)
```

- ゴーストの samples は現行の Ghost 記録 (60Hz の x/y/z/yaw) をそのまま
  間引いて送る想定。1 ラップ約 6,000 点 × 16 バイト ≒ 100KB なので、
  30Hz 間引き + 座標の小数丸めで 30KB 程度に収まる。
- 名前は自由入力 (初期値 "PLAYER")。認証は付けない代わりに、
  Worker 側で最低限の検証 (値域チェック・レート制限・コース名の許可リスト)
  を入れる前提。

### ゲーム側の実装ポイント

- 通信は新しいポート (例: `net.hpp` の `INet` / スコア送信キュー) として
  アプリケーション層から分離し、ブラウザ実装だけ fetch を使う。
  送信は非同期で、失敗してもゲームは止めない (ローカル保存が正)。
- ランキング画面は「ローカル / オンライン」のタブ切替にする想定。

## 段階導入の案

1. (済) ローカルランキング — コース別トップ 5、localStorage / ファイル保存。
2. Workers + KV で `/score` と `/ranking` だけ先に作り、オンラインランキング表示。
3. ゴースト共有 (世界最速のゴーストと走れる) — インパクトが大きいので本命機能。
4. 週次リセットやコース別イベントなどの運用はその後。

このドキュメントは案の提示までで、実装は未着手です。
