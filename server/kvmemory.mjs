// ============================================================================
// kvmemory.mjs ― Workers KV のメモリ上の代用品 (テストとローカル確認用)
// get / put (expirationTtl 対応) だけを持つ。本番では使わない。
// ============================================================================

export class MemoryKV {
  constructor() {
    this.map = new Map();
  }

  async get(key, type) {
    const entry = this.map.get(key);
    if (!entry || (entry.expires && entry.expires < Date.now())) return null;
    return type === 'json' ? JSON.parse(entry.value) : entry.value;
  }

  async put(key, value, options = {}) {
    const expires = options.expirationTtl ? Date.now() + options.expirationTtl * 1000 : 0;
    this.map.set(key, { value: String(value), expires });
  }
}
