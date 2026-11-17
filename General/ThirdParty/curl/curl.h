#pragma once

#ifdef _WIN32
#include "win/curl.h"
#else
#include "mac/curl.h"
#endif