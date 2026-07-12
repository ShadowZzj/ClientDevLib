#include "EnchantStoneControl.h"

#include "../entity/CLocalPlayer.h"
#include "NetLog.h"
#include "PatternResolver.h"
#include "RemoteControl.h"

#include <Windows.h>
#include <json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace GGTB::EnchantStoneControl
{
namespace
{
constexpr int       kColorCount = 4;
constexpr int       kSlotCount  = 12;
constexpr uintptr_t kStoneArrayOffset = 12476; // CUser+0x30BC
constexpr size_t    kStoneStride      = 112;

constexpr uint32_t kProtoOptionChange = 411573;
constexpr uint32_t kProtoUpgrade      = 411574;
constexpr uint32_t kProtoExtendCount  = 411575;
constexpr uint32_t kAnyResult         = 0xFFFFFFFFu;

constexpr uint32_t kFallbackFixedCouponId      = 26421;
constexpr uint32_t kFallbackEventFixedCouponId = 30370;
constexpr uint32_t kFallbackRechargeTicketId   = 26422;
constexpr uint32_t kShiningCrystalItemId       = 26322;
constexpr uint32_t kRainbowPowderItemId        = 26323;

constexpr int kUpgradeSuccessPercent[kSlotCount] = {
    100, 100, 80, 80, 80, 60, 50, 50, 30, 30, 20, 10,
};

const char *kColorNames[kColorCount] = {
    u8"紅色", u8"橙色", u8"黃色", u8"綠色",
};

struct StoneState
{
    uint32_t color;
    int32_t  grade;
    uint32_t attrIds[kSlotCount];
    int32_t  values[kSlotCount];
    uint32_t remaining;
    uint32_t max;
};

struct UpgradeRequirement
{
    uint32_t crystalItemId;
    uint32_t crystalCount;
    uint32_t powderItemId;
    uint32_t powderCount;
    uint32_t successBasisPoints;
    uint32_t optionPool;
    uint64_t money;
};

struct ChangeRequirement
{
    uint32_t crystalItemId;
    uint32_t crystalCount;
    uint64_t money;
};

struct ItemResource
{
    uint32_t itemId = 0;
    uint32_t count = 0;         // total across matching slots
    uint32_t selectedCount = 0; // count in the wire slot sent to the server
    int      slotIndex = -1;
    uint32_t wire = 0;
};

struct WaitResult
{
    bool       confirmed = false;
    uint64_t   sequence = 0;
    uint32_t   resultCode = 0;
    StoneState after{};
};

using LookupRequirementFn = void *(__thiscall *)(void *proxy);
using ItemConfigGetInstanceFn = void *(__cdecl *)();
using IsMaintainCouponFn = char(__thiscall *)(void *instance, int itemId);
using GetRechargeTicketIdFn = int(__thiscall *)(void *instance);
using NetBeginSendFn = void *(__cdecl *)();
using NetSendPlaintextFn = int(__thiscall *)(void *client, void *packet, int length);

bool ReadAllStonesOnce(StoneState out[kColorCount])
{
    uintptr_t user = GetLocalUserPtr();
    if (!user)
        return false;

    __try
    {
        for (int color = 0; color < kColorCount; ++color)
        {
            const uint8_t *src = reinterpret_cast<const uint8_t *>(
                user + kStoneArrayOffset + color * kStoneStride);
            std::memcpy(&out[color].color, src, 4);
            std::memcpy(&out[color].grade, src + 4, 4);
            std::memcpy(out[color].attrIds, src + 8, 48);
            std::memcpy(out[color].values, src + 56, 48);
            std::memcpy(&out[color].remaining, src + 104, 4);
            std::memcpy(&out[color].max, src + 108, 4);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    for (int color = 0; color < kColorCount; ++color)
    {
        if (out[color].color != static_cast<uint32_t>(color) ||
            out[color].grade < -1 || out[color].grade >= kSlotCount ||
            out[color].remaining > 100000 || out[color].max > 100000)
            return false;
    }
    return true;
}

bool ReadAllStones(StoneState out[kColorCount])
{
    // The game applies a 511615 snapshot one colour record at a time.  Require
    // two identical reads so the broker never makes a decision from a torn
    // four-colour view while that handler is running on another thread.
    StoneState first[kColorCount]{};
    StoneState second[kColorCount]{};
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (!ReadAllStonesOnce(first))
            return false;
        Sleep(1);
        if (!ReadAllStonesOnce(second))
            return false;
        if (std::memcmp(first, second, sizeof(first)) == 0)
        {
            std::memcpy(out, second, sizeof(second));
            return true;
        }
    }
    return false;
}

bool ReadStone(int color, StoneState *out)
{
    if (!out || color < 0 || color >= kColorCount)
        return false;
    StoneState all[kColorCount]{};
    if (!ReadAllStones(all))
        return false;
    *out = all[color];
    return true;
}

bool SameStone(const StoneState &a, const StoneState &b)
{
    return std::memcmp(&a, &b, sizeof(StoneState)) == 0;
}

void CopyWireRecord(const NetLog::EnchantStoneRecord &src, StoneState *dst)
{
    dst->color = src.color;
    dst->grade = src.grade;
    std::memcpy(dst->attrIds, src.attrIds, sizeof(dst->attrIds));
    std::memcpy(dst->values, src.values, sizeof(dst->values));
    dst->remaining = src.remaining;
    dst->max = src.max;
}

int CallUpgradeLookupSEH(LookupRequirementFn fn, int color, UpgradeRequirement *out)
{
    uint32_t proxy[27]{};
    proxy[26] = static_cast<uint32_t>(color);
    __try
    {
        const uint32_t *row = static_cast<const uint32_t *>(fn(proxy));
        if (!row)
            return 0;
        out->crystalItemId = row[4];
        out->crystalCount = row[5];
        out->powderItemId = row[6];
        out->powderCount = row[7];
        out->successBasisPoints = row[8];
        out->optionPool = row[9];
        out->money = row[10];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

int CallChangeLookupSEH(LookupRequirementFn fn, int color, ChangeRequirement *out)
{
    uint32_t proxy[27]{};
    proxy[26] = static_cast<uint32_t>(color);
    __try
    {
        const uint32_t *row = static_cast<const uint32_t *>(fn(proxy));
        if (!row)
            return 0;
        out->crystalItemId = row[4];
        out->crystalCount = row[5];
        out->money = row[6];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

bool GetUpgradeRequirement(int color, UpgradeRequirement *out)
{
    auto addr = PatternResolver::Get("StoneEnchantLookupUpgradeRequirement");
    if (!addr || !out)
        return false;
    return CallUpgradeLookupSEH(reinterpret_cast<LookupRequirementFn>(addr),
                                color, out) == 1;
}

bool GetChangeRequirement(int color, ChangeRequirement *out)
{
    auto addr = PatternResolver::Get("StoneEnchantLookupChangeRequirement");
    if (!addr || !out)
        return false;
    return CallChangeLookupSEH(reinterpret_cast<LookupRequirementFn>(addr),
                               color, out) == 1;
}

bool CallIsMaintainCouponSEH(ItemConfigGetInstanceFn getInstance,
                             IsMaintainCouponFn predicate, uint32_t itemId)
{
    __try
    {
        void *instance = getInstance();
        return instance && predicate(instance, static_cast<int>(itemId)) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

uint32_t CallGetRechargeTicketIdSEH(ItemConfigGetInstanceFn getInstance,
                                    GetRechargeTicketIdFn fn)
{
    __try
    {
        void *instance = getInstance();
        return instance ? static_cast<uint32_t>(fn(instance)) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool IsMaintainCoupon(uint32_t itemId)
{
    auto instanceAddr = PatternResolver::Get("StoneItemConfigGetInstance");
    auto predicateAddr = PatternResolver::Get("StoneEnchantIsMaintainCouponItem");
    if (instanceAddr && predicateAddr &&
        CallIsMaintainCouponSEH(
            reinterpret_cast<ItemConfigGetInstanceFn>(instanceAddr),
            reinterpret_cast<IsMaintainCouponFn>(predicateAddr), itemId))
        return true;
    return itemId == kFallbackFixedCouponId ||
           itemId == kFallbackEventFixedCouponId;
}

uint32_t GetRechargeTicketItemId()
{
    auto instanceAddr = PatternResolver::Get("StoneItemConfigGetInstance");
    auto addr = PatternResolver::Get("StoneEnchantGetRechargeTicketItemId");
    if (instanceAddr && addr)
    {
        uint32_t id = CallGetRechargeTicketIdSEH(
            reinterpret_cast<ItemConfigGetInstanceFn>(instanceAddr),
            reinterpret_cast<GetRechargeTicketIdFn>(addr));
        if (id)
            return id;
    }
    return kFallbackRechargeTicketId;
}

ItemResource FindNormalItem(uint32_t itemId, uint32_t required)
{
    ItemResource result;
    result.itemId = itemId;
    uint64_t total = 0;
    int firstSlot = -1;
    int enoughSlot = -1;
    const auto items = GetBagItems();
    for (const auto &item : items)
    {
        if (item.itemId != itemId || item.count == 0)
            continue;
        total += item.count;
        if (firstSlot < 0)
            firstSlot = static_cast<int>(item.slotIndex);
        if (enoughSlot < 0 && item.count >= required)
            enoughSlot = static_cast<int>(item.slotIndex);
    }
    result.count = static_cast<uint32_t>(
        std::min<uint64_t>(total, (std::numeric_limits<uint32_t>::max)()));
    result.slotIndex = enoughSlot >= 0 ? enoughSlot : firstSlot;
    if (result.slotIndex >= 0)
    {
        for (const auto &item : items)
        {
            if (static_cast<int>(item.slotIndex) != result.slotIndex ||
                item.itemId != itemId)
                continue;
            result.selectedCount = item.count;
            result.wire = item.bagId;
            break;
        }
    }
    return result;
}

ItemResource FindCashItem(uint32_t exactItemId, bool maintainCoupon)
{
    ItemResource result;
    uint64_t total = 0;
    for (const auto &item : GetCashBagItems())
    {
        bool match = maintainCoupon ? IsMaintainCoupon(item.itemId)
                                    : item.itemId == exactItemId;
        if (!match || item.count == 0)
            continue;
        if (result.slotIndex < 0)
        {
            result.itemId = item.itemId;
            result.slotIndex = static_cast<int>(item.slotIndex);
            result.wire = item.slotIndex;
            result.selectedCount = item.count;
        }
        total += item.count;
    }
    result.count = static_cast<uint32_t>(
        std::min<uint64_t>(total, (std::numeric_limits<uint32_t>::max)()));
    return result;
}

int CallSendPacketSEH(NetBeginSendFn begin, NetSendPlaintextFn send,
                      uint32_t proto, const int32_t *body, int bodyCount)
{
    __try
    {
        void *client = begin();
        if (!client || bodyCount < 1 || bodyCount > 3)
            return -1;
        uint32_t packet[64]{};
        const int length = 8 + bodyCount * 4;
        packet[0] = static_cast<uint32_t>(length);
        packet[1] = proto;
        for (int i = 0; i < bodyCount; ++i)
            packet[2 + i] = static_cast<uint32_t>(body[i]);
        return send(client, packet, length);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

bool SendPacket(uint32_t proto, const int32_t *body, int bodyCount)
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr = PatternResolver::Get("NetSendPacketPlaintext");
    if (!beginAddr || !sendAddr)
        return false;
    int rv = CallSendPacketSEH(reinterpret_cast<NetBeginSendFn>(beginAddr),
                               reinterpret_cast<NetSendPlaintextFn>(sendAddr),
                               proto, body, bodyCount);
    if (rv < 0)
        return false;
    spdlog::info("GGTB::EnchantStone: sent proto={} bodyCount={} rv={}",
                 proto, bodyCount, rv);
    return true;
}

WaitResult WaitForUpdate(int color, const StoneState &before,
                         uint64_t previousSequence, uint32_t expectedResult,
                         uint32_t alternateResult,
                         int waitMs)
{
    WaitResult result;
    result.after = before;
    DWORD start = GetTickCount();
    do
    {
        NetLog::EnchantStoneSnapshot snapshot{};
        uint64_t sequence = NetLog::GetLastEnchantStoneSnapshot(&snapshot);
        if (sequence != 0 && sequence != previousSequence &&
            (expectedResult == kAnyResult ||
             snapshot.metadata[0] == expectedResult ||
             snapshot.metadata[0] == alternateResult))
        {
            const auto &record = snapshot.stones[color];
            if (record.color == static_cast<uint32_t>(color) &&
                record.grade >= -1 && record.grade < kSlotCount)
            {
                CopyWireRecord(record, &result.after);
                if (expectedResult == kAnyResult &&
                    !(result.after.grade == before.grade &&
                      result.after.max == before.max &&
                      before.remaining > 0 &&
                      result.after.remaining + 1 == before.remaining))
                {
                    Sleep(15);
                    continue;
                }
                result.confirmed = true;
                result.sequence = sequence;
                result.resultCode = snapshot.metadata[0];
                return result;
            }
        }

        StoneState live{};
        if (ReadStone(color, &live) && !SameStone(before, live))
        {
            result.after = live;
            result.confirmed = true;
            result.sequence = sequence;
            return result;
        }
        if (waitMs <= 0)
            break;
        Sleep(15);
    } while (static_cast<DWORD>(GetTickCount() - start) <
             static_cast<DWORD>(waitMs));

    ReadStone(color, &result.after);
    result.sequence = NetLog::GetLastEnchantStoneSnapshot();
    return result;
}

const char *AttrName(uint32_t id)
{
    switch (id)
    {
        case 1: return u8"攻擊力";
        case 2: return u8"魔法力";
        case 3: return u8"防禦力";
        case 4: return u8"命中率";
        case 5: return u8"迴避率";
        case 6: return "AP(%)";
        case 7: return u8"攻擊速度";
        case 8: return u8"移動速度";
        case 9: return u8"必殺技";
        case 10: return u8"增加傷害";
        case 11: return u8"減少傷害";
        case 12: return "HP(%)";
        case 13: return u8"[副本]增加傷害";
        default: return "";
    }
}

bool AttrIsPercent(uint32_t id)
{
    return id == 6 || (id >= 10 && id <= 13);
}

nlohmann::json StoneJson(const StoneState &state)
{
    nlohmann::json slots = nlohmann::json::array();
    for (int i = 0; i < kSlotCount; ++i)
    {
        uint32_t id = state.attrIds[i];
        slots.push_back({
            {"index", i},
            {"opened", i <= state.grade},
            {"attrId", id},
            {"value", state.values[i]},
            {"name", AttrName(id)},
            {"percent", AttrIsPercent(id)},
            {"specialSlot", (i + 1) % 3 == 0},
        });
    }
    int openCount = state.grade < 0 ? 0 : state.grade + 1;
    return {
        {"color", state.color},
        {"colorName", kColorNames[state.color]},
        {"grade", state.grade},
        {"topIndex", state.grade},
        {"openCount", openCount},
        {"remaining", state.remaining},
        {"max", state.max},
        {"slots", std::move(slots)},
    };
}

nlohmann::json ItemResourceJson(const ItemResource &resource, uint32_t required)
{
    return {
        {"itemId", resource.itemId},
        {"owned", resource.count},
        {"selectedCount", resource.selectedCount},
        {"required", required},
        {"slotIndex", resource.slotIndex},
        {"wire", resource.wire},
        {"enough", resource.selectedCount >= required && resource.slotIndex >= 0},
    };
}

nlohmann::json ItemInventoryJson(const ItemResource &resource)
{
    return {
        {"itemId", resource.itemId},
        {"owned", resource.count},
        {"selectedCount", resource.selectedCount},
        {"slotIndex", resource.slotIndex},
        {"wire", resource.wire},
    };
}

nlohmann::json BuildQueryJson(const StoneState states[kColorCount])
{
    int64_t money = GetLocalMoney();
    ItemResource fixedCoupon = FindCashItem(0, true);
    uint32_t rechargeId = GetRechargeTicketItemId();
    ItemResource rechargeTicket = FindCashItem(rechargeId, false);
    ItemResource shiningCrystal = FindNormalItem(kShiningCrystalItemId, 1);
    ItemResource rainbowPowder = FindNormalItem(kRainbowPowderItemId, 1);

    nlohmann::json stones = nlohmann::json::array();
    nlohmann::json requirements = nlohmann::json::array();
    for (int color = 0; color < kColorCount; ++color)
    {
        nlohmann::json stone = StoneJson(states[color]);

        UpgradeRequirement upgrade{};
        bool upgradeResolved = states[color].grade < kSlotCount - 1 &&
                               GetUpgradeRequirement(color, &upgrade);
        nlohmann::json upgradeJson = {
            {"resolved", upgradeResolved},
            {"nextGrade", states[color].grade + 2},
        };
        if (upgradeResolved)
        {
            ItemResource crystal = FindNormalItem(upgrade.crystalItemId,
                                                   upgrade.crystalCount);
            ItemResource powder = FindNormalItem(upgrade.powderItemId,
                                                  upgrade.powderCount);
            upgradeJson["successBasisPoints"] = upgrade.successBasisPoints;
            upgradeJson["successPercent"] = upgrade.successBasisPoints / 100.0;
            upgradeJson["optionPool"] = upgrade.optionPool;
            upgradeJson["moneyRequired"] = upgrade.money;
            upgradeJson["moneyEnough"] = money >= 0 &&
                static_cast<uint64_t>(money) >= upgrade.money;
            upgradeJson["crystal"] = ItemResourceJson(crystal,
                                                       upgrade.crystalCount);
            upgradeJson["rainbowPowder"] = ItemResourceJson(powder,
                                                             upgrade.powderCount);
        }
        stone["upgrade"] = std::move(upgradeJson);

        ChangeRequirement change{};
        bool changeResolved = states[color].grade >= 0 &&
                              GetChangeRequirement(color, &change);
        nlohmann::json changeJson = {{"resolved", changeResolved}};
        if (changeResolved)
        {
            ItemResource crystal = FindNormalItem(change.crystalItemId,
                                                   change.crystalCount);
            changeJson["moneyRequired"] = change.money;
            changeJson["moneyEnough"] = money >= 0 &&
                static_cast<uint64_t>(money) >= change.money;
            changeJson["crystal"] = ItemResourceJson(crystal,
                                                      change.crystalCount);
        }
        stone["change"] = std::move(changeJson);
        requirements.push_back({
            {"color", color},
            {"upgrade", stone["upgrade"]},
            {"change", stone["change"]},
        });
        stones.push_back(std::move(stone));
    }

    nlohmann::json fixedCouponJson = ItemResourceJson(fixedCoupon, 1);
    nlohmann::json rechargeTicketJson = ItemResourceJson(rechargeTicket, 1);
    nlohmann::json shiningCrystalJson = ItemInventoryJson(shiningCrystal);
    nlohmann::json rainbowPowderJson = ItemInventoryJson(rainbowPowder);
    nlohmann::json resources = {
        {"money", money},
        {"fixedCoupon", fixedCouponJson},
        {"rechargeTicket", rechargeTicketJson},
        {"shiningCrystal", shiningCrystalJson},
        {"rainbowPowder", rainbowPowderJson},
        {"requirements", std::move(requirements)},
    };
    return {
        {"money", money},
        {"fixedCoupon", std::move(fixedCouponJson)},
        {"rechargeTicket", std::move(rechargeTicketJson)},
        {"shiningCrystal", std::move(shiningCrystalJson)},
        {"rainbowPowder", std::move(rainbowPowderJson)},
        {"resources", std::move(resources)},
        {"stones", std::move(stones)},
    };
}

nlohmann::json AttrDescriptor(uint32_t id, bool special)
{
    return {
        {"id", id},
        {"name", AttrName(id)},
        {"percent", AttrIsPercent(id)},
        {"special", special},
    };
}

nlohmann::json BuildAttrTableJson()
{
    nlohmann::json colors = nlohmann::json::array();
    const uint32_t normalByColor[kColorCount] = {7, 8, 9, 9};
    const uint32_t specialByColor[kColorCount] = {10, 11, 12, 13};
    for (int color = 0; color < kColorCount; ++color)
    {
        nlohmann::json normal = nlohmann::json::array();
        for (uint32_t id = 1; id <= 6; ++id)
            normal.push_back(AttrDescriptor(id, false));
        normal.push_back(AttrDescriptor(normalByColor[color], false));
        colors.push_back({
            {"color", color},
            {"name", kColorNames[color]},
            {"normalAttrs", std::move(normal)},
            {"specialAttr", AttrDescriptor(specialByColor[color], true)},
        });
    }

    nlohmann::json rates = nlohmann::json::array();
    for (int i = 0; i < kSlotCount; ++i)
        rates.push_back(kUpgradeSuccessPercent[i]);
    return {
        {"colors", std::move(colors)},
        {"specialSlots", {2, 5, 8, 11}},
        {"upgradeSuccessPercent", std::move(rates)},
    };
}

RemoteControl::CmdResult Error(const char *code, const std::string &message,
                               const nlohmann::json &extra = nlohmann::json::object())
{
    nlohmann::json detail = extra;
    detail["code"] = code;
    detail["message"] = message;
    return {false, detail.dump()};
}

int ClampWaitMs(const nlohmann::json &args)
{
    int waitMs = args.value("waitMs", 2500);
    if (waitMs < 0) waitMs = 0;
    if (waitMs > 5000) waitMs = 5000;
    return waitMs;
}

RemoteControl::CmdResult Query()
{
    StoneState states[kColorCount]{};
    if (!ReadAllStones(states))
        return Error("stone-state-unavailable", "local enchant-stone state is unavailable");
    return {true, BuildQueryJson(states).dump()};
}

RemoteControl::CmdResult OptionChange(const nlohmann::json &args)
{
    int color = args.value("color", -1);
    bool useFixed = args.value("useFixed", false);
    if (color < 0 || color >= kColorCount)
        return Error("invalid-color", "color must be 0..3");

    StoneState before{};
    if (!ReadStone(color, &before))
        return Error("stone-state-unavailable", "local enchant-stone state is unavailable");
    if (before.grade < 0)
        return Error("stone-unopened", "upgrade the first slot before changing options");
    if (before.remaining == 0)
        return Error("no-change-count", "stone option-change count is zero");

    ChangeRequirement requirement{};
    if (!GetChangeRequirement(color, &requirement))
        return Error("requirement-unavailable", "live table34 requirement is unavailable");
    ItemResource crystal = FindNormalItem(requirement.crystalItemId,
                                           requirement.crystalCount);
    if (crystal.slotIndex < 0 ||
        crystal.selectedCount < requirement.crystalCount)
        return Error("insufficient-change-material", "not enough shining crystals",
                     {{"crystal", ItemResourceJson(crystal, requirement.crystalCount)}});

    int64_t money = GetLocalMoney();
    if (money < 0 || static_cast<uint64_t>(money) < requirement.money)
        return Error("insufficient-money", "not enough money",
                     {{"owned", money}, {"required", requirement.money}});

    int fixedSlot = -1;
    if (useFixed)
    {
        ItemResource fixedCoupon = FindCashItem(0, true);
        if (fixedCoupon.slotIndex < 0 || fixedCoupon.selectedCount == 0)
            return Error("no-fixed-coupon", "stone maintain/fixed coupon is unavailable");
        fixedSlot = fixedCoupon.slotIndex;
    }

    StoneState current{};
    if (!ReadStone(color, &current) || !SameStone(before, current))
        return Error("state-changed-requery",
                     "stone state changed during preflight; query again before sending");
    crystal = FindNormalItem(requirement.crystalItemId, requirement.crystalCount);
    if (crystal.slotIndex < 0 ||
        crystal.selectedCount < requirement.crystalCount)
        return Error("insufficient-change-material", "not enough shining crystals",
                     {{"crystal", ItemResourceJson(crystal, requirement.crystalCount)}});
    money = GetLocalMoney();
    if (money < 0 || static_cast<uint64_t>(money) < requirement.money)
        return Error("insufficient-money", "not enough money",
                     {{"owned", money}, {"required", requirement.money}});
    if (useFixed)
    {
        ItemResource fixedCoupon = FindCashItem(0, true);
        if (fixedCoupon.slotIndex < 0 || fixedCoupon.selectedCount == 0)
            return Error("no-fixed-coupon", "stone maintain/fixed coupon is unavailable");
        fixedSlot = fixedCoupon.slotIndex;
    }

    uint64_t previousSequence = NetLog::GetLastEnchantStoneSnapshot();
    int32_t body[3] = {color, static_cast<int32_t>(crystal.wire), fixedSlot};
    if (!SendPacket(kProtoOptionChange, body, 3))
        return Error("send-failed", "failed to send option-change packet");

    // OptionChange has no dedicated result-code branch. Accept a new complete
    // 511615 only when its authoritative remaining-count delta matches.
    WaitResult wait = WaitForUpdate(color, before, previousSequence,
                                    kAnyResult, kAnyResult,
                                    ClampWaitMs(args));
    bool applied = wait.after.grade == before.grade &&
                   wait.after.max == before.max &&
                   before.remaining > 0 &&
                   wait.after.remaining + 1 == before.remaining;
    bool maintained = !useFixed ||
        (before.grade >= 0 &&
         wait.after.attrIds[before.grade] == before.attrIds[before.grade]);
    nlohmann::json detail = {
        {"confirmed", wait.confirmed},
        {"applied", applied},
        {"maintained", maintained},
        {"outcomeValid", wait.confirmed && applied && maintained},
        {"changed", !SameStone(before, wait.after)},
        {"usedFixedCoupon", useFixed},
        {"sequence", wait.sequence},
        {"resultCode", wait.resultCode},
        {"before", StoneJson(before)},
        {"after", StoneJson(wait.after)},
    };
    return {true, detail.dump()};
}

RemoteControl::CmdResult Upgrade(const nlohmann::json &args)
{
    int color = args.value("color", -1);
    if (color < 0 || color >= kColorCount)
        return Error("invalid-color", "color must be 0..3");

    StoneState before{};
    if (!ReadStone(color, &before))
        return Error("stone-state-unavailable", "local enchant-stone state is unavailable");
    if (before.grade >= kSlotCount - 1)
        return Error("stone-max-grade", "all 12 enchant-stone slots are already open");

    UpgradeRequirement requirement{};
    if (!GetUpgradeRequirement(color, &requirement))
        return Error("requirement-unavailable", "live table33 requirement is unavailable");
    ItemResource crystal = FindNormalItem(requirement.crystalItemId,
                                           requirement.crystalCount);
    ItemResource powder = FindNormalItem(requirement.powderItemId,
                                          requirement.powderCount);
    if (crystal.slotIndex < 0 ||
        crystal.selectedCount < requirement.crystalCount)
        return Error("insufficient-upgrade-crystal", "not enough shining crystals",
                     {{"crystal", ItemResourceJson(crystal, requirement.crystalCount)}});
    if (powder.slotIndex < 0 ||
        powder.selectedCount < requirement.powderCount)
        return Error("insufficient-rainbow-powder", "not enough rainbow powder",
                     {{"rainbowPowder", ItemResourceJson(powder, requirement.powderCount)}});

    int64_t money = GetLocalMoney();
    if (money < 0 || static_cast<uint64_t>(money) < requirement.money)
        return Error("insufficient-money", "not enough money",
                     {{"owned", money}, {"required", requirement.money}});

    StoneState current{};
    if (!ReadStone(color, &current) || !SameStone(before, current))
        return Error("state-changed-requery",
                     "stone state changed during preflight; query again before sending");
    crystal = FindNormalItem(requirement.crystalItemId, requirement.crystalCount);
    powder = FindNormalItem(requirement.powderItemId, requirement.powderCount);
    if (crystal.slotIndex < 0 ||
        crystal.selectedCount < requirement.crystalCount)
        return Error("insufficient-upgrade-crystal", "not enough shining crystals",
                     {{"crystal", ItemResourceJson(crystal, requirement.crystalCount)}});
    if (powder.slotIndex < 0 ||
        powder.selectedCount < requirement.powderCount)
        return Error("insufficient-rainbow-powder", "not enough rainbow powder",
                     {{"rainbowPowder", ItemResourceJson(powder, requirement.powderCount)}});
    money = GetLocalMoney();
    if (money < 0 || static_cast<uint64_t>(money) < requirement.money)
        return Error("insufficient-money", "not enough money",
                     {{"owned", money}, {"required", requirement.money}});

    uint64_t previousSequence = NetLog::GetLastEnchantStoneSnapshot();
    int32_t body[3] = {color, static_cast<int32_t>(crystal.wire),
                       static_cast<int32_t>(powder.wire)};
    if (!SendPacket(kProtoUpgrade, body, 3))
        return Error("send-failed", "failed to send upgrade packet");

    // 24 = upgrade success; 17 = probabilistic upgrade failure.  Both are
    // explicit server outcomes; actual state consistency is checked below.
    WaitResult wait = WaitForUpdate(color, before, previousSequence, 24, 17,
                                    ClampWaitMs(args));
    bool opened = wait.after.grade == before.grade + 1 &&
                  wait.after.grade >= 0 &&
                  wait.after.attrIds[wait.after.grade] != 0 &&
                  wait.after.values[wait.after.grade] != -1;
    bool failed = wait.resultCode == 17 &&
                  wait.after.grade == before.grade;
    bool outcomeValid = wait.confirmed &&
        ((opened && (wait.resultCode == 0 || wait.resultCode == 24)) || failed);
    nlohmann::json detail = {
        {"confirmed", wait.confirmed},
        {"opened", opened},
        {"failed", failed},
        {"outcomeValid", outcomeValid},
        {"sequence", wait.sequence},
        {"resultCode", wait.resultCode},
        {"successBasisPoints", requirement.successBasisPoints},
        {"before", StoneJson(before)},
        {"after", StoneJson(wait.after)},
    };
    return {true, detail.dump()};
}

RemoteControl::CmdResult ExtendCount(const nlohmann::json &args)
{
    int color = args.value("color", -1);
    if (color < 0 || color >= kColorCount)
        return Error("invalid-color", "color must be 0..3");

    StoneState before{};
    if (!ReadStone(color, &before))
        return Error("stone-state-unavailable", "local enchant-stone state is unavailable");
    if (before.remaining > before.max || before.remaining + 50 > before.max)
        return Error("max-recharge", "adding 50 would exceed the stone change-count cap",
                     {{"remaining", before.remaining}, {"max", before.max}});

    uint32_t ticketId = GetRechargeTicketItemId();
    ItemResource ticket = FindCashItem(ticketId, false);
    if (ticket.slotIndex < 0 || ticket.selectedCount == 0)
        return Error("no-recharge-ticket", "stone option-change recharge ticket is unavailable");

    StoneState current{};
    if (!ReadStone(color, &current) || !SameStone(before, current))
        return Error("state-changed-requery",
                     "stone state changed during preflight; query again before sending");
    ticket = FindCashItem(ticketId, false);
    if (ticket.slotIndex < 0 || ticket.selectedCount == 0)
        return Error("no-recharge-ticket", "stone option-change recharge ticket is unavailable");

    uint64_t previousSequence = NetLog::GetLastEnchantStoneSnapshot();
    int32_t wire = ((ticket.slotIndex + 13) & 0x7FFF) | 0x8000;
    int32_t body[2] = {color, wire};
    if (!SendPacket(kProtoExtendCount, body, 2))
        return Error("send-failed", "failed to send count-extend packet");

    WaitResult wait = WaitForUpdate(color, before, previousSequence, 25, 25,
                                    ClampWaitMs(args));
    bool extended = wait.after.grade == before.grade &&
                    wait.after.max == before.max &&
                    wait.after.remaining == before.remaining + 50;
    nlohmann::json detail = {
        {"confirmed", wait.confirmed},
        {"extended", extended},
        {"outcomeValid", wait.confirmed && extended &&
                             (wait.resultCode == 0 || wait.resultCode == 25)},
        {"sequence", wait.sequence},
        {"resultCode", wait.resultCode},
        {"before", StoneJson(before)},
        {"after", StoneJson(wait.after)},
    };
    return {true, detail.dump()};
}
} // namespace

void RegisterHandlers()
{
    RemoteControl::RegisterCommandHandler(
        "queryEnchantStones",
        [](const nlohmann::json &) -> RemoteControl::CmdResult { return Query(); });
    RemoteControl::RegisterCommandHandler(
        "getEnchantStoneAttrTable",
        [](const nlohmann::json &) -> RemoteControl::CmdResult {
            return {true, BuildAttrTableJson().dump()};
        });
    RemoteControl::RegisterCommandHandler(
        "enchantStoneOptionChange",
        [](const nlohmann::json &args) -> RemoteControl::CmdResult {
            return OptionChange(args);
        });
    RemoteControl::RegisterCommandHandler(
        "enchantStoneUpgrade",
        [](const nlohmann::json &args) -> RemoteControl::CmdResult {
            return Upgrade(args);
        });
    RemoteControl::RegisterCommandHandler(
        "enchantStoneExtend",
        [](const nlohmann::json &args) -> RemoteControl::CmdResult {
            return ExtendCount(args);
        });
}
} // namespace GGTB::EnchantStoneControl
