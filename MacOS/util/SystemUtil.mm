#import "SystemUtil.h"
#import <Cocoa/Cocoa.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <assert.h>
#include <iostream>
#include <sys/sysctl.h>
#include <sys/types.h>

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
std::string zzj::Computer::GetActiveConsoleSessionId()
{
    @autoreleasepool {
        
        SCDynamicStoreRef store;
        CFStringRef name;
        uid_t uid;
        std::string ret;
        
        store = SCDynamicStoreCreate(NULL, CFSTR("GetConsoleUser"), NULL, NULL);
        name  = SCDynamicStoreCopyConsoleUser(store, &uid, NULL);
        CFRelease(store);
        if (name != NULL)
        {
            CFRelease(name);
            ret = std::to_string(uid);
        }
        else
            ret = "";
        
        return ret;
    }
}

std::string zzj::Computer::GetCurrentUserName()
{
    @autoreleasepool {
        
        SCDynamicStoreRef store;
        CFStringRef name;
        uid_t uid;
        char buf[256];
        Boolean ok;
        
        store = SCDynamicStoreCreate(NULL, CFSTR("GetConsoleUser"), NULL, NULL);
        name  = SCDynamicStoreCopyConsoleUser(store, &uid, NULL);
        CFRelease(store);
        
        if (name != NULL)
        {
            ok = CFStringGetCString(name, buf, 256, kCFStringEncodingUTF8);
            assert(ok == true);
            CFRelease(name);
        }
        else
        {
            strcpy(buf, "");
        }
        
        return buf;
    }
}
