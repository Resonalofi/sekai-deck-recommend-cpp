import { spawn } from "node:child_process";
import { createReadStream } from "node:fs";
import { access, mkdtemp, rm } from "node:fs/promises";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const assetsDirectory = resolve(process.argv[2] ?? "dist/wasm");
const moduleName = "sekai_deck_recommend_wasm";
const assets = new Map([
  [`/${moduleName}.js`, [join(assetsDirectory, `${moduleName}.js`), "text/javascript"]],
  [`/${moduleName}.wasm`, [join(assetsDirectory, `${moduleName}.wasm`), "application/wasm"]],
  [`/${moduleName}.data`, [join(assetsDirectory, `${moduleName}.data`), "application/octet-stream"]],
]);

await Promise.all([...assets.values()].map(([path]) => access(path)));

let resolveResult;
let rejectResult;
const resultPromise = new Promise((resolve, reject) => {
  resolveResult = resolve;
  rejectResult = reject;
});

const page = `<!doctype html>
<meta charset="utf-8">
<title>WASM smoke test</title>
<script type="module">
  const assert = (condition, message) => {
    if (!condition) throw new Error(message);
  };
  const report = (result) => fetch("/__result", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(result),
  });

  try {
    assert(crossOriginIsolated, "Page is not cross-origin isolated");
    assert(typeof SharedArrayBuffer === "function", "SharedArrayBuffer is unavailable");

    const { default: createModule } = await import("/${moduleName}.js");
    const module = await createModule({
      locateFile: (path) => new URL(\`/\${path}\`, location.href).href,
    });

    assert(typeof module.SekaiDeckRecommend === "function", "Missing SekaiDeckRecommend export");
    const engine = new module.SekaiDeckRecommend();
    try {
      const manifest = JSON.parse(engine.masterdataManifest());
      assert(Array.isArray(manifest.required) && manifest.required.length > 0, "Invalid required manifest");
      assert(Array.isArray(manifest.optional), "Invalid optional manifest");

      const recommendation = JSON.parse(engine.recommend("{}", "{}"));
      assert(
        Object.keys(recommendation).length === 1 && recommendation.error === "region is required.",
        "Unexpected recommendation result",
      );
    } finally {
      engine.delete();
    }

    await report({ ok: true });
  } catch (error) {
    await report({ ok: false, error: error?.stack ?? String(error) });
  }
</script>`;

const server = createServer(async (request, response) => {
  response.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  response.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  response.setHeader("Cross-Origin-Resource-Policy", "same-origin");

  try {
    const pathname = new URL(request.url, "http://localhost").pathname;
    if (request.method === "GET" && pathname === "/") {
      response.setHeader("Content-Type", "text/html; charset=utf-8");
      response.end(page);
      return;
    }

    if (request.method === "GET" && assets.has(pathname)) {
      const [path, contentType] = assets.get(pathname);
      response.setHeader("Content-Type", contentType);
      createReadStream(path).pipe(response);
      return;
    }

    if (request.method === "POST" && pathname === "/__result") {
      let body = "";
      for await (const chunk of request) body += chunk;
      const result = JSON.parse(body);
      response.end("ok");
      if (result.ok) resolveResult();
      else rejectResult(new Error(result.error));
      return;
    }

    response.statusCode = 404;
    response.end("Not found");
  } catch (error) {
    response.statusCode = 500;
    response.end(String(error));
    rejectResult(error);
  }
});

await new Promise((resolveListen, rejectListen) => {
  server.once("error", rejectListen);
  server.listen(0, "127.0.0.1", resolveListen);
});

const browserDirectory = await mkdtemp(join(tmpdir(), "sekai-wasm-browser-"));
const { port } = server.address();
const browserUrl = `http://127.0.0.1:${port}/`;
const browserCandidates = process.env.CHROME_PATH
  ? [process.env.CHROME_PATH]
  : process.platform === "win32"
    ? [
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
      ]
    : ["google-chrome", "google-chrome-stable", "chromium", "chromium-browser"];

let browser;
let browserError;
for (const command of browserCandidates) {
  try {
    browser = await new Promise((resolveSpawn, rejectSpawn) => {
      const child = spawn(command, [
        "--headless=new",
        "--no-sandbox",
        "--disable-dev-shm-usage",
        "--no-first-run",
        `--user-data-dir=${browserDirectory}`,
        browserUrl,
      ], { stdio: ["ignore", "ignore", "pipe"] });
      child.once("spawn", () => resolveSpawn(child));
      child.once("error", rejectSpawn);
    });
    break;
  } catch (error) {
    browserError = error;
  }
}

if (!browser) {
  server.close();
  await rm(browserDirectory, { recursive: true, force: true });
  throw new Error(`Could not start Chrome: ${browserError?.message ?? "browser not found"}`);
}

let browserStderr = "";
browser.stderr.on("data", (chunk) => {
  browserStderr += chunk;
});

let timeout;
try {
  await Promise.race([
    resultPromise,
    new Promise((_, reject) => {
      browser.once("exit", (code, signal) => {
        reject(new Error(`Chrome exited before the smoke test completed (${code ?? signal})\n${browserStderr}`));
      });
    }),
    new Promise((_, reject) => {
      timeout = setTimeout(() => reject(new Error(`WASM smoke test timed out\n${browserStderr}`)), 120_000);
    }),
  ]);
  console.log("WASM smoke test passed");
} finally {
  clearTimeout(timeout);
  const browserStopped = browser.exitCode === null
    ? new Promise((resolveExit) => browser.once("exit", resolveExit))
    : Promise.resolve();
  browser.kill();
  await browserStopped;
  server.closeAllConnections();
  await new Promise((resolveClose) => server.close(resolveClose));
  await rm(browserDirectory, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
}
