#include "WfpEngine.h"

namespace zzj::network::wfp
{
Engine::~Engine() { Close(); }

DWORD Engine::OpenDynamic(const std::wstring& name)
{
    if (handle_)
        return ERROR_ALREADY_INITIALIZED;
    FWPM_SESSION0 session{};
    session.displayData.name = const_cast<wchar_t*>(name.c_str());
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    session.txnWaitTimeoutInMSec = 5000;
    HANDLE opened = nullptr;
    const DWORD result = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &opened);
    if (result == ERROR_SUCCESS) handle_ = opened;
    return result;
}
void Engine::Close() noexcept
{
    if (handle_)
        FwpmEngineClose0(handle_);
    handle_ = nullptr;
}
DWORD Engine::AddProvider(const FWPM_PROVIDER0& provider)
{
    return handle_ ? FwpmProviderAdd0(handle_, &provider, nullptr) : ERROR_INVALID_HANDLE;
}
DWORD Engine::AddSubLayer(const FWPM_SUBLAYER0& subLayer)
{
    return handle_ ? FwpmSubLayerAdd0(handle_, &subLayer, nullptr) : ERROR_INVALID_HANDLE;
}
DWORD Engine::AddCallout(const FWPM_CALLOUT0& callout, UINT32* id)
{
    return handle_ ? FwpmCalloutAdd0(handle_, &callout, nullptr, id) : ERROR_INVALID_HANDLE;
}
DWORD Engine::AddFilter(const FWPM_FILTER0& filter, UINT64* id)
{
    return handle_ ? FwpmFilterAdd0(handle_, &filter, nullptr, id) : ERROR_INVALID_HANDLE;
}
DWORD Engine::DeleteFilter(UINT64 id)
{
    return handle_ ? FwpmFilterDeleteById0(handle_, id) : ERROR_INVALID_HANDLE;
}

Transaction::Transaction(Engine& engine, bool readOnly) : engine_(engine.Handle())
{
    if (engine_)
        status_ = FwpmTransactionBegin0(engine_, readOnly ? FWPM_TXN_READ_ONLY : 0);
    active_ = status_ == ERROR_SUCCESS;
}
Transaction::~Transaction() { Abort(); }
DWORD Transaction::Commit()
{
    if (!active_)
        return status_ == ERROR_SUCCESS ? ERROR_INVALID_STATE : status_;
    status_ = FwpmTransactionCommit0(engine_);
    if (status_ == ERROR_SUCCESS)
        active_ = false;
    return status_;
}
DWORD Transaction::Abort() noexcept
{
    if (!active_)
        return ERROR_SUCCESS;
    const DWORD result = FwpmTransactionAbort0(engine_);
    active_ = false;
    return result;
}
} // namespace zzj::network::wfp
