#include <General/util/Network/Pcap/PcapInterface.h>
#include <General/util/StrUtil.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <pcap.h>
#include <spdlog/spdlog.h>

void zzj::PcapInterface::StopCapture()
{
    requestStop = true;
    pcap_breakloop(m_SessionHandle);
    return;
}


void zzj::PcapInterface::PacketHandlerInternal(u_char* paramWrapper, const struct pcap_pkthdr* packetHeader,const u_char *packet)
{
    ParamWrap* param = (ParamWrap*) paramWrapper;
    if(!param || !param->pcapInterface)
        return;
    
    if(param->pcapInterface->requestStop)
    {
        pcap_breakloop(param->pcapInterface->m_SessionHandle);
        return;
    }
    return param->pcapInterface->m_packetHandler(param->param,packetHeader,packet);
}

int zzj::PcapInterface::OpenForCapture(bool promisc,uint32_t captureLength)
{
    if(m_SessionHandle)
    {
        pcap_close(m_SessionHandle);
        m_SessionHandle = nullptr;
    }
    char errBuf[1024] = {0};
    m_SessionHandle = pcap_open_live(m_InterfaceInfo.m_ifName.c_str(), captureLength, promisc, 1000, errBuf);
    if(m_SessionHandle == nullptr){
        spdlog::error("openforcapture fails with {}",errBuf);
        return -1;
    }
    return 0;
}

int zzj::PcapInterface::SetFilter(const std::string& filterExp)
{
    if(!m_SessionHandle)
        return -1;
    struct bpf_program fp;        /* The compiled filter expression */
    
    if (pcap_compile(m_SessionHandle, &fp, filterExp.c_str(), 0, PCAP_NETMASK_UNKNOWN) == -1) {
        spdlog::error("Couldn't parse filter {}: {}\n", filterExp.c_str(), pcap_geterr(m_SessionHandle));
        return(-2);
    }
    if (pcap_setfilter(m_SessionHandle, &fp) == -1) {
        spdlog::error("Couldn't install filter {}: {}\n", filterExp.c_str(), pcap_geterr(m_SessionHandle));
        return(-3);
    }
    return 0;
}
int zzj::PcapInterface::SetPacketHandler(pcap_handler handler,void* param)
{
    m_packetHandler = handler;
    m_Param = param;
    return 0;
}
int zzj::PcapInterface::RunLoop()
{
    if(!m_SessionHandle)
        return -1;
    ParamWrap paramWrap;
    paramWrap.param = (u_char*)m_Param;
    paramWrap.pcapInterface = this;
    return pcap_loop(m_SessionHandle, -1, PacketHandlerInternal, (u_char*)&paramWrap);
}

int zzj::PcapInterface::Close()
{
    if(m_SessionHandle)
        pcap_close(m_SessionHandle);
    m_SessionHandle = nullptr;
}
zzj::PcapInterface::~PcapInterface()
{
}

int zzj::PcapInterface::InterfaceInfo::GetInterfaceInfo(const pcap_if_t * pcapIf,InterfaceInfo& interfaceInfo)
{
    interfaceInfo.m_InterfaceType = pcapIf->flags;
    for (const pcap_addr *addrTest = pcapIf->addresses; addrTest != NULL; addrTest = addrTest->next)
    {
        if (addrTest->addr->sa_family == AF_INET)
        {
            sockaddr_in *netMask = (sockaddr_in *)addrTest->netmask;
            sockaddr_in *sockAddr = (sockaddr_in *)addrTest->addr;
            char str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(sockAddr->sin_addr), str, INET_ADDRSTRLEN);
            interfaceInfo.m_IPV4Address.push_back(str);
        }
        else if (addrTest->addr->sa_family == AF_INET6)
        {
            sockaddr_in6 *sockAddr = (sockaddr_in6 *)addrTest->addr;
            char str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &(sockAddr->sin6_addr), str, INET6_ADDRSTRLEN);
            interfaceInfo.m_IPV6Address.push_back(str);
        }
        else if (addrTest->addr->sa_family == AF_LINK)
        {
            sockaddr_dl *sockAddr = (sockaddr_dl *)addrTest->addr;
            std::string macTmp = link_ntoa(sockAddr);
            std::string sockDataStr = sockAddr->sdl_data;
            
            std::string macAddr;
            for(int i=0;i<sockAddr->sdl_alen;i++){
                if(i !=0 )
                    macAddr+=":";
                macAddr +=ToHex(*(sockAddr->sdl_data+sockAddr->sdl_nlen+i), false);
            }
            interfaceInfo.m_MacAddress = macAddr;
            interfaceInfo.m_ifName = macTmp.substr(0,sockAddr->sdl_nlen);
        }
    }
    
    char errBuf[PCAP_ERRBUF_SIZE]{0};
    pcap_t *handle = pcap_open_live(interfaceInfo.m_ifName.c_str(), BUFSIZ, 0, 1000, errBuf);
    if (handle == NULL) {
        spdlog::error("pcap_open_live error:{}",errBuf);
        return -1;
    }
    interfaceInfo.m_DataLinkType=pcap_datalink(handle);
    pcap_close(handle);
    return 0;
}

int zzj::PcapInterface::GetAllInterfaces(std::vector<PcapInterface> &interfaces)
{
    pcap_if_t *alldevs;
    pcap_if_t *d;
    char errbuf[PCAP_ERRBUF_SIZE + 1];
    int index = 0;
    
    if (-1 == pcap_findalldevs(&alldevs, errbuf))
    {
        spdlog::error("pcap_findalldevs error with {}",errbuf);
        return -1;
    }
    
    interfaces.clear();
    for (d = alldevs; d != NULL; d = d->next, index++){
        PcapInterface tmp;
        int res = InterfaceInfo::GetInterfaceInfo(d, tmp.m_InterfaceInfo);
        if(0 == res)
            interfaces.push_back(std::move(tmp));
        else
            spdlog::error("GerInterfaceInfo error with {}",res);
    }
    pcap_freealldevs(alldevs);
    return 0;
}

int zzj::PcapInterface::GetInterface(const std::string &interfaceName, PcapInterface &_interface)
{
    return 0;
}
