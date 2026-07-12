"""so3dclient —— SO3D 在线登录客户端 / 批量登录器(模块化重构)。

分层(自下而上,import 只允许向下):
  runtime -> logio/protocol/crypto -> tables -> config -> packets -> netio
  -> farm.state -> farm.{fishing,vendor,cash,restock} -> session/selftest
  -> gui -> cli
"""
from __future__ import annotations

__all__ = ["cli"]
