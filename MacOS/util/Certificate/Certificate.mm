#import <Foundation/Foundation.h>
#include <General/util/Certificate/Certificate.h>
#include <General/util/StrUtil.h>
#include <General/util/System/System.h>
#include <MacOS/util/FileUtil.h>
using namespace zzj;
std::tuple<int,bool> IsCertificateTrusted(SecCertificateRef certificate) {
    @autoreleasepool {
        CFArrayRef certArray = CFArrayCreate(kCFAllocatorDefault, (const void **)&certificate, 1, NULL);
        SecPolicyRef policy = SecPolicyCreateBasicX509();
        SecTrustRef trust = NULL;
        OSStatus status = SecTrustCreateWithCertificates(certArray, policy, &trust);
        DEFER{
            CFRelease(certArray);
            CFRelease(policy);
            if(trust)
                CFRelease(trust);
        };
        
        if (status != errSecSuccess) {
            return {-1,false};
        }
        return {0,SecTrustEvaluateWithError(trust, nil)};
    }
}

std::tuple<int, Certificate> Certificate::GetCertificateInfo(SecCertificateRef certificate)
{
    @autoreleasepool
    {
        int result = 0;
        Certificate cert;
        NSArray *arr        = [NSArray arrayWithObjects:(id)kSecOIDX509V1IssuerName, nil];
        CFDictionaryRef dic = SecCertificateCopyValues((SecCertificateRef)certificate, (__bridge CFArrayRef)arr, nil);
        NSDictionary *ndic  = CFBridgingRelease(dic);
        ndic                = [ndic objectForKey:(__bridge NSString *)kSecOIDX509V1IssuerName];
        std::string signerName;
        NSArray *issuerNameArr = [ndic objectForKey:@"value"];
        for (int j = 0; j < [issuerNameArr count]; j++)
        {

            NSDictionary *internal = [issuerNameArr objectAtIndex:j];
            if (![((NSString *)[internal
                    objectForKey:@"label"]) isEqualToString:(__bridge NSString *)kSecOIDCommonName])
                continue;
            signerName = [[internal objectForKey:@"value"] UTF8String];
        }
        std::string serialNumber;
        std::string commanName;

        CFStringRef name;
        NSString *tmp;
        SecCertificateCopyCommonName((SecCertificateRef)certificate, &name);
        tmp = CFBridgingRelease(name);
        commanName = [tmp UTF8String];

        CFErrorRef err                  = nil;
        NSData *serialNumberData        = nil;
        const unsigned char *dataBuffer = nullptr;
        NSMutableString *hexString      = nil;
        std::string retStr;
        CFDataRef serialNumberCData = SecCertificateCopySerialNumberData(certificate, &err);
        if (nil != err)
        {
            result = -1;
            CFRelease(err);
            return {result, cert};
        }
        serialNumberData = CFBridgingRelease(serialNumberCData);
        dataBuffer       = (const unsigned char *)serialNumberData.bytes;
        hexString        = [NSMutableString stringWithCapacity:serialNumberData.length * 2];
        for (int j = 0; j < serialNumberData.length; j++)
        {
            [hexString appendString:[NSString stringWithFormat:@"%02lx", (unsigned long)dataBuffer[j]]];
        }
        result = zzj::str::HexStrToDecStr([hexString UTF8String], retStr);
        if (0 != result)
        {
            result = -2;
            return {result, cert};
        }
        serialNumber   = retStr;
        cert._name     = commanName;
        cert._issuer   = signerName;
        cert._sequence = serialNumber;
        auto [trustedResult,trustedBool] = IsCertificateTrusted(certificate);
        if(0 != trustedResult)
            return {-3,cert};
        cert._isTrust = trustedBool;
        return {result, cert};
    }
}

std::tuple<int, std::vector<Certificate>> Certificate::ImportCertificateTemplate(
    const std::string &content, const std::string &passwd, const Certificate::StoreType &storeType,
    std::vector<std::function<bool(Certificate)>> predicate, OperateType operateType)
{
    @autoreleasepool
    {
        std::vector<Certificate> retCertificates;
        SecKeychainRef userKeychain = NULL;
        std::string userLoginKeychainPath;
        NSString *homeDir = nil;
        OSStatus status;
        bool isPredicateOk     = false;
        bool result            = false;
        int ret                = 0;
        NSData *PKCS12Data     = [[NSData alloc] initWithBytes:content.data() length:content.size()];
        CFDataRef inPKCS12Data = (__bridge CFDataRef)PKCS12Data;
        NSString *certPassword = [NSString stringWithUTF8String:passwd.c_str()];
        CFStringRef password   = (__bridge CFStringRef)certPassword;
        NSDictionary *itemsDic;
        NSArray *itemsArr;
        SecItemImportExportKeyParameters params{NULL};
        SecExternalItemType itemType   = kSecItemTypeUnknown;
        SecExternalFormat format       = kSecFormatUnknown;
        SecItemImportExportFlags flags = 0;
        params.flags                   = kSecKeyNoAccessControl;
        params.passphrase              = password;
        CFArrayRef items               = NULL;
        NSDictionary *options;
        OSStatus securityError =
            SecItemImport(inPKCS12Data, CFSTR(".p12"), &format, &itemType, flags, &params, nil, &items);
        if (securityError != 0)
        {
            CFStringRef errorStr = SecCopyErrorMessageString(securityError, NULL);
            ret                  = -1;
            goto exit;
        }

        if (storeType == Certificate::StoreType::CurrentUser)
        {
            std::string userName;
            auto userInfo = zzj::UserInfo::GetActiveUserInfo();

            userName = userInfo.has_value() ? userInfo->userName : "";
            if (userName.empty())
            {
                ret = -2;
                goto exit;
            }
            homeDir = NSHomeDirectoryForUser([NSString stringWithUTF8String:userName.c_str()]);
            if (nil == homeDir)
            {
                ret = -3;
                goto exit;
            }
            userLoginKeychainPath = [homeDir UTF8String];
            userLoginKeychainPath += "/Library/Keychains/login.keychain-db";

            status = SecKeychainOpen(userLoginKeychainPath.c_str(), &userKeychain);
            if (errSecSuccess != status)
            {
                ret = -4;
                goto exit;
            }
        }

        itemsArr = (__bridge NSArray *)items;
        for (int i = 0; i < itemsArr.count; i++)
        {
            SecCertificateRef certRef = (SecCertificateRef)CFArrayGetValueAtIndex(items, i);
            id itemClass;
            if (CFGetTypeID((void *)certRef) == SecIdentityGetTypeID())
                itemClass = (__bridge id)kSecClassIdentity;
            else if (CFGetTypeID((void *)certRef) == SecCertificateGetTypeID())
                itemClass = (__bridge id)kSecClassCertificate;

            NSDictionary *newTrustSettings =
                @{(id)kSecTrustSettingsResult : [NSNumber numberWithInt:kSecTrustSettingsResultTrustRoot]};
            securityError = SecTrustSettingsSetTrustSettings(certRef, kSecTrustSettingsDomainAdmin,
                                                             (__bridge CFTypeRef)(newTrustSettings));

            NSDictionary *addquery;

            if (NULL != userKeychain)
                addquery = @{
                    (id)kSecValueRef : (__bridge id)certRef,
                    (id)kSecClass : (id)itemClass,
                    (id)kSecUseKeychain : (__bridge id)userKeychain
                };
            else
                addquery = @{(id)kSecValueRef : (__bridge id)certRef, (id)kSecClass : (id)itemClass};

            auto [getInfoRes, cert] = GetCertificateInfo(certRef);
            if (0 != getInfoRes)
            {
                ret = -5;
                goto exit;
            }

            for (auto predicateFunc : predicate)
            {
                isPredicateOk = predicateFunc(cert);
                if (!isPredicateOk)
                    break;
            }
            if (!isPredicateOk)
                continue;

            switch (operateType)
            {
            case Certificate::OperateType::Add:
                securityError = SecItemAdd((__bridge CFDictionaryRef)addquery, NULL);
                if (errSecDuplicateItem == securityError)
                    break;
                if (securityError != errSecSuccess)
                {
                    ret = -6;
                    goto exit;
                }
                break;
            case Certificate::OperateType::Query:
                break;
            }
            retCertificates.push_back(cert);
        }
    exit:
        if (userKeychain)
            CFRelease(userKeychain);
        CFRelease(items);
        return {ret, retCertificates};
    }
}
std::tuple<int, std::vector<Certificate>> Certificate::GetCerticifateTemplate(
    const Certificate::StoreType &storeType, std::vector<std::function<bool(std::string)>> predicate,
    CertificateTemplateType templateType, OperateType operateType)
{
    @autoreleasepool
    {
        std::vector<Certificate> retCertificate;
        int result       = 0;
        CFArrayRef certs = NULL;
        OSStatus status;
        NSArray *certificates = nil;
        NSString *ret         = nil;
        NSDictionary *options;
        SecKeychainRef userKeychain   = NULL;
        SecKeychainRef systemKeychain = NULL;
        std::string userLoginKeychainPath;
        std::string systemKeychainPath  = "/Library/Keychains/System.keychain";
        CFArrayRef searchKeychain       = NULL;
        NSString *homeDir               = nil;
        CFMutableArrayRef newSearchList = CFArrayCreateMutable(NULL, 0, NULL);

        if (storeType == Certificate::StoreType::LocalMachine)
        {
            status = SecKeychainOpen(systemKeychainPath.c_str(), &systemKeychain);
            if (errSecSuccess != status)
            {
                result = -2;
                goto exit;
            }
            CFArrayAppendValue(newSearchList, systemKeychain);
        }
        else
        {
            std::string userName;
            auto userInfo = zzj::UserInfo::GetActiveUserInfo();

            userName = userInfo.has_value() ? userInfo->userName : "";
            if (userName.empty())
            {
                result = -4;
                goto exit;
            }
            homeDir = NSHomeDirectoryForUser([NSString stringWithUTF8String:userName.c_str()]);
            if (nil == homeDir)
            {
                result = -1;
                goto exit;
            }
            userLoginKeychainPath = [homeDir UTF8String];
            userLoginKeychainPath += "/Library/Keychains/login.keychain-db";

            status = SecKeychainOpen(userLoginKeychainPath.c_str(), &userKeychain);
            if (errSecSuccess != status)
            {
                result = -2;
                goto exit;
            }
            CFArrayAppendValue(newSearchList, userKeychain);
        }
        options = @{
            (id)kSecClass : (id)kSecClassCertificate,
            (id)kSecMatchSearchList : (__bridge id)newSearchList,
            (id)kSecMatchLimit : (__bridge id)kSecMatchLimitAll
        };
        status = SecItemCopyMatching((__bridge CFDictionaryRef)options, (CFTypeRef *)&certs);
        if (errSecItemNotFound == status)
        {
            result = 0;
            goto exit;
        }
        else if (errSecSuccess != status)
        {

            result = -3;
            goto exit;
        }
        certificates = CFBridgingRelease(certs);

        for (int i = 0; i < [certificates count]; i++)
        {
            SecCertificateRef certificate = (__bridge SecCertificateRef)([certificates objectAtIndex:i]);
            bool isPredicateOk            = false;

            auto [res, cert] = GetCertificateInfo(certificate);
            if (0 != res)
            {
                goto exit;
            }
            switch (templateType)
            {
            case CertificateTemplateType::Issuer:
                for (auto predicateFunc : predicate)
                {
                    isPredicateOk = predicateFunc(cert._issuer);
                    if (!isPredicateOk)
                        break;
                }
                break;
            case CertificateTemplateType::Sequence:
                for (auto predicateFunc : predicate)
                {
                    isPredicateOk = predicateFunc(cert._sequence);
                    if (!isPredicateOk)
                        break;
                }
                break;
            case CertificateTemplateType::Name:
                for (auto predicateFunc : predicate)
                {
                    isPredicateOk = predicateFunc(cert._name);
                    if (!isPredicateOk)
                        break;
                }
                break;
            }
            if (isPredicateOk)
            {
                retCertificate.push_back(cert);
                if (Certificate::OperateType::Delete == operateType)
                {
                    id itemClass;
                    if (CFGetTypeID((void *)certificate) == SecCertificateGetTypeID())
                        itemClass = (__bridge id)kSecClassCertificate;
                    else if (CFGetTypeID((void *)certificate) == SecIdentityGetTypeID())
                        itemClass = (__bridge id)kSecClassIdentity;
                    const void *ref[]          = {certificate};
                    CFArrayRef cfItemList      = CFArrayCreate(NULL, ref, 1, NULL);
                    NSDictionary *deleteOption = nullptr;
                    if (storeType == Certificate::StoreType::LocalMachine)
                        deleteOption = @{
                            (__bridge id)kSecClass : itemClass,
                            (__bridge id)kSecMatchItemList : (__bridge id)cfItemList,
                            (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitAll
                        };
                    else
                        deleteOption = @{
                            (__bridge id)kSecClass : itemClass,
                            (id)kSecMatchSearchList : (__bridge id)newSearchList,
                            (__bridge id)kSecMatchItemList : (__bridge id)cfItemList,
                            (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitAll
                        };

                    status = SecItemDelete((__bridge CFDictionaryRef)deleteOption);
                    CFRelease(cfItemList);
                }
            }
        }
    exit:
        if (userKeychain)
            CFRelease(userKeychain);
        if (systemKeychain)
            CFRelease(systemKeychain);
        if (searchKeychain)
            CFRelease(searchKeychain);
        if (newSearchList)
            CFRelease(newSearchList);
        return {result, retCertificate};
    }
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::DoFromFileTemplate(
    const std::string &filePath, const std::string &passwd, zzj::Certificate::StoreType storeType,
    std::function<std::tuple<int, std::vector<Certificate>>(const std::string &filePath, const std::string &passwd,
                                                            zzj::Certificate::StoreType storeType)>
        func)
{
    @autoreleasepool
    {
        std::vector<Certificate> certs;
        unsigned long long fileSize = 0;
        char *fileContent           = nullptr;
        DEFER
        {
            if (fileContent)
                delete[] fileContent;
        };
        bool result          = false;
        int ret              = 0;
        NSString *nsFileName = [NSString stringWithUTF8String:filePath.c_str()];
        fileSize             = [[[NSFileManager defaultManager] attributesOfItemAtPath:nsFileName error:nil] fileSize];

        if (fileSize <= 0)
        {
            ret = -1;
            return {ret, certs};
        }

        fileContent = new char[fileSize]{0};
        if (nullptr == fileContent)
        {
            ret = -2;
            return {ret, certs};
        }

        result = zzj::File::ReadFileAtOffset(filePath.c_str(), fileContent, fileSize, std::ios::beg);
        if (!result)
        {
            ret = -3;
            return {ret, certs};
        }

        auto [tmpRet, tmpCerts] = func(std::string(fileContent, fileContent + fileSize), passwd, storeType);
        ret                     = tmpRet;
        certs                   = tmpCerts;

        return {ret, certs};
    }
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::GetCerticifateByIssuer(const std::string &issuer,
                                                                                   const StoreType &storeType)
{
    return GetCerticifateTemplate(storeType, {[issuer](std::string _issuer) { return issuer == _issuer; }},
                                  CertificateTemplateType::Issuer);
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::GetCerticifateByName(const std::string &name,
                                                                                 const StoreType &storeType)
{
    return GetCerticifateTemplate(storeType,
                                  {[name](std::string _name) { return _name.find(name) != std::string::npos; }},
                                  CertificateTemplateType::Name);
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::GetCerticifateBySequence(const std::string &sequence,
                                                                                     const StoreType &storeType)
{
    return GetCerticifateTemplate(storeType, {[sequence](std::string _sequence) { return sequence == _sequence; }},
                                  CertificateTemplateType::Sequence);
}
int zzj::Certificate::Delete()
{
    auto [result, _1]  = GetCerticifateTemplate(StoreType::CurrentUser,
                                                {[this](std::string _sequence) { return this->_sequence == _sequence; },
                                                 [this](std::string _name) { return this->_name == _name; },
                                                 [this](std::string _issuer) { return this->_issuer == _issuer; }},
                                                CertificateTemplateType::Sequence);
    auto [result2, _2] = GetCerticifateTemplate(StoreType::LocalMachine,
                                                {[this](std::string _sequence) { return this->_sequence == _sequence; },
                                                 [this](std::string _name) { return this->_name == _name; },
                                                 [this](std::string _issuer) { return this->_issuer == _issuer; }},
                                                CertificateTemplateType::Sequence);
    return result;
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::AddFromContent(const std::string &content,
                                                                           const std::string &passwd,
                                                                           zzj::Certificate::StoreType storeType)
{
    return ImportCertificateTemplate(content, passwd, storeType, {[](Certificate cert) { return true; }},
                                     Certificate::OperateType::Add);
}

std::tuple<int, std::vector<Certificate>> zzj::Certificate::AddFromFile(const std::string &filePath,
                                                                        const std::string &passwd,
                                                                        zzj::Certificate::StoreType storeType)
{
    return DoFromFileTemplate(filePath, passwd, storeType, AddFromContent);
}

std::tuple<int, std::vector<Certificate>> zzj::Certificate::ReadFromFile(const std::string &filePath,
                                                                         const std::string &passwd, StoreType storeType)
{
    return DoFromFileTemplate(filePath, passwd, storeType, ReadFromContent);
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::ReadFromContent(const std::string &content,
                                                                            const std::string &passwd,
                                                                            StoreType storeType)
{
    return ImportCertificateTemplate(content, passwd, storeType, {[](Certificate cert) { return true; }},
                                     Certificate::OperateType::Query);
}
