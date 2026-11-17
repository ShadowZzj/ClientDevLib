#pragma once

#include <winsock2.h>
#include <windows.h>
#include <fwpmu.h>
#include <string>

namespace zzj::network::wfp
{
// This object must outlive transactions. Dynamic sessions remove all their
// objects automatically when closed or when the owning process terminates.
class Engine
{
  public:
    Engine() = default;
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    DWORD OpenDynamic(const std::wstring& name);
    void Close() noexcept;
    HANDLE Handle() const noexcept { return handle_; }
    DWORD AddProvider(const FWPM_PROVIDER0& provider);
    DWORD AddSubLayer(const FWPM_SUBLAYER0& subLayer);
    DWORD AddCallout(const FWPM_CALLOUT0& callout, UINT32* id = nullptr);
    DWORD AddFilter(const FWPM_FILTER0& filter, UINT64* id = nullptr);
    DWORD DeleteFilter(UINT64 id);

  private:
    HANDLE handle_ = nullptr;
};

class Transaction
{
  public:
    explicit Transaction(Engine& engine, bool readOnly = false);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    DWORD Status() const noexcept { return status_; }
    bool Active() const noexcept { return active_; }
    DWORD Commit();
    DWORD Abort() noexcept;

  private:
    HANDLE engine_ = nullptr;
    DWORD status_ = ERROR_INVALID_HANDLE;
    bool active_ = false;
};
} // namespace zzj::network::wfp
