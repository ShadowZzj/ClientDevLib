#pragma once
#include "../IModule.h"
#include <spdlog/spdlog.h>
#include <imgui/imgui.h>

class DemoModule : public IModule
{
  public:
    DemoModule() : IModule("Demo") {}

    void OnInit() override
    {
        spdlog::info("DemoModule initialized");
    }

    void OnRender() override
    {
        if (ImGui::Checkbox("Enable", &enabled_))
            spdlog::info("DemoModule enabled: {}", enabled_);

        ImGui::Text("This is a demo module.");

        if (ImGui::Button("Test Button"))
            spdlog::info("DemoModule: Test Button clicked");

        ImGui::SliderFloat("Value", &value_, 0.0f, 100.0f);
    }

    void OnShutdown() override
    {
        spdlog::info("DemoModule shutdown");
    }

  private:
    float value_ = 50.0f;
};
