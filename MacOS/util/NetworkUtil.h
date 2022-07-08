
#ifndef _MAC_NETWORKUTIL_H_
#define _MAC_NETWORKUTIL_H_

#import <Foundation/Foundation.h>
#include <string>
char *getMacAddress(char *macAddress, const char *ifName);

@interface NetworkInformation : NSObject
@property(nonatomic, retain) NSString *deviceIP;
@property(nonatomic, retain) NSString *netmask;
@property(nonatomic, retain) NSString *address;
@property(nonatomic, retain) NSString *broadcast;
@property(nonatomic, retain) NSArray *ipsInRange;
- (void)updateData;
- (NSString *)description;
@end

#endif
