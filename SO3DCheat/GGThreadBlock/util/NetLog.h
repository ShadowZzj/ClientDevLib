#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>

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

// ---------- Auto-trade observers (for AutoTradeController) ----------
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

// ---------- 洗发条结果观察 (ClockworkWasher) ----------
//
// 返回最近一次 proto 511644 (0x7CE9C, GC_RES_SPRING_OPT_ASSIGN) 的 GetTickCount(),0=从未。
// 可选回填 body: resultCode(0=成功)、grade(0=未洗,1=N,2=G,3=DG,4=XG,5=SG)、3 条属性 id/值。
// washClockwork 发 411590 后轮询本接口等 tick 变化来确认洗完成并取真实结果 —— 冷发不写回
// 物品内存,故只能靠这条回包。原子写在 recv hook(net 线程),worker 跨线程读。
DWORD GetLastSpringAssign(uint32_t *outResultCode = nullptr, uint32_t *outGrade = nullptr,
                          uint32_t outIds[3] = nullptr, uint32_t outVals[3] = nullptr);

// ---------- Auto-fishing bait observer (for AutoFishingModule) ----------
//
// 返回最近一次手动抛竿(send hook 看到的 411047/411174)里的 baitItemId,从未见到
// 则 0。让 AutoFishing 不必自己挂 Detour:复用本模块已装在 Net__SendPacket_Plaintext
// 上的 hook 作只读观察。原子写在 send hook(可能是游戏 net 线程或我们的 worker 线程),
// 由 worker / UI 跨线程读。
uint32_t GetLastFishingBaitId();

// ---------- Fishing-pose broadcast observer (for AutoFishingModule 破姿态时序) ----------
//
// 返回 recv hook 最近一次看到"针对某 actor 的钓鱼姿态广播(进入态)"的 GetTickCount(),
// 从未见到则 0。线上 proto 511058 = GC_OnEntityAction_07CC52(IDA 实证:
// Net__RegisterPacketHandlersB @ 0x8928c0 处 sub_875550(511058, handler) 字面注册,
// 非 base+subIndex 映射),其分发器 GC_DispatchActorAction case 3/13 = 钓鱼动作,把
// 目标 actor 摆成钓鱼 motion(27)。wire body 布局(帧 = {u32 len, u32 proto, body...},
// body 自 +8 起):body[0]@+8 = actorId,body[1]@+12 = 动作子码(3 或 13 = 钓鱼),
// 末尾一个 u32 = 姿态开关 flag(非 0 = 进入钓鱼姿态)。
//
// AutoFishing 抛竿后用它做时序门:抛竿那一刻起轮询本接口,等服务端把"我"广播成钓鱼
// (actorId == 本地玩家、flag != 0)之后再触发破姿态微移 —— 这样走路动作必然盖在钓鱼
// 广播之后,远端不会再被随后到来的钓鱼广播重新摆回钓鱼姿态。
//
// 只关心"是不是我",所以调用方传入自己的 actorId(*(localUser+408));命中才更新 tick。
// 原子写在 recv hook(游戏 net 线程),worker 跨线程读。
DWORD GetLastSelfFishingPoseTickMs(uint32_t selfActorId);

// ---------- 账号共享仓库整桶观察 (511320 SC_BANK_BULK) ----------
//
// 开仓库(CG 411154[1]+411644[self])后服务端回 proto 511320 整桶,recv hook 解析成
// 仓库物品快照。帧 = {u32 len, u32 proto, body...},body 自 +8 起:body+0 page、
// body+4 条目数,之后每条 24 字节:+0 唯一实例id(搬运时 411155 引用) / +8 模板 itemId /
// +12 packed count(可堆叠真实数量 = packed+1)。空条目(uid==0 || itemId==0)跳过。
// 511322(SC_BANK_UPDATE,搬出一格后回流)body+20 是该实例 uid,据此从快照里删掉,避免
// 补货状态机重复搬同一格。抓包+IDA 实证 2026-06-14(shadowdance)。
struct BankEntry
{
    uint32_t uid;    // 唯一实例 id (411155 搬运引用这个)
    uint32_t itemId; // 模板 itemId
    uint32_t count;  // 真实件数 (packed+1,按可堆叠口径)
};

// 取最近一次 511320 整桶解析出的仓库快照。返回该桶的 GetTickCount(),0=从未见到。
// 原子/互斥保护:recv hook(游戏 net 线程)写,broker 命令(worker 线程)读。
DWORD GetBankSnapshot(std::vector<BankEntry> &out);
} // namespace GGTB::NetLog
