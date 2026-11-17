#include "ResourceHelper.h"

bool zzj::Resource::ExtractResourceToFile(int Id, const char* type, const char* path)
{
	HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(Id), type);
	if (NULL == hRes)
		return false;

	DWORD dwSize = SizeofResource(NULL, hRes);
	if (0 == dwSize)
		return false;

	HGLOBAL hGlobal = LoadResource(NULL, hRes);
	if (NULL == hGlobal)
		return false;

	LPVOID lp = LockResource(hGlobal);
	if (NULL == lp)
		return false;

	std::ofstream file;
	file.open(path,std::ios_base::out|std::ios_base::binary);
	file.write((const char*)lp, dwSize);
	file.close();

	FreeResource(hGlobal);
	return true;
}
