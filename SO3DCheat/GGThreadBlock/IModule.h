#pragma once
#include <json.hpp>
#include <string>

namespace GGTB
{

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

    const std::string &GetName() const { return name_; }
    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool v) { enabled_ = v; }

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
    std::string name_;
    bool enabled_ = false;
};

} // namespace GGTB
