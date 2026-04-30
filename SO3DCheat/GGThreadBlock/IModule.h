#pragma once
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

  protected:
    std::string name_;
    bool enabled_ = false;
};

} // namespace GGTB
