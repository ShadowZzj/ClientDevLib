#include "Pinger.h"
#ifndef _WIN32
std::shared_ptr<zzj::PingReceiver> zzj::MacPinger::receiver_ = nullptr;
std::mutex zzj::MacPinger::receiver_mutex_;
std::atomic<int> zzj::MacPinger::instance_count_(0);
#endif