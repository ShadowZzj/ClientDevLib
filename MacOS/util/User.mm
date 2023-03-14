#import <Collaboration/Collaboration.h>
#import <CoreServices/CoreServices.h>
#include <General/util/User/User.h>

std::vector<zzj::User> zzj::User::GetComputerUsers()
{
    @autoreleasepool
    {
        CSIdentityAuthorityRef defaultAuthority = CSGetLocalIdentityAuthority();
        CSIdentityClass identityClass           = kCSIdentityClassUser;

        CSIdentityQueryRef query = CSIdentityQueryCreate(NULL, identityClass, defaultAuthority);

        CFErrorRef error = NULL;
        CSIdentityQueryExecute(query, 0, &error);

        CFArrayRef results = CSIdentityQueryCopyResults(query);
        int numResults     = CFArrayGetCount(results);

        std::vector<zzj::User> ret;
        for (int i = 0; i < numResults; ++i)
        {

            CSIdentityRef identity = (CSIdentityRef)CFArrayGetValueAtIndex(results, i);

            // Get the user's name
            auto fullName    = CSIdentityGetPosixName(identity);
            NSString *foo    = (__bridge NSString *)fullName;
            std::string name = [foo UTF8String];

            // Get the user's home directory
            auto homedir                 = NSHomeDirectoryForUser([NSString stringWithUTF8String:name.c_str()]);
            std::string homeDirStdString = [homedir UTF8String];

            zzj::User user;
            user.name          = name;
            user.homeDirectory = homeDirStdString;
            ret.push_back(user);
        }

        CFRelease(results);
        CFRelease(query);
        return ret;
    }
}
