#pragma once
#include <string>
#include <utility>
#include <vector>
/*
in param
{
    "MethodIndex":1
    "MethodParam":{...}
}

out param
{
    "Status":1
    "Data":{...}
    "ReturnMsg":""
}
*/

#include "json.hpp"
using json = nlohmann::json;

namespace zzj
{
class CallJ
{
  public:
    enum Status
    {
        Success,
        UnHandledException,
        HandledException,
        IndexError,
    };

    typedef std::tuple<int, std::string> (*method_template)(nlohmann::json::iterator &jparam, std::string &retj);
    typedef std::pair<std::string, method_template> MethodInfo;

    // if name or method has been in methods, return false
    bool AddMethod(method_template method);
    bool AddMethod(std::string name, method_template method);
    MethodInfo GetMethod(int index, bool &success);
    bool DeleteMethod(method_template method);
    bool DeleteMethod(std::string name);
    bool ExecCall(char *injson, int injsonlen, char **outjson, int *outjsonlen);

    void SetRetJson(int iStatus, json &jmsg, std::string &retj, std::string errorMsg);

  private:
    const char *METHODINDEX_STR = "MethodIndex";
    const char *METHODPARAM_STR = "MethodParam";
    const char *STATUS_STR      = "Status";
    const char *RETURNMSG_STR   = "ReturnMsg";
    const char *DATA            = "Data";
    std::vector<MethodInfo> methods;
    bool isIndexParamValid(nlohmann::json::iterator &indexIterator, nlohmann::json::iterator &paramIterator, json &inj);
};
} // namespace zzj
