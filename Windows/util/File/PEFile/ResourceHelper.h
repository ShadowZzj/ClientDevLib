#pragma once
#include <Windows.h>
#include <iostream>
#include <fstream>
namespace zzj {
	class Resource {
	public:
		static bool ExtractResourceToFile(int Id, const char* type,const char* path);
	};
}