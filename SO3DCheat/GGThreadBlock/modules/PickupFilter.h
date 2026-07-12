#pragma once
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

namespace GGTB
{

// 自动拾取过滤 —— broker 远控的全局共享配置(所有角色共用一份),由 AutoPickupModule
// 的 worker 在每轮发拾取包前查询。配置只活在 broker 的 pickup_filter.json 里,DLL 每次
// 连上 broker(或配置改动)会被 push 一次 setPickupFilter;broker 不在线时默认 mode=all,
// 即不过滤、全捡(旧行为)。
//
// 单例 + mutex,无 worker:IO 线程经 SetConfig 写入,AutoPickup worker 经 ShouldPick 读。
// 列表只存 itemId(掉落物在 DLL 端只能稳定拿到 itemId);名字搜索/展示在 web 侧用
// item_names.json 完成,wire 上只传 itemId 数组。
class PickupFilter
{
  public:
    static PickupFilter &Instance()
    {
        static PickupFilter s_instance;
        return s_instance;
    }

    // broker 下发的共享配置。pickupExecutionMode 只决定由谁执行拾取；mode/itemIds
    // 仍只决定哪些物品允许拾取。
    void SetConfig(const nlohmann::json &j)
    {
        std::lock_guard<std::mutex> lk(mutex_);

        if (auto it = j.find("pickupExecutionMode");
            it != j.end() && it->is_string())
        {
            const auto mode = it->get<std::string>();
            if (mode == "autoPickup" || mode == "nativeGame")
                nativeGame_ = mode == "nativeGame";
        }

        if (auto it = j.find("mode"); it != j.end() && it->is_string())
            whitelist_ = (it->get<std::string>() == "whitelist");

        if (auto it = j.find("itemIds"); it != j.end() && it->is_array())
        {
            ids_.clear();
            for (const auto &e : *it)
            {
                uint32_t id = 0;
                if (e.is_number_unsigned())
                    id = e.get<uint32_t>();
                else if (e.is_number_integer())
                    id = static_cast<uint32_t>(e.get<int64_t>());
                else if (e.is_number_float())
                    id = static_cast<uint32_t>(e.get<double>());
                else if (e.is_string())
                {
                    try { id = static_cast<uint32_t>(std::stoul(e.get<std::string>())); }
                    catch (...) { id = 0; }
                }
                if (id != 0)
                    ids_.insert(id);
            }
        }

        spdlog::info("GGTB::PickupFilter: config updated execution={} mode={} ids={}",
                     nativeGame_ ? "nativeGame" : "autoPickup",
                     whitelist_ ? "whitelist" : "all", ids_.size());
    }

    // worker 每轮对每个掉落调一次。mode=all 全过;mode=whitelist 仅放行列表内 itemId。
    bool ShouldPick(uint32_t itemId) const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!whitelist_)
            return true;
        return ids_.find(itemId) != ids_.end();
    }

    bool   IsWhitelist() const { std::lock_guard<std::mutex> lk(mutex_); return whitelist_; }
    bool   UsesAutoPickup() const { std::lock_guard<std::mutex> lk(mutex_); return !nativeGame_; }
    size_t Count()       const { std::lock_guard<std::mutex> lk(mutex_); return ids_.size(); }

    // 运行时状态快照,给 broker 的 getPickupFilterStatus 用。
    nlohmann::json GetStatus() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        nlohmann::json j;
        j["pickupExecutionMode"] = nativeGame_ ? "nativeGame" : "autoPickup";
        j["mode"]                = whitelist_ ? "whitelist" : "all";
        j["count"]               = ids_.size();
        return j;
    }

  private:
    PickupFilter() = default;
    ~PickupFilter() = default;
    PickupFilter(const PickupFilter &)            = delete;
    PickupFilter &operator=(const PickupFilter &) = delete;

    mutable std::mutex          mutex_;
    bool                        nativeGame_ = false;
    bool                        whitelist_ = false; // false = 全部拾取(默认)
    std::unordered_set<uint32_t> ids_;
};

} // namespace GGTB
