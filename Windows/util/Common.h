#pragma once
#if _WIN32 || _WIN64
#if _WIN64
#define ENV64BIT
#else
#define ENV32BIT
#endif
#endif

#include <Windows.h>
#include <General/util/File/File.h>
#include <General/util/StrUtil.h>
#include <boost/filesystem.hpp>
#include <Windows/util/HandleHelper.h>
#include <General/util/BaseUtil.hpp>