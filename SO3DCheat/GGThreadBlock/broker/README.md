# GGTB Broker

将 GGThreadBlock.dll 各个游戏进程跟一个 web UI 串起来的中间层。

```
[Browser http://127.0.0.1:7321]
            │  HTTP + WS (/ws)
            ▼
   [ggtb-broker  (Node)]
            │  Named pipe \\.\pipe\GGTB_BROKER  (server)
            ▼
   [GGThreadBlock.dll × N]   每个游戏进程一份,DLL 主动连过来
```

## 启动

```bash
cd SO3DCheat/GGThreadBlock/broker
npm install
npm run dev      # ts-node, 改完代码自动重启
```

或编译后跑:

```bash
npm run build
npm start
```

启动成功会打:

```
[broker] pipe listening on \\.\pipe\GGTB_BROKER
[broker] http listening on http://127.0.0.1:7321
```

浏览器开 http://127.0.0.1:7321。

## 环境变量

| 变量 | 默认 | 含义 |
|---|---|---|
| `GGTB_BROKER_PIPE` | `GGTB_BROKER` | 命名管道名(无 `\\.\pipe\` 前缀)。DLL 侧同名变量。 |
| `GGTB_BROKER_PORT` | `7321`        | HTTP/WS 端口 |
| `GGTB_BROKER_HOST` | `127.0.0.1`   | 监听地址。改成 `0.0.0.0` 才能从其它机器访问。 |

## 协议

详见上层 plan 文件。简言之:

- 帧 = UTF-8 JSON 对象 + `\n`
- DLL → Broker: `hello` / `identity` / `status` / `ack` / `bye`
- Broker → DLL: `command` / `ping`
- 每个 `command` 带 uuid `id`, DLL 处理完回 `ack` 同 id

## REST API

- `GET /api/instances` → 当前在线实例列表
- `POST /api/command/:pid`  body `{action, args}`  → ack 结果

## 添加新命令

1. 在 DLL 侧 (`dllmain.cpp` 或新建文件) 加一行 `RemoteControl::RegisterCommandHandler("xxx", ...)`
2. 在 web `app.js` 里加一个表单 + `fetch POST /api/command/:pid` 调用

不需要改 broker — 它只做透传。
