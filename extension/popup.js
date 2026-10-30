// ドリフト調整ポップアップ。
// ゲームページの localStorage['racegameDriftTuning'] を読み書きする。
// ゲーム側は 0.5 秒ごとに localStorage を確認して、変わっていれば
// 走行中でもその値を取り込む（src/main.cpp の poll 処理）。
"use strict";

const KEY = "racegameDriftTuning";

// 項目定義。保存キーはゲーム側の parseTuning と一致させる。
// disp は表示用の換算（保存値 -> 表示値）、inv はその逆。
const ITEMS = [
  { key: "gripDrift", label: "ドリフトグリップ", desc: "ドリフト時の横グリップ。小さいほどよく滑る", min: 0.2, max: 4.0, step: 0.05, def: 1.15, unit: "" },
  { key: "inertia", label: "慣性の強さ", desc: "大きいほど速度が向きを保ち、横滑りが長引く", min: 0.4, max: 2.5, step: 0.05, def: 1.0, unit: "" },
  { key: "driftDecel", label: "ドリフト中の減速率", desc: "大きいほどドリフトで失速する", min: 0.0, max: 1.5, step: 0.05, def: 0.15, unit: "" },
  { key: "maxSlip", label: "最大スリップ角", desc: "目標スリップ角の上限", min: 20, max: 72, step: 1, def: 54, unit: "°",
    disp: (rad) => Math.round(rad * 57.29578), inv: (deg) => deg / 57.29578 },
  { key: "minSpeed", label: "ドリフト最低速度", desc: "この速度より遅いとドリフトに入らない", min: 0, max: 90, step: 2, def: 32, unit: " km/h",
    disp: (ms) => Math.round(ms * 3.6), inv: (kmh) => kmh / 3.6 },
  { key: "gripNormal", label: "通常グリップ", desc: "通常時の横グリップ", min: 6, max: 30, step: 0.5, def: 15, unit: "" },
  { key: "entry", label: "進入の鋭さ（ENTRY）", desc: "小さいとじわり、大きいと切った瞬間に一気に曲がる", min: 0.0, max: 1.0, step: 0.05, def: 0, unit: "" },
];

const values = {};   // 保存値（ゲーム内部の単位）
const els = {};      // key -> {slider, val}

function toDisplay(item, saved) {
  return item.disp ? item.disp(saved) : saved;
}
function toSaved(item, display) {
  return item.inv ? item.inv(display) : display;
}
function defaults() {
  const out = {};
  for (const item of ITEMS) out[item.key] = toSaved(item, item.def);
  return out;
}
function serialize(v) {
  const f = (x) => Number(x).toFixed(3);
  return `gripNormal=${f(v.gripNormal)} gripDrift=${f(v.gripDrift)} inertia=${f(v.inertia)} ` +
         `driftDecel=${f(v.driftDecel)} maxSlip=${f(v.maxSlip)} minSpeed=${f(v.minSpeed)} entry=${f(v.entry)}`;
}
function parse(text) {
  const out = defaults();
  if (!text) return out;
  for (const part of text.trim().split(/\s+/)) {
    const eq = part.indexOf("=");
    if (eq < 0) continue;
    const k = part.slice(0, eq);
    const n = parseFloat(part.slice(eq + 1));
    if (k in out && Number.isFinite(n)) out[k] = n;
  }
  return out;
}

// --- ゲームページとの通信（activeTab + scripting） ---------------------------
async function activeTabId() {
  const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
  return tabs.length ? tabs[0].id : null;
}
async function readFromPage() {
  const tabId = await activeTabId();
  if (tabId == null) throw new Error("no tab");
  const [res] = await chrome.scripting.executeScript({
    target: { tabId },
    func: (key) => { try { return localStorage.getItem(key); } catch (e) { return null; } },
    args: [KEY],
  });
  return res ? res.result : null;
}
async function writeToPage(text) {
  const tabId = await activeTabId();
  if (tabId == null) throw new Error("no tab");
  await chrome.scripting.executeScript({
    target: { tabId },
    func: (key, value) => { try { localStorage.setItem(key, value); } catch (e) {} },
    args: [KEY, text],
  });
}

// --- UI ----------------------------------------------------------------------
const statusEl = document.getElementById("status");
function setStatus(msg, isErr) {
  statusEl.textContent = msg;
  statusEl.className = isErr ? "err" : "";
}

let writeTimer = null;
function scheduleWrite() {
  setStatus("反映中…");
  clearTimeout(writeTimer);
  writeTimer = setTimeout(async () => {
    try {
      await writeToPage(serialize(values));
      setStatus("ゲームに反映しました");
    } catch (e) {
      setStatus("書き込めませんでした。ゲームのタブで開いてください", true);
    }
  }, 120);
}

function buildUi() {
  const root = document.getElementById("items");
  for (const item of ITEMS) {
    const row = document.createElement("div");
    row.className = "row";
    const label = document.createElement("label");
    const name = document.createElement("span");
    name.textContent = item.label;
    const val = document.createElement("span");
    val.className = "val";
    label.append(name, val);
    const slider = document.createElement("input");
    slider.type = "range";
    slider.min = item.min; slider.max = item.max; slider.step = item.step;
    slider.addEventListener("input", () => {
      const disp = parseFloat(slider.value);
      values[item.key] = toSaved(item, disp);
      val.textContent = `${disp}${item.unit}`;
      scheduleWrite();
    });
    const desc = document.createElement("div");
    desc.className = "desc";
    desc.textContent = item.desc;
    row.append(label, slider, desc);
    root.appendChild(row);
    els[item.key] = { slider, val, item };
  }
}

function applyToUi() {
  for (const item of ITEMS) {
    const disp = toDisplay(item, values[item.key]);
    const e = els[item.key];
    e.slider.value = disp;
    e.val.textContent = `${disp}${item.unit}`;
  }
}

async function load() {
  try {
    const text = await readFromPage();
    Object.assign(values, parse(text));
    setStatus(text ? "ゲームから現在の値を読み込みました" : "保存値が無いので初期値を表示しています");
  } catch (e) {
    Object.assign(values, defaults());
    setStatus("ゲームのタブで開いてください（このタブからは読み込めません）", true);
  }
  applyToUi();
}

document.getElementById("reload").addEventListener("click", load);
document.getElementById("defaults").addEventListener("click", () => {
  Object.assign(values, defaults());
  applyToUi();
  scheduleWrite();
});

buildUi();
Object.assign(values, defaults());
applyToUi();
load();
