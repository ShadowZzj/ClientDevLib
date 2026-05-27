# build-exe.ps1 — 一键打包 ggtb-broker.exe
# 用法: 在 broker/ 目录下运行 .\build-exe.ps1

$ErrorActionPreference = "Stop"
$brokerDir = $PSScriptRoot

Write-Host "[1/4] Building web frontend..." -ForegroundColor Cyan
Push-Location "$brokerDir\web-vue"
npm run build
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "web build failed" }
Pop-Location

Write-Host "[2/4] Compiling TypeScript..." -ForegroundColor Cyan
Push-Location $brokerDir
npm run build
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "tsc failed" }
Pop-Location

Write-Host "[3/4] Installing pkg (if needed)..." -ForegroundColor Cyan
Push-Location $brokerDir
if (-not (Test-Path "node_modules\@yao-pkg\pkg")) {
    npm install
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "npm install failed" }
}
Pop-Location

Write-Host "[4/4] Packaging exe..." -ForegroundColor Cyan
Push-Location $brokerDir
npx pkg . --output release\ggtb-broker.exe
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "pkg failed" }
Pop-Location

$exe = Join-Path $brokerDir "release\ggtb-broker.exe"
$size = [math]::Round((Get-Item $exe).Length / 1MB, 1)
Write-Host "`nDone! Output: $exe ($size MB)" -ForegroundColor Green
