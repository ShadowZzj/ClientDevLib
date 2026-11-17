#include "DxgiInfoManager.h"
#include "../Graphics.h"
#include "../Window.h"
#include <memory>


#pragma comment(lib, "dxguid.lib")

using namespace zzj;
DxgiInfoManager::DxgiInfoManager()
{
    // define function signature of DXGIGetDebugInterface
    typedef HRESULT(WINAPI * DXGIGetDebugInterface)(REFIID, void **);

    // load the dll that contains the function DXGIGetDebugInterface
    const auto hModDxgiDebug = LoadLibraryExA("dxgidebug.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hModDxgiDebug == nullptr)
    {
        throw ZZJ_LAST_WIN32_EXCEPTION();
    }

    // get address of DXGIGetDebugInterface in dll
    const auto DxgiGetDebugInterface = reinterpret_cast<DXGIGetDebugInterface>(
        reinterpret_cast<void *>(GetProcAddress(hModDxgiDebug, "DXGIGetDebugInterface")));
    if (DxgiGetDebugInterface == nullptr)
    {
        throw ZZJ_LAST_WIN32_EXCEPTION();
    }

    HRESULT hr;
    hr = DxgiGetDebugInterface(__uuidof(IDXGIInfoQueue), reinterpret_cast<void **>(&pDxgiInfoQueue));
    if (FAILED(hr))
    {
        throw ZZJ_DX_EXCEPTION(hr);
    }
}

DxgiInfoManager::~DxgiInfoManager()
{
    if (pDxgiInfoQueue != nullptr)
    {
        pDxgiInfoQueue->Release();
    }
}

std::vector<std::string> DxgiInfoManager::GetMessages() const
{
    std::vector<std::string> messages;
    const auto end = pDxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
    for (auto i = next; i < end; i++)
    {
        HRESULT hr;
        SIZE_T messageLength;
        // get the size of message i in bytes
        hr = pDxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &messageLength);
        if (FAILED(hr))
        {
            throw ZZJ_DX_EXCEPTION(hr);
        }
        // allocate memory for message
        auto bytes    = std::make_unique<byte[]>(messageLength);
        auto pMessage = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE *>(bytes.get());
        // get the message and push its description into the vector
        hr = pDxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, pMessage, &messageLength);
        if (FAILED(hr))
        {
            throw ZZJ_DX_EXCEPTION(hr);
        }
        messages.emplace_back(pMessage->pDescription);
    }
    return messages;
}
void DxgiInfoManager::Begin() noexcept
{
	// set the index (next) so that the next all to GetMessages()
    // will only get errors generated after this call
    next = pDxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
}
void DxgiInfoManager::End()
{
	auto messages = GetMessages();
	if (!messages.empty())
	{
		// throw an exception with error message
        throw ZZJ_DX_INFO_EXCEPTION(messages);
	}
}