#pragma once
#include <Windows.h>
#include <cstdint>

namespace GGTB::NetLog
{
// Attaches Detours on Net__SendPacket_Plaintext (lowest plaintext send) and
// Net__RawRecv (socket-level byte pump, wire is already plaintext). Both call
// sites are in unpackd_so3d.exe — PatternResolver resolves them via fallback
// RVA (0x71CA60 / 0x71C750).
//
// Log files live under GGConfig/<character>/net/{sendlog,recvlog}. Before
// UserConfig is ready (no character selected yet) the hooks silently buffer
// nothing — they just flush when the character-scoped log files exist. This is
// intentional: pre-login traffic is channel/auth crap that we don't want in
// the per-character log anyway.
//
// On-disk format (append-only UTF-8 text, one frame per line):
//   YYYY-MM-DD HH:MM:SS.mmm | [SEND|RECV] <ip>:<port> len=<N> hex: AA BB CC ...
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
} // namespace GGTB::NetLog
