#include "HardWare.h"
#include <General/util/StrUtil.h>
#include <Windows/util/COM/WMIHelper.h>
#include <Windows.h>
#include <intrin.h>
using namespace zzj;

std::wstring CPU::GetCPUBrandString()
{
    int cpuInfo[4] = {-1};
    char cpuBrandString[0x40]{};
    // Get the information associated with each extended ID.
    __cpuid(cpuInfo, 0x80000000);
    for (int i = 0x80000002; i <= 0x80000004; ++i)
    {
        __cpuid(cpuInfo, i);
        // Interpret CPU brand string
        if (i == 0x80000002)
            memcpy(cpuBrandString, cpuInfo, sizeof(cpuInfo));
        else if (i == 0x80000003)
            memcpy(cpuBrandString + 16, cpuInfo, sizeof(cpuInfo));
        else if (i == 0x80000004)
            memcpy(cpuBrandString + 32, cpuInfo, sizeof(cpuInfo));
    }

    return str::ansi2w(std::string(cpuBrandString));
}

std::vector<std::wstring> HardDrive::GetHardDriveSerialNumber()
{
    WMIWrapper hdHandler;
    HRESULT hRes;
    VARIANT var;
    std::vector<std::wstring> ret;
    int result = 0;

    hRes = hdHandler.WMIConnectServer(L"root\\cimv2");
    if (S_OK != hRes)
    {
        ret.clear();
        return ret;
    }

    hRes = hdHandler.WMIExecQuery("WQL", "SELECT SerialNumber FROM Win32_PhysicalMedia");
    if (S_OK != hRes)
    {
        ret.clear();
        return ret;
    }

    while (S_OK == hdHandler.WMIGetNextObject())
    {
        std::wstring tmp;
        hRes = hdHandler.WMIGetProperty(L"SerialNumber", var);
        if (S_OK != hRes)
        {
            ret.clear();
            result = -1;
            goto exit;
        }

        tmp = (NULL == var.bstrVal) ? L"" : var.bstrVal; 

        hdHandler.WMIReleaseProperty(var);
        ret.push_back(tmp);

    exit:
        hdHandler.WMIReleaseThisObject();
        if (0 != result)
            break;
    }

    return ret;
}

std::wstring Bios::GetBiosSerialNumber()
{
    WMIWrapper hdHandler;
    HRESULT hRes;
    VARIANT var;
    std::wstring ret;
    int result = 0;

    hRes = hdHandler.WMIConnectServer(L"root\\cimv2");
    if (S_OK != hRes)
    {
        ret.clear();
        return ret;
    }

    hRes = hdHandler.WMIExecQuery("WQL", "SELECT * FROM Win32_Bios");
    if (S_OK != hRes)
    {
        ret.clear();
        return ret;
    }

    while (S_OK == hdHandler.WMIGetNextObject())
    {
        hRes = hdHandler.WMIGetProperty(L"SerialNumber", var);
        if (S_OK != hRes)
        {
            ret.clear();
            result = -1;
            goto exit;
        }
        ret = (NULL == var.bstrVal) ? L"" : var.bstrVal;

        hdHandler.WMIReleaseProperty(var);
    exit:
        hdHandler.WMIReleaseThisObject();
        if (0 != result)
            break;
    }

    return ret;
}
