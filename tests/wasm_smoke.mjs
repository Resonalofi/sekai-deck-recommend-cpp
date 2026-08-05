import assert from "node:assert/strict";
import { copyFile, mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const assetsDirectory = resolve(process.argv[2] ?? "dist/wasm");
const moduleName = "sekai_deck_recommend_wasm";
const temporaryDirectory = await mkdtemp(join(tmpdir(), "sekai-wasm-smoke-"));
const esmPath = join(temporaryDirectory, `${moduleName}.mjs`);

try {
  await copyFile(join(assetsDirectory, `${moduleName}.js`), esmPath);

  globalThis.location = new URL(`https://wasm-smoke.invalid/${moduleName}.js`);
  globalThis.window = globalThis;

  const [{ default: createModule }, wasm, data] = await Promise.all([
    import(pathToFileURL(esmPath).href),
    readFile(join(assetsDirectory, `${moduleName}.wasm`)),
    readFile(join(assetsDirectory, `${moduleName}.data`)),
  ]);
  const preloadedData = data.buffer.slice(
    data.byteOffset,
    data.byteOffset + data.byteLength,
  );
  const module = await createModule({
    wasmBinary: new Uint8Array(wasm.buffer, wasm.byteOffset, wasm.byteLength),
    getPreloadedPackage: () => preloadedData,
  });

  assert.equal(typeof module.SekaiDeckRecommend, "function");
  const engine = new module.SekaiDeckRecommend();
  try {
    const manifest = JSON.parse(engine.masterdataManifest());
    assert.ok(Array.isArray(manifest.required) && manifest.required.length > 0);
    assert.ok(Array.isArray(manifest.optional));
    assert.deepEqual(JSON.parse(engine.recommend("{}", "{}")), {
      error: "region is required.",
    });
  } finally {
    engine.delete();
  }
} finally {
  delete globalThis.window;
  delete globalThis.location;
  await rm(temporaryDirectory, { recursive: true, force: true });
}

console.log("WASM smoke test passed");
