#import "NetworkUtil.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if_dl.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>
@implementation NetworkInformation

- (id)init
{
    self = [super init];
    if (self)
    {
        [self updateData];
    }
    return self;
}

- (void)updateData
{
    [self updateDataFromNetwork];
    self.address    = [self getNetworkAddress];
    self.ipsInRange = [self getIPsInRange];
}

- (void)updateDataFromNetwork
{
    self.deviceIP              = nil;
    self.netmask               = nil;
    self.broadcast             = nil;
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr  = NULL;
    int success                = 0;
    success                    = getifaddrs(&interfaces);
    if (success == 0)
    {
        temp_addr = interfaces;
        while (temp_addr != NULL)
        {
            if (temp_addr->ifa_addr->sa_family == AF_INET)
            {
                if ([[NSString stringWithUTF8String:temp_addr->ifa_name] isEqualToString:@"en0"])
                {
                    self.deviceIP  = [NSString
                        stringWithUTF8String:inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr)];
                    self.netmask   = [NSString
                        stringWithUTF8String:inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_netmask)->sin_addr)];
                    self.broadcast = [NSString
                        stringWithUTF8String:inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_dstaddr)->sin_addr)];
                    break;
                }
            }
            temp_addr = temp_addr->ifa_next;
        }
    }
    freeifaddrs(interfaces);
}

- (NSString *)getNetworkAddress
{
    if (!self.deviceIP || !self.netmask)
    {
        return nil;
    }
    unsigned int address = [self convertSymbolicIpToNumeric:self.deviceIP];
    address &= [self convertSymbolicIpToNumeric:self.netmask];
    return [self convertNumericIpToSymbolic:address];
}

- (NSArray *)getIPsInRange
{
    unsigned int address   = [self convertSymbolicIpToNumeric:self.address];
    unsigned int netmask   = [self convertSymbolicIpToNumeric:self.netmask];
    NSMutableArray *result = [[NSMutableArray alloc] init];
    int numberOfBits;
    for (numberOfBits = 0; numberOfBits < 32; numberOfBits++)
    {
        if ((netmask << numberOfBits) == 0)
        {
            break;
        }
    }
    int numberOfIPs = 0;
    for (int n = 0; n < (32 - numberOfBits); n++)
    {
        numberOfIPs = numberOfIPs << 1;
        numberOfIPs = numberOfIPs | 0x01;
    }
    for (int i = 1; i < (numberOfIPs) && i < numberOfIPs; i++)
    {
        unsigned int ourIP = address + i;
        NSString *ip       = [self convertNumericIpToSymbolic:ourIP];
        [result addObject:ip];
    }
    return result;
}

- (NSString *)convertNumericIpToSymbolic:(unsigned int)numericIP
{
    NSMutableString *sb = [NSMutableString string];
    for (int shift = 24; shift > 0; shift -= 8)
    {
        [sb appendString:[NSString stringWithFormat:@"%d", (numericIP >> shift) & 0xff]];
        [sb appendString:@"."];
    }
    [sb appendString:[NSString stringWithFormat:@"%d", (numericIP & 0xff)]];
    return sb;
}

- (unsigned int)convertSymbolicIpToNumeric:(NSString *)symbolicIP
{
    NSArray *st = [symbolicIP componentsSeparatedByString:@"."];
    if (st.count != 4)
    {
        NSLog(@"error in convertSymbolicIpToNumeric, splited string count: %lu", st.count);
        return 0;
    }
    int i         = 24;
    int ipNumeric = 0;
    for (int n = 0; n < st.count; n++)
    {
        int value = [(NSString *)st[n] intValue];
        if (value != (value & 0xff))
        {
            NSLog(@"error in convertSymbolicIpToNumeric, invalid IP address: %@", symbolicIP);
            return 0;
        }
        ipNumeric += value << i;
        i -= 8;
    }
    return ipNumeric;
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"\nip:%@\nnetmask:%@\nnetwork:%@\nbroadcast:%@", self.deviceIP, self.netmask,
                                      self.address, self.broadcast];
}

@end
#define IFT_ETHER 0x6
char *getMacAddress(char *macAddress, const char *ifName)
{
    int success;
    struct ifaddrs *addrs;
    struct ifaddrs *cursor;
    const struct sockaddr_dl *dlAddr;
    const unsigned char *base;
    int i;

    success = getifaddrs(&addrs) == 0;
    if (success)
    {
        cursor = addrs;
        while (cursor != 0)
        {

            if ((cursor->ifa_addr->sa_family == AF_LINK) &&
                (((const struct sockaddr_dl *)cursor->ifa_addr)->sdl_type == IFT_ETHER))
            {
                dlAddr = (const struct sockaddr_dl *)cursor->ifa_addr;
                base   = (const unsigned char *)&dlAddr->sdl_data[dlAddr->sdl_nlen];
                strcpy(macAddress, "");
                for (i = 0; i < dlAddr->sdl_alen; i++)
                {
                    if (i != 0)
                    {
                        strcat(macAddress, ":");
                    }
                    char partialAddr[3];
                    sprintf(partialAddr, "%02X", base[i]);
                    strcat(macAddress, partialAddr);
                }
            }
            cursor = cursor->ifa_next;
        }

        freeifaddrs(addrs);
    }
    return macAddress;
}

int zzj::NetworkHelper::GetOutputIpAddress(std::string &outIpAddress)
{
    int result = 0;
    const char *destination_address;
    sockaddr_in Addr;
    unsigned long addr;
    char *source_address;
    int sockHandle = 0;
    int AddrLen;

    destination_address  = "8.8.8.8";
    Addr                 = {0};
    addr                 = inet_addr(destination_address);
    Addr.sin_addr.s_addr = addr;
    Addr.sin_family      = AF_INET;
    Addr.sin_port        = htons(9); // 9 is discard port
    sockHandle           = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    AddrLen              = sizeof(Addr);
    result               = connect(sockHandle, (sockaddr *)&Addr, AddrLen);
    if (0 != result)
    {
        result = -2;
        goto exit;
    }
    result = getsockname(sockHandle, (sockaddr *)&Addr, (socklen_t *)&AddrLen);
    if (0 != result)
    {
        result = -3;
        goto exit;
    }
    source_address = inet_ntoa(((struct sockaddr_in *)&Addr)->sin_addr);
    outIpAddress   = source_address;
exit:
    if (sockHandle)
        close(sockHandle);
    return result;
}

int zzj::NetworkHelper::GetAllIpv4(std::vector<std::string> &ipv4List)
{
    int result                 = 0;
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr  = NULL;
    int success                = 0;
    success                    = getifaddrs(&interfaces);
    std::string ipAddr;
    if (success != 0)
    {
        result = -1;
        goto exit;
    }
    temp_addr = interfaces;
    while (temp_addr != NULL)
    {
        if (temp_addr->ifa_addr->sa_family == AF_INET && !(temp_addr->ifa_flags & IFF_LOOPBACK))
        {
            ipAddr = inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr);
            ipv4List.push_back(ipAddr);
        }
        temp_addr = temp_addr->ifa_next;
    }
    freeifaddrs(interfaces);
exit:
    return result;
}
