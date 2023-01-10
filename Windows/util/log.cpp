#include "log.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/Memory/Allocator/Allocator.hpp>
#include <stdarg.h>
#include <string>
//Any cpp include log.h and one needs to call FileLoggerInit.
#ifdef SPDLOG
std::shared_ptr<spdlog::logger> file_logger;
bool isfile_loggerInit = false;

void FileLoggerInit(const char* fileName) {
	if (isfile_loggerInit) {
		return;
	}
	file_logger = spdlog::basic_logger_mt("log", fileName);
	spdlog::set_default_logger(file_logger);
	spdlog::flush_every(std::chrono::seconds(2));
	isfile_loggerInit = true;
	return;
}
#else
std::shared_ptr<SimpleLogger> file_logger;
bool isfile_loggerInit = false;

void FileLoggerInit(const char* fileName) {
	if (isfile_loggerInit) {
		return;
	}
	file_logger = std::make_shared<SimpleLogger>(fileName);
	isfile_loggerInit = true;
}

void SimpleLogger::info(const char* info, ...) {
	va_list arguments;
	va_start(arguments, info);
	char* convertedInfo = zzj::str::FmtV(info, arguments);
	DEFER{ zzj::Allocator::FreeMemory(convertedInfo); };

	ForwardStr(convertedInfo, LogType::Info);

	va_end(arguments);

}

void SimpleLogger::error(const char* info, ...) {
	va_list arguments;
	va_start(arguments, info);
	char* convertedInfo = zzj::str::FmtV(info, arguments);
    DEFER
    {
        zzj::Allocator::FreeMemory(convertedInfo);
    };

	ForwardStr(info, LogType::Error);

	va_end(arguments);
}

char* SimpleLogger::GetDateInfo() {
	using namespace std;
	time_t now = time(0);
	//产生“YYYY-MM-DD hh:mm:ss”格式的字符串。
	char s[32];
	tm tmmm;
	localtime_s(&tmmm, &now);
	strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", &tmmm);
	return zzj::str::Dup(s);
}

char* SimpleLogger::GetLogTypeInfo(LogType type) {
	switch (type)
	{
	case SimpleLogger::Info:
		return zzj::str::Dup(" [info] ");
	case SimpleLogger::Error:
		return zzj::str::Dup(" [error] ");
	case SimpleLogger::Critical:
		return zzj::str::Dup(" [critical] ");
	default:
		return nullptr;
	}
}

void  SimpleLogger::ForwardStr(const char* info, LogType logType) {
    int infoLen = std::string(info).length();

	char* date = GetDateInfo();
	char* logTypeStr = GetLogTypeInfo(logType);
    DEFER
    {
        zzj::Allocator::FreeMemory(date);
        zzj::Allocator::FreeMemory(logTypeStr);
    };

	std::string totalMessage;
	totalMessage += date;
    totalMessage += logTypeStr;
	totalMessage += info;

	fd << totalMessage << std::endl;
	fd.flush();
}
#endif