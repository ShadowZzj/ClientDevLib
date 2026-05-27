const fs = require("fs");
const path = require("path");
const iconv = require("iconv-lite");

const cwd = process.cwd();
const entries = fs.readdirSync(cwd, { withFileTypes: true });
const allLines = new Set();
const cslbFiles = [];

for (const ent of entries) {
    if (!ent.isDirectory()) continue;
    const fp = path.join(cwd, ent.name, "cslb.txt");
    if (!fs.existsSync(fp)) continue;
    cslbFiles.push(fp);
    const buf = fs.readFileSync(fp);
    const content = iconv.decode(buf, "gb2312");
    for (const line of content.split(/\r?\n/)) {
        const trimmed = line.trim();
        if (trimmed) allLines.add(trimmed);
    }
}

const merged = [...allLines].sort();
console.log(`扫描到 ${cslbFiles.length} 个角色目录, 合并后共 ${merged.length} 条唯一记录:`);
for (const item of merged) console.log("  " + item);

const output = merged.join("\r\n") + "\r\n";
const outBuf = iconv.encode(output, "gb2312");
for (const fp of cslbFiles) {
    fs.writeFileSync(fp, outBuf);
}
console.log(`\n已同步写回 ${cslbFiles.length} 个 cslb.txt`);
