// build-bundle.mjs — esbuild: bundle broker + embed web-dist into single JS
import { build } from "esbuild";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const webDistDir = path.resolve(__dirname, "web-dist");

// Collect all web-dist files as base64 map
function collectAssets(dir, prefix = "") {
    const assets = {};
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const rel = prefix ? `${prefix}/${entry.name}` : entry.name;
        if (entry.isDirectory()) {
            Object.assign(assets, collectAssets(path.join(dir, entry.name), rel));
        } else {
            assets[rel] = fs.readFileSync(path.join(dir, entry.name)).toString("base64");
        }
    }
    return assets;
}

const assets = collectAssets(webDistDir);
const assetModule = `export default ${JSON.stringify(assets)};`;
fs.writeFileSync(path.resolve(__dirname, "dist", "_embedded_assets.js"), assetModule);

await build({
    entryPoints: [path.resolve(__dirname, "dist", "server.js")],
    bundle: true,
    platform: "node",
    target: "node20",
    outfile: path.resolve(__dirname, "release", "ggtb-broker.js"),
    format: "cjs",
    external: [],
    define: { "process.env.EMBEDDED": '"1"' },
    banner: { js: "// ggtb-broker bundled build\n" },
});

console.log("Bundle written to release/ggtb-broker.js");
