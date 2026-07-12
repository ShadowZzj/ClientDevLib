#pragma once
#include <atomic>
#include <json.hpp>
#include <string>

namespace GGTB
{

// 右侧标签页归属。Common=常用(不破坏平衡的工具),Teleport=传送(回城/瞬移类),
// OP=变态(破坏平衡的作弊),PK=对战(暂空),Monster=怪物(只读内存的周围怪物列表)。
// 默认 Common —— 新模块默认进常用,不会误塞进变态页。
enum class Tab
{
    Common,
    Teleport,
    OP,
    PK,
    Monster,
};

class IModule
{
  public:
    IModule(const std::string &name) : name_(name) {}
    virtual ~IModule() = default;

    virtual void OnInit() {}
    virtual void OnRender() = 0;
    virtual void OnShutdown() {}
    virtual void OnResume() {}
    virtual bool CanAutoPause() const { return true; }

    // 该模块画在哪个标签页。RenderInHeader() 的模块不进任何标签页,而是常驻在
    // 窗口顶部状态区(如 StatusModule)。
    virtual Tab  GetTab() const { return Tab::Common; }
    virtual bool RenderInHeader() const { return false; }

    // 纯单 checkbox 的模块返回 true:Setting 会把它们横向流式排到标签页顶部(一行
    // 塞多个),不再每个独占一列。契约 —— compact 模块的 OnRender 必须只画"一行"
    // (一个 checkbox),说明文字放进 hover tooltip,否则流式换行会算错宽度。
    virtual bool IsCompact() const { return false; }

    const std::string &GetName() const { return name_; }
    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool v) { enabled_ = v; }

    // 互斥模块用 — 把对方"安静地"关掉:翻 enabled_ 到 false,并触发 OnShutdown
    // 让对方清理掉 hook / patch 等副作用。和用户手动取消 checkbox 路径行为一致。
    // 不触发 SaveDirty(由 Setting::Render 的 snapshot diff 自动检测变化)。
    void ForceDisable()
    {
        if (enabled_)
        {
            enabled_ = false;
            OnShutdown();
        }
    }

    // NPG uses this to mark a module as "暂停中" separately from the user's
    // intent (enabled_). Patch-type modules must gate their checkbox-enable
    // branch and any per-frame poke on !IsPausedByGuard(), otherwise the user
    // re-toggling the checkbox while NPG holds us down re-applies the patch
    // and shows the effect to a nearby player.
    bool IsPausedByGuard() const { return pausedByGuard_.load(); }
    void SetPausedByGuard(bool v) { pausedByGuard_.store(v); }

    // ----- persistence (per-character config.json) -----
    // Empty key opts a module out of persistence (e.g. StatusModule).
    virtual std::string ConfigKey() const { return {}; }

    // Default just enabled_; overrides should call base then add slider state.
    virtual void SaveState(nlohmann::json &j) const { j["enabled"] = enabled_; }

    // Default toggles enabled_ via OnResume/OnShutdown so patches are applied.
    // Overrides should restore slider values FIRST, then delegate to base so
    // OnResume sees the right slider state.
    virtual void LoadState(const nlohmann::json &j)
    {
        bool want = j.value("enabled", false);
        if (want && !enabled_) { enabled_ = true; OnResume(); }
        else if (!want && enabled_) { enabled_ = false; OnShutdown(); }
        else { enabled_ = want; }
    }

  protected:
    std::string       name_;
    bool              enabled_ = false;
    std::atomic<bool> pausedByGuard_{false};
};

} // namespace GGTB
