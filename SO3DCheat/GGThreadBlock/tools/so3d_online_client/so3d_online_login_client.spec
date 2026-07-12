# -*- mode: python ; coding: utf-8 -*-
# 入口在交付目录根;static import so3dclient.cli 会把整个包(含 farm 子包、bridge_client)拉进来。
# 数据文件(item_names.json 等)不进 exe:运行期从 exe 同目录(APP_DIR)读取,由 build 脚本拷到 dist\。
from PyInstaller.utils.hooks import collect_submodules

hiddenimports = []
hiddenimports += collect_submodules('Crypto')
hiddenimports += collect_submodules('so3dclient')


a = Analysis(
    ['so3d_online_login_client.py'],
    pathex=['.'],
    binaries=[],
    datas=[],
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='so3d_online_login_client',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
