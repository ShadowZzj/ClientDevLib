#pragma once
#include <windows.h>
#include <dxgidebug.h>
#include <string>
#include <vector>

namespace zzj
{
class DxgiInfoManager
{
public:
    static DxgiInfoManager &GetInstance()
    {
        static DxgiInfoManager instance;
        return instance;
    }

    std::vector<std::string> GetMessages() const;
	void Begin() noexcept;
    void End();

  private:
    DxgiInfoManager();
    ~DxgiInfoManager();
    DxgiInfoManager(const DxgiInfoManager &) = delete;
    DxgiInfoManager &operator=(const DxgiInfoManager &) = delete;

    unsigned long long next               = 0u;
    struct IDXGIInfoQueue *pDxgiInfoQueue = nullptr;
};
} // namespace zzj