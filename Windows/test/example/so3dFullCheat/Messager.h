#include <json.hpp>
class Messager
{
  public:
	nlohmann::json GetBasicInfo();
    int PostMoney();
    int PostDieInfo();
    int PostInfo();
    void CardHandler();
    std::string serverDestination = "http://123.56.255.71:1265";
};