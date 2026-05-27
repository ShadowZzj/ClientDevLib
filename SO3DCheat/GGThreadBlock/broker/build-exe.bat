@echo off
setlocal
cd /d "%~dp0"

echo [1/5] Building web frontend...
cd web-vue
call npm run build
if %errorlevel% neq 0 (echo web build failed & exit /b 1)
cd ..

echo [2/5] Compiling TypeScript...
call npm run build
if %errorlevel% neq 0 (echo tsc failed & exit /b 1)

echo [3/5] Bundling with esbuild (embed web assets)...
if not exist release mkdir release
call node build-bundle.mjs
if %errorlevel% neq 0 (echo esbuild bundle failed & exit /b 1)

echo [4/5] Generating SEA blob...
cd release
echo { "main": "ggtb-broker.js", "output": "sea-prep.blob", "disableExperimentalSEAWarning": true }>sea-config.json
call node --experimental-sea-config sea-config.json
if %errorlevel% neq 0 (cd .. & echo SEA blob generation failed & exit /b 1)
cd ..

echo [5/5] Injecting into node.exe copy...
for /f "delims=" %%i in ('where node') do (
    copy /Y "%%i" release\ggtb-broker.exe >nul
    goto :inject
)
:inject
call npx postject release\ggtb-broker.exe NODE_SEA_BLOB release\sea-prep.blob --sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2
if %errorlevel% neq 0 (echo postject failed & exit /b 1)

echo.
echo Done! Output: %~dp0release\ggtb-broker.exe
