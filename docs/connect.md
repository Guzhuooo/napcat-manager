# 外部客户端对接 NapCat（OneBot 11）

笔上的 NapCat 已开启 OneBot 11 服务，**HTTP 与 WebSocket 同端口**（3000）。
本文以真机实测为准（HTTP `get_login_info` 返回 200、WS 握手 101）。

## 连接参数

| 项 | 值 |
|---|---|
| 设备 IP | 以管理器 miniapp 显示为准（DHCP 可能变化，建议在路由器绑定） |
| HTTP API | `http://<设备IP>:3000` |
| WebSocket | `ws://<设备IP>:3000` |
| token | **空**（未设置；见下文安全提示） |
| 消息格式 | array（OneBot 11 标准数组格式） |

当前实测环境（2026-10-06）：

```
HTTP  http://192.168.0.102:3000/get_login_info  → 200
WS    ws://192.168.0.102:3000                    → 101 Switching Protocols
```

## 常见客户端的填法

### 反向 WebSocket（最常见，推荐）

适用于 NoneBot / Koishi / ZeroBot /云崽 等大多数框架——
即「你的客户端主动连到 NapCat」：

```
ws://192.168.0.102:3000
```

在你的框架里填入上面的地址作为「OneBot 反向 WS / NapCat 连接地址」即可。
连接成功后事件推送与 API 调用都走这一条连接。

### 正向 HTTP API

适用于只需「调用 API、不需要事件」的场景：

```
http://192.168.0.102:3000
```

调用示例（OneBot 11 标准）：

```bash
curl -X POST http://192.168.0.102:3000/get_login_info \
     -H "Content-Type: application/json" -d "{}"

curl -X POST http://192.168.0.102:3000/send_private_msg \
     -H "Content-Type: application/json" \
     -d '{"user_id": 123456, "message": [{"type": "text", "data": {"text": "hi"}}]}'
```

### HTTP 上报（NapCat 主动 POST 事件给你的服务端）

当前**未配置** `httpClients`。如果你的客户端是「HTTP 监听等上报」型，
两种解决方式任选：

1. 打开笔上的 WebUI（`http://<设备IP>:6099/webui`）→ 网络配置 →
   添加 HTTP 客户端，填你的服务端 URL；
2. 或告诉我 URL，我在管理器里加上该配置项。

## 快速自检（电脑上执行）

```bash
# API 通不通
curl -X POST http://192.168.0.102:3000/get_login_info -d "{}" -H "Content-Type: application/json"

# WS 通不通（返回 HTTP/1.1 101 即通过）
curl -i -N -H "Connection: Upgrade" -H "Upgrade: websocket" \
     -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
     -H "Sec-WebSocket-Version: 13" http://192.168.0.102:3000/
```

## 安全提示（建议做）

当前 token 为空，**局域网内任何设备都能控制这个 QQ**。建议：

1. 在 WebUI 或管理器里给 OneBot 设置 token；
2. 在路由器里给笔绑定静态 IP；
3. 不要把 3000 端口暴露到公网（笔在 NAT 后，默认不可达公网）。

设置 token 后，客户端连接需带 `Authorization: Bearer <token>`（HTTP）
或 `?access_token=<token>`（WS）。

## 故障排查

| 现象 | 排查 |
|---|---|
| 连接超时 | 笔与电脑是否同一网段；`ping 192.168.0.102`；IP 是否变了（看管理器） |
| 连接拒绝 | NapCat 是否在跑（管理器状态页 / `napcat-ctl.sh status`）；3000 是否 LISTEN |
| 连上即断 | 登录是否失效——看管理器「账号」是否显示昵称，掉了就重新扫码 |
| API 401/403 | token 不匹配 |
| IP 变了 | 路由器绑定静态 IP，或用管理器查看新 IP |
