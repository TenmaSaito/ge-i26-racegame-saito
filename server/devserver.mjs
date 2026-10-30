// ============================================================================
// devserver.mjs ― 共有サーバーをローカルで動かす (Cloudflare のアカウント不要)
//   node server/devserver.mjs [ポート (既定 8787)]
// worker.js をそのまま使い、保存先だけメモリ (再起動で消える) にする。
// ゲームのページで window.RACEGAME_API = 'http://localhost:8787' にすると試せる。
// ============================================================================

import http from 'node:http';
import worker from './worker.js';
import { MemoryKV } from './kvmemory.mjs';

const port = Number(process.argv[2] || 8787);
const env = { RECORDS: new MemoryKV(), ALLOWED_ORIGINS: '*', RATE_SALT: 'dev' };

http.createServer(async (req, res) => {
  const chunks = [];
  for await (const chunk of req) chunks.push(chunk);
  const body = chunks.length ? Buffer.concat(chunks) : undefined;
  const request = new Request(`http://localhost:${port}${req.url}`, {
    method: req.method,
    headers: { ...req.headers, 'CF-Connecting-IP': req.socket.remoteAddress || 'local' },
    body: req.method === 'GET' || req.method === 'HEAD' ? undefined : body,
  });
  const response = await worker.fetch(request, env);
  res.writeHead(response.status, Object.fromEntries(response.headers));
  res.end(Buffer.from(await response.arrayBuffer()));
  console.log(`${req.method} ${req.url} -> ${response.status}`);
}).listen(port, () => console.log(`records server on http://localhost:${port}`));
