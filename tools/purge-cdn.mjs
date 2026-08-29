// 发布后清空 EdgeOne 上三件 WASM 产物的缓存，让前端不改一行就能拿到新版本。
//
// 链路是 浏览器 → EdgeOne → Cloudflare（R2 自定义域）→ R2。Cloudflare 那一层用
// Cache Rule 配成了 Bypass cache，所以只有 EdgeOne 需要 purge。
// 前提变了就得改这里：Cloudflare 一旦重新开始缓存，EdgeOne 回源会把旧字节原样
// 拉回来，purge 等于没做——下面的 verify-cdn.mjs 会当场发现。
//
// usage: node tools/purge-cdn.mjs
import { createHash, createHmac } from "node:crypto";
import { setTimeout as sleep } from "node:timers/promises";

const ASSETS = [
  "sekai_deck_recommend_wasm.js",
  "sekai_deck_recommend_wasm.wasm",
  "sekai_deck_recommend_wasm.data",
];

const BASE_URL = "https://cdn-eo.resona.cn/wasm/";
const EO_HOST = "teo.tencentcloudapi.com"; // 国际版是 teo.intl.tencentcloudapi.com

const targets = ASSETS.map((name) => new URL(name, BASE_URL).href);

const sha256hex = (data) => createHash("sha256").update(data).digest("hex");
const hmac = (key, data) => createHmac("sha256", key).update(data).digest();

function requireEnv(name) {
  const value = process.env[name];
  if (!value) throw new Error(`${name} is required`);
  return value;
}

// 腾讯云 TC3-HMAC-SHA256 签名，见 cloud.tencent.com/document/api/1145/50536
async function edgeOneRequest(action, payload) {
  const host = EO_HOST;
  const service = "teo";
  const secretId = requireEnv("TENCENTCLOUD_SECRET_ID");
  const secretKey = requireEnv("TENCENTCLOUD_SECRET_KEY");

  const body = JSON.stringify(payload);
  const timestamp = Math.floor(Date.now() / 1000);
  const date = new Date(timestamp * 1000).toISOString().slice(0, 10);
  const contentType = "application/json; charset=utf-8";
  const signedHeaders = "content-type;host;x-tc-action";
  const canonicalHeaders = `content-type:${contentType}\nhost:${host}\nx-tc-action:${action.toLowerCase()}\n`;
  const canonicalRequest = ["POST", "/", "", canonicalHeaders, signedHeaders, sha256hex(body)].join("\n");
  const scope = `${date}/${service}/tc3_request`;
  const stringToSign = ["TC3-HMAC-SHA256", timestamp, scope, sha256hex(canonicalRequest)].join("\n");
  const signingKey = hmac(hmac(hmac(`TC3${secretKey}`, date), service), "tc3_request");
  const signature = createHmac("sha256", signingKey).update(stringToSign).digest("hex");

  const response = await fetch(`https://${host}`, {
    method: "POST",
    headers: {
      Authorization:
        `TC3-HMAC-SHA256 Credential=${secretId}/${scope}, ` +
        `SignedHeaders=${signedHeaders}, Signature=${signature}`,
      "Content-Type": contentType,
      "X-TC-Action": action,
      "X-TC-Timestamp": String(timestamp),
      "X-TC-Version": "2022-09-01",
    },
    body,
  });

  if (!response.ok && response.status >= 500) {
    const error = new Error(`EdgeOne HTTP ${response.status}`);
    error.retryable = true;
    throw error;
  }
  const json = await response.json();
  const error = json?.Response?.Error;
  if (error) throw new Error(`EdgeOne ${action} ${error.Code}: ${error.Message}`);
  return json.Response;
}

// 网络抖动重试；业务错误（鉴权、配额）不重试，直接抛出去让流水线红掉
async function submitPurge(zoneId) {
  for (let attempt = 1; ; attempt += 1) {
    try {
      // purge_url 按精确 URL 清，不带 Method——Method 只对目录/域名/全站类型生效
      return await edgeOneRequest("CreatePurgeTask", {
        ZoneId: zoneId,
        Type: "purge_url",
        Targets: targets,
      });
    } catch (error) {
      if (attempt >= 3 || !error.retryable) throw error;
      console.log(`purge attempt ${attempt} failed (${error.message}), retrying`);
      await sleep(3000 * attempt);
    }
  }
}

try {
  const result = await submitPurge(requireEnv("EO_ZONE_ID"));
  if (result.FailedList?.length) {
    throw new Error(`EdgeOne rejected targets: ${JSON.stringify(result.FailedList)}`);
  }
  console.log(`EdgeOne purge submitted, JobId=${result.JobId}`);
  for (const target of targets) console.log(`purged ${target}`);
} catch (error) {
  console.error(`purge failed: ${error.message}`);
  process.exit(1);
}
