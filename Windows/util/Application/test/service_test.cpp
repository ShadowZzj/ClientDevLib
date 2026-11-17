#include <iostream>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <General/util/BaseUtil.hpp>
#include <Windows/util/Service/WinService.h>
class MyService :public WinService {
public:
	using WinService::WinService;

	virtual BOOL OnInit() {
		return true;
	}
	virtual void Run() {
		while (1) {
            
		}
	}
	virtual void OnStop() {
		
	}
	virtual void OnShutdown() {
		
	}
	virtual void OnPreShutDown() {
		
	}
    virtual void OnSessionChange(zzj::Session::SessionMessage msg,zzj::Session session)
    {

    }
};
int main(int argc, char* argv[])
{
	MyService myservice("myservice", "a service test for zzj", "TestService");
	if (argc == 1)
		myservice.Start();
}