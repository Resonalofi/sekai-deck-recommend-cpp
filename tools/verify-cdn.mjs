// purge 之后核对 CDN 上的字节确实是本次构建的。
//
// 关键是**逐个编码变体**查：CDN 按 Accept-Encoding 分变体缓存，各变体 TTL 独立，
// 线上出过「br 变体是上一代、identity 变体是新一代」的事故——只查一个变体查不出来。
// glue 与 .wasm 不同代会读到零页，症状是 live-calculator.cpp 抛
// "Music meta not found for musicId=0 musicDif=0"。
//
// usage: node tools/verify-cdn.mjs <dist-dir>
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";
import { brotliDecompressSync, gunzipSync, inflateSync } from "node:zlib";

const ASSETS = [
  "sekai_deck_recommend_wasm.js",
  "sekai_deck_recommend_wasm.wasm",
  "sekai_deck_recommend_wasm.data",
];
const ENCODINGS = ["br", "gzip", "identity"];

const BASE_URL = "https://cdn-eo.resona.cn/wasm/";
const ATTEMPTS = 10;
const INTERVAL_MS = 15000;

// 不带 Origin 请求即可：`.wasm` 虽然带 Vary: Origin（R2 的 CORS 加的），但实测
// EdgeOne 的缓存键不含它——带与不带 Origin 拿到的是同一个条目（Age 完全相同），
// 所以这里查的就是浏览器拿到的那份。

const distDir = resolve(process.argv[2] ?? "dist/wasm");
const sha256 = (buffer) => createHash("sha256").update(buffer).digest("hex");

function decode(buffer, encoding) {
  try {
    if (encoding === "br") return brotliDecompressSync(buffer);
    if (encoding === "gzip") return gunzipSync(buffer);
    if (encoding === "deflate") return inflateSync(buffer);
  } catch {
    return buffer; // fetch 已经替我们解过码了
  }
  return buffer;
}

const expected = new Map();
for (const name of ASSETS) expected.set(name, sha256(await readFile(join(distDir, name))));

async function checkVariant(name, encoding) {
  const url = new URL(name, BASE_URL).href;
  const response = await fetch(url, { headers: { "accept-encoding": encoding } });
  if (!response.ok) return { ok: false, detail: `HTTP ${response.status}` };

  const raw = Buffer.from(await response.arrayBuffer());
  const contentEncoding = (response.headers.get("content-encoding") || "").toLowerCase();
  const want = expected.get(name);
  // 两条都认：undici 有时按 content-encoding 自行解码，有时原样返回
  if (sha256(raw) === want || sha256(decode(raw, contentEncoding)) === want) return { ok: true };
  return {
    ok: false,
    detail: `stale bytes (content-encoding=${contentEncoding || "none"}, ` +
      `age=${response.headers.get("age") ?? "?"}, ` +
      `last-modified=${response.headers.get("last-modified") ?? "?"})`,
  };
}

for (let attempt = 1; ; attempt += 1) {
  const failures = [];
  for (const name of ASSETS) {
    for (const encoding of ENCODINGS) {
      const result = await checkVariant(name, encoding);
      if (!result.ok) failures.push(`${name} [${encoding}] ${result.detail}`);
    }
  }
  if (failures.length === 0) {
    console.log(`all ${ASSETS.length * ENCODINGS.length} variants match this build`);
    break;
  }
  if (attempt >= ATTEMPTS) {
    console.error(`CDN still serving stale bytes after ${attempt} attempts:`);
    for (const failure of failures) console.error(`  ${failure}`);
    process.exit(1);
  }
  console.log(`attempt ${attempt}: ${failures.length} variant(s) not updated yet, waiting`);
  await sleep(INTERVAL_MS);
}
