#pragma once
#ifdef SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
extern std::shared_ptr<spdlog::logger> file_logger;

void FileLoggerInit(const char* fileName);
#else
#include <fstream>
#include <ctime>
#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
class SimpleLogger;
extern std::shared_ptr<SimpleLogger> file_logger;
void FileLoggerInit(const char* fileName);

class SimpleLogger {
public:
	enum LogType {
		Info,
		Error,
		Critical
	};
	SimpleLogger(const std::string& fileName) :fileName(fileName) {
		if (!fileName.empty()) {
			fd.open(fileName, std::ios_base::out | std::ios_base::app);
		}
		else
			zzj::CrashMe();
	}
	~SimpleLogger() {
		fd.close();
	}
	void info(const char* info, ...);
	void error(const char* info, ...);

private:
	std::string fileName;
	std::ofstream fd;
	char* GetDateInfo();
	char* GetLogTypeInfo(LogType type);
	void ForwardStr(const char* info, LogType logType);
};
#endif 
