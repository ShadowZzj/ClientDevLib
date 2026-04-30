#include "CUser.h"
#include "CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace SO3D
{

static std::string GbkToUtf8(const char *gbk)
{
    if (!gbk || !gbk[0])
        return {};
    int wLen = MultiByteToWideChar(936, 0, gbk, -1, nullptr, 0);
    if (wLen <= 0)
        return gbk;
    std::wstring wstr(wLen, 0);
    MultiByteToWideChar(936, 0, gbk, -1, wstr.data(), wLen);
    int u8Len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8Len <= 0)
        return gbk;
    std::string utf8(u8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, utf8.data(), u8Len, nullptr, nullptr);
    utf8.pop_back();
    return utf8;
}

std::string CUser::GetName()
{
    return GbkToUtf8(name);
}

static bool IsValidReadPtr(const void *p, size_t size)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0)
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                               PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & readable) != 0;
}

std::vector<AroundPlayer> GetAroundPlayers(const std::string &localPlayerName)
{
    std::vector<AroundPlayer> result;

    auto mgrAddr = PatternResolver::Get("EntityManager");
    if (!mgrAddr)
        return result;

    auto mgr = *reinterpret_cast<uintptr_t *>(mgrAddr);
    if (!mgr || !IsValidReadPtr(reinterpret_cast<void *>(mgr + 0x0C), 4))
        return result;

    auto cuserVft = PatternResolver::Get("CUserVftable");

    CUser *entity = *reinterpret_cast<CUser **>(mgr + 0x0C);

    int maxIter = 500;
    while (entity && maxIter-- > 0)
    {
        if (!IsValidReadPtr(entity, sizeof(uintptr_t)))
            break;

        uintptr_t vft = *reinterpret_cast<uintptr_t *>(entity);
        if (vft == cuserVft)
        {
            std::string name = entity->GetName();
            if (!name.empty() && name != localPlayerName)
            {
                AroundPlayer ap;
                ap.user = entity;
                ap.name = name;
                ap.dist = 0.f;
                auto *lp = GetLocalPlayer();
                if (lp)
                {
                    float dx = entity->x - lp->x;
                    float dy = entity->y - lp->y;
                    float dz = entity->z - lp->z;
                    ap.dist  = std::sqrtf(dx * dx + dy * dy + dz * dz);
                }
                result.push_back(ap);
            }
        }

        if (!IsValidReadPtr(reinterpret_cast<void *>(
                reinterpret_cast<uintptr_t>(entity) + offsetof(CUser, nextUser)), 4))
            break;
        entity = reinterpret_cast<CUser *>(entity->nextUser);
    }

    return result;
}

} // namespace SO3D
