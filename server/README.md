# 走行記録の共有サーバー

ゲームの走行記録（名前・コース・タイム・区間ごとの速度）をオンラインで共有するための、小さな API サーバーです。
Cloudflare Workers と Workers KV の**無料枠**で動きます（クレジットカード不要）。

共有された記録は、ほかのプレイヤーのゲームで

- 対戦相手の AI のモデル（その人の走りを真似る AI が、その人の名前で出走する）
- AI の学習（速い記録の区間速度を、AI の目標速度に混ぜる）

として使われます。

## 安全のための設計

| 心配ごと | 対策 |
|---|---|
| 鍵の漏えい | ゲーム側に鍵・トークンを一切置かない。書き込みの正しさはサーバーが検証する |
| 不正な記録 | コース名は許可リストのみ。名前は英数字 12 文字まで。最高速でも出せないラップ、合計とベストの矛盾、区間速度とラップタイムの矛盾、区間数の違いはすべて拒否 |
| 連投・荒らし | 同じ送り元からは 20 秒に 1 件まで。本文は 8KB まで。コースごとに速い 30 件だけ残す（同じ名前は速い 1 件だけ） |
| 個人情報 | 保存するのは名前・コース・車種・強さ・タイム・区間速度・日付だけ。IP アドレスは保存しない（連投判定には日替わりのハッシュを 2 分だけ使う） |
| よそのサイトからの利用 | `ALLOWED_ORIGINS` でゲームのページのオリジンだけに絞れる |
| 料金 | 無料枠を超えると Cloudflare はリクエストを止めるだけで、勝手に課金されない（有料プランに切り替えない限り） |

## 公開の手順（初回だけ・5 分程度）

1. [Cloudflare](https://dash.cloudflare.com/sign-up) の無料アカウントを作る。
2. このフォルダで Cloudflare にログインする（ブラウザが開く）。

   ```bash
   cd server
   npx wrangler login
   ```

3. 記録の保存先（KV）を作り、表示された `id` を `wrangler.toml` の `REPLACE_WITH_YOUR_KV_NAMESPACE_ID` と置き換える。

   ```bash
   npx wrangler kv namespace create RECORDS
   ```

4. （推奨）`wrangler.toml` の `ALLOWED_ORIGINS` を、ゲームのページのオリジン（例: `https://tenmasaito.github.io`）にする。
5. 公開する。最後に `https://racegame-records.<アカウント名>.workers.dev` のような URL が表示される。

   ```bash
   npx wrangler deploy
   ```

6. ゲームのページ（`web/index.html`、`web/portable/index.html`、`web/portable/game.html`）の次の行に、その URL を書く。

   ```js
   window.RACEGAME_API = 'https://racegame-records.<アカウント名>.workers.dev';
   ```

URL が空のままでも、ゲームは「手元だけ（LOCAL ONLY）」で普通に動きます（記録と学習はブラウザの中に残ります）。

## ローカルで試す（アカウント不要）

```bash
node server/test.mjs        # テスト (検証・連投制限・オリジン制限)
node server/devserver.mjs   # http://localhost:8787 で起動 (保存先はメモリ)
```

ゲームのページで `window.RACEGAME_API = 'http://localhost:8787'` にすると、共有の流れをそのまま試せます。

## API

| メソッド | パス | 内容 |
|---|---|---|
| GET | `/records?course=suzuka` | そのコースの記録（速い順、最大 30 件）の JSON 配列 |
| POST | `/records` | 記録を 1 件登録。本文は `{name, course, car, level, total, bestLap, profile[100]}` |
| GET | `/health` | 動作確認 |
