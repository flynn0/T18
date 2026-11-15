const fs = require("fs");
const path = require("path");
const { execSync } = require("child_process");

const ROOT = path.resolve(__dirname, "..");
const WORKER_DIR = path.join(ROOT, "worker");
const DOCS_DIR = path.join(ROOT, "docs");
const TMP_OUT = path.join(ROOT, ".tmp_build");

if (!fs.existsSync(DOCS_DIR)) {
  fs.mkdirSync(DOCS_DIR, { recursive: true });
}
if (!fs.existsSync(TMP_OUT)) {
  fs.mkdirSync(TMP_OUT, { recursive: true });
}

console.log("[Build-Docs] Compiling frontend.ts...");
execSync(`./node_modules/.bin/tsc src/frontend.ts --outDir "${TMP_OUT}" --module commonjs --target es2020 --skipLibCheck`, {
  cwd: WORKER_DIR,
  stdio: "inherit"
});

console.log("[Build-Docs] Evaluating renderDashboardHtml()...");
const { renderDashboardHtml } = require(path.join(TMP_OUT, "frontend.js"));
const html = renderDashboardHtml();

// Syntax check JavaScript in HTML
const scriptStart = html.lastIndexOf("<script>") + 8;
const scriptEnd = html.lastIndexOf("</script>");
if (scriptStart > 7 && scriptEnd > scriptStart) {
  const scriptContent = html.substring(scriptStart, scriptEnd);
  const testScriptPath = path.join(TMP_OUT, "test-script.js");
  fs.writeFileSync(testScriptPath, scriptContent);
  console.log("[Build-Docs] Validating embedded JavaScript syntax with node --check...");
  execSync(`node --check "${testScriptPath}"`, { stdio: "inherit" });
  console.log("[Build-Docs] JavaScript syntax is 100% valid!");
}

console.log("[Build-Docs] Writing docs/index.html and docs/404.html...");
fs.writeFileSync(path.join(DOCS_DIR, "index.html"), html, "utf8");
fs.writeFileSync(path.join(DOCS_DIR, "404.html"), html, "utf8");

// Cleanup temp build
fs.rmSync(TMP_OUT, { recursive: true, force: true });

console.log(`[Build-Docs] Successfully generated ${html.length} bytes into docs/index.html and docs/404.html.`);
