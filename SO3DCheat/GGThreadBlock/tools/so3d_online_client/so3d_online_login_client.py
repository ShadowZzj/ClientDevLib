#!/usr/bin/env python3
"""PyInstaller / 直接运行入口。保留 exe 名 so3d_online_login_client。

实际逻辑都在 so3dclient 包里;这里只把交付目录加进 sys.path 再调 main()。
数据文件(item_names.json 等)与本脚本同级,运行期由 so3dclient.runtime.APP_DIR 定位。
"""
from __future__ import annotations

import os
import sys

# 直接运行时脚本目录已是 sys.path[0];显式补一次以兼容部分启动方式。
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from so3dclient.cli import main

if __name__ == "__main__":
    main()
