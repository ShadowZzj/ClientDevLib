#include <Windows.h>
#include <iostream>
#include <vector>
#include <Windows/util/AntiDebug/CommonApi.h>
#include<Windows/util/AntiDebug/ExceptionDetect.h>
#include <Windows/util/AntiVM/SandBox.h>
#include <Windows/util/AntiVM/VBox.h>
#include <Windows/util/AntiVM/VMWare.h>
int main()
{
    auto res = zzj::AntiDebug::CommonAntiDebugCheck();
    std::cout << res << std::endl;
    res = zzj::AntiDebug::ExceptionCheck();
    std::cout<<res<<std::endl;

    res = zzj::AntiVM::SandBoxCheck();
    std::cout << res << std::endl;
    res = zzj::AntiVM::VBoxCheck();
    std::cout << res << std::endl;
    res = zzj::AntiVM::VMWareCheck();
    std::cout << res << std::endl;
    system("pause");
    return 0;
}