#import "SystemUtil.h"
#import <Cocoa/Cocoa.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <assert.h>
#include <iostream>
#include <pwd.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <mach/mach_time.h>
#include <MacOS/util/Process.h>
std::string zzj::Computer::GetIdentifier()
{
    io_service_t platformExpert =
        IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    CFStringRef serialNumberAsCFString = NULL;

    if (platformExpert)
    {
        serialNumberAsCFString = (CFStringRef)IORegistryEntryCreateCFProperty(
            platformExpert, CFSTR(kIOPlatformSerialNumberKey), kCFAllocatorDefault, 0);
        IOObjectRelease(platformExpert);
    }

    NSString *serialNumberAsNSString = nil;
    if (serialNumberAsCFString)
    {
        serialNumberAsNSString = [NSString stringWithString:(__bridge NSString *)serialNumberAsCFString];
        CFRelease(serialNumberAsCFString);
    }

    return [serialNumberAsNSString UTF8String];
}

std::string zzj::Computer::GetUUID()
{
    io_service_t platformExpert =
        IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    CFStringRef guidStr = NULL;

    if (platformExpert)
    {
        guidStr = (CFStringRef)IORegistryEntryCreateCFProperty(platformExpert, CFSTR(kIOPlatformUUIDKey),
                                                               kCFAllocatorDefault, 0);
        IOObjectRelease(platformExpert);
    }

    NSString *serialNumberAsNSString = nil;
    if (guidStr)
    {
        serialNumberAsNSString = [NSString stringWithString:(__bridge NSString *)guidStr];
        CFRelease(guidStr);
    }

    return [serialNumberAsNSString UTF8String];
}
std::vector<std::string> zzj::HardDrive::GetRootHardDriveUUID()
{
    DADiskRef disk;
    CFDictionaryRef descDict;
    DASessionRef session   = DASessionCreate(NULL);
    const char *mountPoint = "/";
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)mountPoint, strlen(mountPoint), TRUE);

    std::vector<std::string> ret;
    if (session)
    {
        disk = DADiskCreateFromVolumePath(NULL, session, url);
        if (disk)
        {
            descDict = DADiskCopyDescription(disk);
            if (descDict)
            {
                CFTypeRef value      = (CFTypeRef)CFDictionaryGetValue(descDict, CFSTR("DAVolumeUUID"));
                CFStringRef strValue = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@"), value);
                NSString *nsValue    = (__bridge NSString *)strValue;
                ret.push_back([nsValue UTF8String]);
                CFRelease(descDict);
                CFRelease(strValue);
            }
            CFRelease(disk);
        }
        CFRelease(session);
    }
    CFRelease(url);
    return ret;
}

std::string zzj::Computer::GetCPUBrandString()
{
    char buf[100];
    size_t buflen = 100;
    sysctlbyname("machdep.cpu.brand_string", &buf, &buflen, NULL, 0);
    return std::string(buf);
}
std::string zzj::Computer::GetCurrentTimeStamp()
{
    UInt64 recordTime   = [[NSDate date] timeIntervalSince1970];
    NSString *timeLocal = [[NSString alloc] initWithFormat:@"%llu", recordTime];
    return [timeLocal UTF8String];
}

void zzj::Computer::GetKernelVersion(KernelVersion* k) {
   static KernelVersion cachedKernelVersion;

   if (!cachedKernelVersion.major) {
      // just in case it fails someday
      cachedKernelVersion = (KernelVersion) { -1, -1, -1 };
      char str[256] = {0};
      size_t size = sizeof(str);
      int ret = sysctlbyname("kern.osrelease", str, &size, NULL, 0);
      if (ret == 0) {
         sscanf(str, "%hd.%hd.%hd", &cachedKernelVersion.major, &cachedKernelVersion.minor, &cachedKernelVersion.patch);
      }
   }
   memcpy(k, &cachedKernelVersion, sizeof(cachedKernelVersion));
}

int zzj::Computer::CompareKernelVersion(KernelVersion v) {
   struct KernelVersion actualVersion;
   GetKernelVersion(&actualVersion);

   if (actualVersion.major != v.major) {
      return actualVersion.major - v.major;
   }
   if (actualVersion.minor != v.minor) {
      return actualVersion.minor - v.minor;
   }
   if (actualVersion.patch != v.patch) {
      return actualVersion.patch - v.patch;
   }

   return 0;
}

bool zzj::Computer::KernelVersionIsBetween(KernelVersion lowerBound, KernelVersion upperBound) {
   return 0 <= CompareKernelVersion(lowerBound)
      && CompareKernelVersion(upperBound) < 0;
}

double zzj::Computer::CalculateNanosecondsPerMachTick(void) {
   mach_timebase_info_data_t info;

   /* WORKAROUND for `mach_timebase_info` giving incorrect values on M1 under Rosetta 2.
    *    rdar://FB9546856 https://openradar.appspot.com/radar?id=5055988478509056
    *
    *    We don't know exactly what feature/attribute of the M1 chip causes this mistake under Rosetta 2.
    *    Until we have more Apple ARM chips to compare against, the best we can do is special-case
    *    the "Apple M1" chip specifically when running under Rosetta 2.
    */

   std::string cpuBrandString = GetCPUBrandString();

   bool isRunningUnderRosetta2 = zzj::Process::IsCurrentProcessRunningTranslated();

   // Kernel version 20.0.0 is macOS 11.0 (Big Sur)
   bool isBuggedVersion = KernelVersionIsBetween((KernelVersion) {20, 0, 0}, (KernelVersion) {999, 999, 999});

   if (isRunningUnderRosetta2 && cpuBrandString.find("Apple M1") != cpuBrandString.npos && isBuggedVersion) {
      // In this case `mach_timebase_info` provides the wrong value, so we hard-code the correct factor,
      // as determined from `mach_timebase_info` when the process running natively.
      info = (mach_timebase_info_data_t) { .numer = 125, .denom = 3 };
   } else {
      // No workarounds needed, use the OS-provided value.
      mach_timebase_info(&info);
   }

   return (double)info.numer / (double)info.denom;
}
