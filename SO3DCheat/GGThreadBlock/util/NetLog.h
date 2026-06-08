#pragma once
#include <Windows.h>
#include <cstdint>

namespace GGTB::NetLog
{
// Attaches Detours on Net__SendPacket_Plaintext (lowest game-plaintext send),
// Net__RawRecv (game-visible recv buffer), and ws2_32!send/recv. Both game
// call
// sites are in unpackd_so3d.exe — PatternResolver resolves them via fallback
// RVA (0x71CA60 / 0x71C750).
//
// Log files live under GGConfig/_bootstrap/net/{sendlog,recvlog} before
// UserConfig is ready (no character selected yet), then switch to
// GGConfig/<character>/net/{sendlog,recvlog} after the character is known.
// This keeps login/auth traffic visible without mixing it into character logs.
//
// On-disk format (append-only UTF-8 text, one frame per line):
//   YYYY-MM-DD HH:MM:SS.mmm | [SEND|RECV|WIRE_SEND|WIRE_RECV] <ip>:<port> len=<N> hex: AA BB CC ...
// ip/port come from getpeername() on the SOCKET stored at CGameClient+0x10.
// When peer info is unavailable (socket not connected yet) the field renders
// as "?:?". hex is the exact plaintext payload bytes:
//   SEND payload = {u32 totalLen, u32 protocolId, ...body} captured at the
//                  entry of Net__SendPacket_Plaintext, BEFORE the optional
//                  XOR obfuscation (sub_5CA910) rewrites the body in place.
//   RECV payload = the bytes j_recv just appended to the game's rolling
//                  buffer. We do not re-frame them — dispatchers re-parse
//                  them as {u16 size; u32 proto; ...body}; emitting raw
//                  chunks means partial reads / dropped frames stay visible.
void Install();
void Uninstall();

// True once both hooks have successfully attached. Diagnostic only.
bool IsActive();

// ---------- Skill-hit observer (for FireFullPower 稳健模式) ----------
//
// Returns GetTickCount() of the most recent proto 521056 (= GC_SKILL_CAST_RESULT,
// handler sub_895450) packet observed by the recv hook. 0 if never.
//
// 521056 broadcasts every successful skill cast (ours or anyone nearby): body
// layout starts with skillId at offset 8 (after 8-byte header). Server sends
// this back AFTER processing our skill packet, so seeing one means "the
// server is still alive and caught up to at least one of our prior sends".
//
// FireFullPower uses this to gate per-cast pacing: skip the next cast unless
// (now - lastSendMs >= 2000ms) OR (lastSkillResultMs > lastSendMs). That
// stops the disconnect-causing burst when the server falls behind.
//
// outSkillId (optional): if non-null, populated with the skillId field
// (body[0]) from the matching packet. 83 = throw bomb, etc.
DWORD GetLastSkillResultTickMs(uint32_t *outSkillId = nullptr);

// ---------- Auto-trade observers (for AutoTradeModule) ----------
//
// Returns GetTickCount() of the most recent GC trade packet observed by the
// recv hook. 0 if never. Mirrors GetLastSkillResultTickMs — atomics written in
// the recv hook (game net thread), polled cross-thread by the worker.
//
// 0x07CC62 (511074) = 收到交易请求。body[0] (offset 8) 是发起方 entity id,
//   AutoTrade 据此发 CG 412028 同意。outRequesterId(可选)填该 id。IDA 静态
//   确认:handler GC_OnTradeRequest_07CC62 读 body[0],忙时 412029 DECLINE,
//   否则 Trade_InitRequestWindow(state=0) 把 id 存到 ctrl+64 供 Trade_RequestPopup
//   回读。recvlog 实测此 proto 在他人发起交易时上线(len=12,body=requesterId)。
//   注:proto 不是 base+subIndex,注册时字面值才是线上 proto(详见 NetLog.cpp)。
// 0x07D050 (512080) = 对方锁定了交易。gate 本地 412035 确认 —— AutoTrade 先发
//   412033 锁定再发 412035 确认。
DWORD GetLastTradeRequestTickMs(uint32_t *outRequesterId = nullptr);
DWORD GetLastTradePeerLockTickMs();
} // namespace GGTB::NetLog
