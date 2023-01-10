#include "IPC.h"

using namespace zzj;
IPC::SharedMemory::SharedMemory(const wchar_t* name, unsigned int bufSize){

}

unsigned int IPC::SharedMemory::Get(void*){
	return 0;
}

bool IPC::SharedMemory::Set(void*, unsigned int len){
	return false;
}

int IPC::ReadEndPipe::SyncRead(void* buf, size_t bufSize, size_t noReturnBytes)
{
	if (readHandle == INVALID_HANDLE_VALUE)
		return -1;
	DWORD readSum = 0;
	DWORD readBytes;
	while (1) {
		bool success = ReadFile(readHandle, buf, bufSize, &readBytes, NULL);
		if (!success || readBytes == 0)
			return -1;
		readSum += readBytes;
		if (readSum >= noReturnBytes)
			break;
	}
	return readSum;
}
bool IPC::ReadEndPipe::SetHandle(HANDLE read) {
	readHandle = read;
	return true;
}
int IPC::WriteEndPipe::SyncWrite(void* buf, size_t bufSize, size_t noReturnBytes) {
	if (writeHandle == INVALID_HANDLE_VALUE)
		return -1;
	DWORD writeSum = 0;
	DWORD writeBytes;
	while (1) {
		bool success = WriteFile(writeHandle, buf, bufSize, &writeBytes, NULL);
		if (!success || writeBytes == 0)
			return -1;
		writeSum += writeBytes;
		if (writeSum >= noReturnBytes)
			break;
	}
	return writeSum;
}
bool IPC::WriteEndPipe::SetHandle(HANDLE write) {
	writeHandle = write;
	return true;
}