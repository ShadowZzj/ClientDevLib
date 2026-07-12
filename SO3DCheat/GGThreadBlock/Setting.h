#pragma once
#include <Windows/util/DirectX/D3D9Hook.h>
#include <atomic>
#include <json.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "IModule.h"

namespace GGTB
{

class Setting : public zzj::D3D::Setting
{
  public:
    void RegisterModule(std::shared_ptr<IModule> mod);

    void Init() override;
    void Render(bool &open) override;
    void End() override;
    DWORD GetToggleMenuKey() const { return VK_OEM_6; } // ']'

    const std::vector<std::shared_ptr<IModule>> &GetModules() const { return modules_; }

    // True after Init() has run (the external menu thread calls Init during setup).
    // UserConfig::Tick gates on this — modules' OnInit must run before LoadState.
    bool IsInitialized() const { return initialized_.load(); }

  private:
    // 当前所有模块状态的快照(ConfigKey -> SaveState)。和"已保存基线"对比出脏标记。
    nlohmann::json ComputeSnapshot() const;
    // 从 UserConfig 重新拉取 profile 列表 / 活动 profile,并把选中项对齐到活动 profile。
    void           RefreshProfiles();

    void RenderHeader();              // RenderInHeader() 的模块(常驻状态区)
    void RenderConfigSidebar();       // 左侧配置(profile)管理
    void RenderTab(Tab tab);          // 单个标签页(按高度均衡的自适应多列)
    void RenderModule(IModule *mod);  // 单模块(SeparatorText + NPG 禁用包裹)

    // 上一帧实测的每模块渲染高度(像素)。RenderTab 用它做贪心装箱:把下一个
    // 模块塞进当前最矮的列,避免重模块(带表格)扎堆把某一列撑到要滚轮。模块
    // 指针在进程生命周期内稳定(Setting 按值持有 shared_ptr)。
    std::unordered_map<const IModule *, float> moduleHeights_;

    std::vector<std::shared_ptr<IModule>> modules_;
    std::atomic<bool>                     initialized_{false};

    // ----- 配置(profile)UI 状态 -----
    // 模块状态不再每帧自动落盘,只有点「保存」才写。脏标记靠"当前快照 != 基线"算出。
    uint64_t       lastGen_ = 0;     // 上次见到的 UserConfig::ConfigGeneration()
    nlohmann::json savedSnapshot_;   // 最近一次保存/加载后的基线
    bool           dirty_   = false; // 当前状态 != 基线 => 有未保存改动

    std::vector<std::string> profiles_;            // 缓存的 profile 名(UTF-8)
    std::string              activeProfile_;        // 缓存的活动 profile 名
    int                      selectedProfileIdx_ = -1;
    char                     newNameBuf_[64]{};
};

} // namespace GGTB
