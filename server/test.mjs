// ============================================================================
// test.mjs ― 共有サーバー (worker.js) のテスト。Node 18 以降でそのまま動く。
//   node server/test.mjs
// ============================================================================

import assert from 'node:assert/strict';
import worker from './worker.js';
import { MemoryKV } from './kvmemory.mjs';

const env = { RECORDS: new MemoryKV(), ALLOWED_ORIGINS: '*', RATE_SALT: 'test' };
let ipCounter = 0;

// 毎回違う送り元にする (連投制限のテスト以外)。
function post(body, ip = `10.0.0.${++ipCounter}`) {
  return worker.fetch(new Request('https://example.test/records', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'CF-Connecting-IP': ip, Origin: 'https://game.example' },
    body: JSON.stringify(body),
  }), env);
}

function get(path) {
  return worker.fetch(new Request(`https://example.test${path}`, { headers: { Origin: 'https://game.example' } }), env);
}

// 鈴鹿 (5793m) を平均 55 m/s で走った、妥当な記録。
function record(name, bestLap = 105, laps = 3) {
  const mean = 5793 / bestLap;
  return {
    name, course: 'suzuka', car: 0, level: 1, laps, total: bestLap * laps + 4, bestLap,
    profile: Array.from({ length: 100 }, (_, i) => Math.round((mean + Math.sin(i / 5) * 8) * 10) / 10),
  };
}

const tests = [];
const test = (name, fn) => tests.push([name, fn]);

test('accepts a valid record and lists it', async () => {
  const res = await post(record('alice 1'));
  assert.equal(res.status, 201);
  const list = await (await get('/records?course=suzuka')).json();
  assert.equal(list.length, 1);
  assert.equal(list[0].name, 'ALICE1');  // 名前は英数字だけに整えられる
  assert.equal(list[0].profile.length, 100);
  assert.ok(!('ip' in list[0]));
});

test('keeps only the faster record per name and sorts by best lap', async () => {
  await post(record('BOB', 110));
  await post(record('BOB', 100));
  await post(record('BOB', 120));  // 遅い記録は残らない
  const list = await (await get('/records?course=suzuka')).json();
  const bobs = list.filter((r) => r.name === 'BOB');
  assert.equal(bobs.length, 1);
  assert.equal(bobs[0].bestLap, 100);
  assert.deepEqual(list.map((r) => r.bestLap), [...list.map((r) => r.bestLap)].sort((a, b) => a - b));
});

test('accepts other lap counts and old clients without laps', async () => {
  assert.equal((await post(record('DAVE', 101, 10))).status, 201);
  const old = record('ERIN', 102);
  delete old.laps;
  assert.equal((await post(old)).status, 201);
  const list = await (await get('/records?course=suzuka')).json();
  assert.equal(list.find((r) => r.name === 'DAVE').laps, 10);
  assert.equal(list.find((r) => r.name === 'ERIN').laps, 3);
  assert.equal((await post({ ...record('FRANK', 101, 5), total: 300 })).status, 400);  // 5 周で合計が短すぎる
  assert.equal((await post(record('GRACE', 101, 50))).status, 400);                   // 周回数が多すぎる
});

test('rejects impossible or malformed records', async () => {
  const bad = [
    { ...record('X'), course: 'nowhere' },
    { ...record('X'), name: '<!>' },                             // 英数字が残らない名前
    { ...record('X'), bestLap: 30, total: 95 },                  // 最高速でも無理なラップ
    { ...record('X'), total: 100 },                              // 合計がベスト×3 より短い
    { ...record('X'), profile: [1, 2, 3] },                      // 区間数が違う
    { ...record('X'), profile: Array(100).fill(500) },           // ありえない速度
    { ...record('X'), profile: Array(100).fill(10) },            // ラップタイムと矛盾
    { ...record('X'), car: 7 },
  ];
  for (const body of bad) {
    const res = await post(body);
    assert.equal(res.status, 400, JSON.stringify(body).slice(0, 80));
  }
  const res = await worker.fetch(new Request('https://example.test/records', {
    method: 'POST', headers: { 'CF-Connecting-IP': '10.9.9.9' }, body: 'x'.repeat(9000),
  }), env);
  assert.equal(res.status, 413);
});

test('rate limits repeated submissions from one client', async () => {
  assert.equal((await post(record('CAROL'), '192.0.2.1')).status, 201);
  assert.equal((await post(record('CAROL', 99), '192.0.2.1')).status, 429);
});

test('restricts origins when configured', async () => {
  const strict = { ...env, ALLOWED_ORIGINS: 'https://game.example' };
  const ok = await worker.fetch(new Request('https://example.test/health', { headers: { Origin: 'https://game.example' } }), strict);
  assert.equal(ok.status, 200);
  assert.equal(ok.headers.get('Access-Control-Allow-Origin'), 'https://game.example');
  const ng = await worker.fetch(new Request('https://example.test/health', { headers: { Origin: 'https://evil.example' } }), strict);
  assert.equal(ng.status, 403);
});

test('rejects unknown course on GET and unknown paths', async () => {
  assert.equal((await get('/records?course=../../etc')).status, 400);
  assert.equal((await get('/admin')).status, 404);
});

let failed = 0;
for (const [name, fn] of tests) {
  try {
    await fn();
    console.log(`ok   ${name}`);
  } catch (e) {
    failed++;
    console.log(`FAIL ${name}\n     ${e.message}`);
  }
}
console.log(failed ? `${failed} failed` : 'all passed');
process.exit(failed ? 1 : 0);
