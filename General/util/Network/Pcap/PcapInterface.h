#ifndef _PCAPINTERFACE_GENERAL_H
#define _PCAPINTERFACE_GENERAL_H
#include <pcap.h>
#include <string>
#include <vector>
namespace zzj
{
class PcapInterface
{
public:
    class InterfaceInfo
    {
    public:
        friend class PcapInterface;
        std::string m_ifName;
        std::string m_MacAddress;
        std::vector<std::string> m_IPV4Address;
        std::vector<std::string> m_IPV6Address;
        uint32_t m_InterfaceType;
        uint32_t m_DataLinkType;
    private:
        static int GetInterfaceInfo(const pcap_if_t * pcapIf,InterfaceInfo& interfaceInfo);
        
    };
    struct ParamWrap
    {
        u_char* param;
        PcapInterface* pcapInterface;
    };
    int OpenForCapture(bool promisc,uint32_t captureLength);
    int Close();
    void StopCapture();
    int SetFilter(const std::string& filterExp);
    int SetPacketHandler(pcap_handler handler,void* param);
    int RunLoop();
    static int GetAllInterfaces(std::vector<PcapInterface> &interfaces);
    static int GetInterface(const std::string &interfaceName, PcapInterface &_interface);
    ~PcapInterface();
    PcapInterface(){
        int a=5;
    };
protected:
    static void PacketHandlerInternal(u_char *, const struct pcap_pkthdr *,
                                      const u_char *);
    
    
    
public:
    InterfaceInfo m_InterfaceInfo;
    
private:
    pcap_t* m_SessionHandle = nullptr;
    pcap_handler m_packetHandler = nullptr;
    void* m_Param;
    bool requestStop = false;
};

}; // namespace zzj
#endif
