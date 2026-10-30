// ============================================================================
// worker.js ― 走行記録の共有サーバー (Cloudflare Workers + KV、無料枠で動く)
//
// API:
//   GET  /records?course=suzuka   そのコースの共有記録 (ベストラップの速い順、最大 30 件) を JSON で返す
//   POST /records                  記録を 1 件登録する (JSON。形式は validateRecord を参照)
//   GET  /health                   動作確認用
//
// 安全のための方針:
//   - クライアント (ゲーム) に鍵やトークンを置かない。誰でも送れる代わりに、
//     サーバー側で値を厳しく検証する (コース・名前・タイム・速度の妥当性)。
//   - 送り元ごとの連投制限 (既定 20 秒に 1 件)。IP アドレスはそのまま保存せず、
//     日替わりのハッシュにして期限付きで覚えるだけ。
//   - 保存するのは名前 (英数字 12 文字まで)・コース・車種・強さ・タイム・区間速度だけ。
//   - 受け付けるページの出所 (Origin) を ALLOWED_ORIGINS で絞れる。
//   - リクエストの大きさの上限 (8KB)。
//
// 無料枠 (Workers: 1 日 10 万リクエスト、KV: 1 日 1,000 書き込み / 10 万読み出し) に
// 収まるよう、コースごとに 1 つのキーへまとめて保存する。
// ============================================================================

// 受け付けるコースと、全長 (m)。最速でも「全長 ÷ 最高速 82 m/s」より速い
// ラップはありえないので、それを下回る記録は不正として拒否する。
const COURSES = {
  suzuka: 5793,
  flow: 3646,
  coast: 4010,
};
const MAX_LAPS = 20;            // 周回数の上限 (ゲームで選べるのは 1〜10 周)
const MAX_SPEED = 82;           // 車の最高速 (m/s)
const PROFILE_BINS = 100;       // 区間速度の数 (ゲームの kProfileBins と同じ)
const KEEP_PER_COURSE = 30;     // コースごとに残す件数
const SUBMIT_INTERVAL_SEC = 20; // 同じ送り元からの連投を断る間隔
const MAX_BODY_BYTES = 8192;

// ----------------------------------------------------------------------------
// 検証
// ----------------------------------------------------------------------------

// 名前を英大文字・数字・_- だけ、12 文字までにする (ゲーム側の sanitizeName と同じ規則)。
export function cleanName(raw) {
  const name = String(raw ?? '').toUpperCase().replace(/[^A-Z0-9_-]/g, '').slice(0, 12);
  return name;
}

// 受け取った JSON を検証し、保存する形に整える。問題があれば理由の文字列を投げる。
export function validateRecord(body) {
  if (!body || typeof body !== 'object') throw 'body must be an object';
  const course = String(body.course ?? '');
  if (!Object.prototype.hasOwnProperty.call(COURSES, course)) throw 'unknown course';
  const name = cleanName(body.name);
  if (!name) throw 'name required';
  const car = Number(body.car);
  const level = Number(body.level);
  if (!Number.isInteger(car) || car < 0 || car > 2) throw 'bad car';
  if (!Number.isInteger(level) || level < -1 || level > 2) throw 'bad level';
  const laps = body.laps === undefined ? 3 : Number(body.laps);  // 古いゲームは周回数を送らない (3 周)
  if (!Number.isInteger(laps) || laps < 1 || laps > MAX_LAPS) throw 'bad laps';
  const total = Number(body.total);
  const bestLap = Number(body.bestLap);
  const minLap = COURSES[course] / MAX_SPEED;
  if (!Number.isFinite(bestLap) || bestLap < minLap || bestLap > 1800) throw 'bad bestLap';
  if (!Number.isFinite(total) || total < bestLap * laps * 0.98 || total > 36000) throw 'bad total';
  const profile = body.profile;
  if (!Array.isArray(profile) || profile.length !== PROFILE_BINS) throw 'bad profile';
  const speeds = profile.map(Number);
  if (speeds.some((v) => !Number.isFinite(v) || v < 0 || v > MAX_SPEED * 1.1)) throw 'bad profile value';
  // 区間速度の平均とベストラップが大きく食い違う記録 (作り物) は拒否する。
  const mean = speeds.reduce((a, b) => a + b, 0) / speeds.length;
  const implied = COURSES[course] / bestLap;  // ベストラップから求めた平均速度
  if (mean < implied * 0.6 || mean > implied * 1.6) throw 'profile does not match lap time';
  return {
    name,
    course,
    car,
    level,
    laps,
    total: Math.round(total * 1000) / 1000,
    bestLap: Math.round(bestLap * 1000) / 1000,
    profile: speeds.map((v) => Math.round(v * 10) / 10),
    at: new Date().toISOString().slice(0, 10),  // 日付だけ (時刻は残さない)
  };
}

// ----------------------------------------------------------------------------
// HTTP
// ----------------------------------------------------------------------------

function corsHeaders(request, env) {
  const origin = request.headers.get('Origin') || '';
  const allowed = String(env.ALLOWED_ORIGINS || '*').split(',').map((s) => s.trim()).filter(Boolean);
  const ok = allowed.includes('*') || allowed.includes(origin);
  return {
    'Access-Control-Allow-Origin': ok ? (allowed.includes('*') ? '*' : origin) : 'null',
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Access-Control-Max-Age': '86400',
    Vary: 'Origin',
  };
}

function json(data, status, headers) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...headers },
  });
}

// 送り元の識別子: IP をそのまま残さないよう、日付と秘密の塩を混ぜてハッシュ化する。
async function clientKey(request, env) {
  const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
  const day = new Date().toISOString().slice(0, 10);
  const data = new TextEncoder().encode(`${env.RATE_SALT || 'racegame'}|${day}|${ip}`);
  const digest = await crypto.subtle.digest('SHA-256', data);
  return Array.from(new Uint8Array(digest).slice(0, 12)).map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function loadCourse(env, course) {
  const list = await env.RECORDS.get(`course:${course}`, 'json');
  return Array.isArray(list) ? list : [];
}

export default {
  async fetch(request, env) {
    const headers = corsHeaders(request, env);
    if (headers['Access-Control-Allow-Origin'] === 'null') {
      return json({ error: 'origin not allowed' }, 403, headers);
    }
    if (request.method === 'OPTIONS') {
      return new Response(null, { status: 204, headers });
    }
    const url = new URL(request.url);

    if (url.pathname === '/health') {
      return json({ ok: true }, 200, headers);
    }

    if (url.pathname === '/records' && request.method === 'GET') {
      const course = url.searchParams.get('course') || '';
      if (!Object.prototype.hasOwnProperty.call(COURSES, course)) {
        return json({ error: 'unknown course' }, 400, headers);
      }
      return json(await loadCourse(env, course), 200, headers);
    }

    if (url.pathname === '/records' && request.method === 'POST') {
      // 大きすぎる本文は読む前に断る。
      const length = Number(request.headers.get('Content-Length') || '0');
      if (length > MAX_BODY_BYTES) return json({ error: 'too large' }, 413, headers);
      const text = await request.text();
      if (text.length > MAX_BODY_BYTES) return json({ error: 'too large' }, 413, headers);

      // 連投制限 (期限付きのキー。KV の最短期限は 60 秒なので、時刻で細かく判定する)。
      const key = `rate:${await clientKey(request, env)}`;
      const last = Number(await env.RECORDS.get(key));
      const now = Date.now();
      if (last && now - last < SUBMIT_INTERVAL_SEC * 1000) {
        return json({ error: 'too many requests' }, 429, headers);
      }

      let record;
      try {
        record = validateRecord(JSON.parse(text));
      } catch (e) {
        return json({ error: typeof e === 'string' ? e : 'invalid json' }, 400, headers);
      }
      await env.RECORDS.put(key, String(now), { expirationTtl: 120 });

      // コースの一覧へ加える。同じ名前の人はベストラップの速い方だけ残す
      // (周回数が違っても比べられるよう、合計ではなくベストラップで比べる)。
      const list = (await loadCourse(env, record.course)).filter((r) => r.name !== record.name || r.bestLap < record.bestLap);
      if (!list.some((r) => r.name === record.name)) list.push(record);
      list.sort((a, b) => a.bestLap - b.bestLap);
      await env.RECORDS.put(`course:${record.course}`, JSON.stringify(list.slice(0, KEEP_PER_COURSE)));
      const rank = list.findIndex((r) => r.name === record.name);
      return json({ ok: true, rank: rank + 1 }, 201, headers);
    }

    return json({ error: 'not found' }, 404, headers);
  },
};
