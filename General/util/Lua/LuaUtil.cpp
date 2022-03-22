#include "LuaUtil.h"

using namespace zzj;

nlohmann::json LuaUtil::TableToJson(const sol::table &table)
{
    nlohmann::json ret;
    for (auto [key, val] : table)
    {
        std::string keyy  = key.as<std::string>();
        sol::type valType = val.get_type();
        if (valType == sol::type::nil)
            ret[keyy] = "";
        else if (valType == sol::type::table)
            ret[keyy] = TableToJson(val.as<sol::table>());
        else if (valType == sol::type::function || valType == sol::type::lightuserdata ||
                 valType == sol::type::userdata || valType == sol::type::thread)
            continue;
        else if (valType == sol::type::boolean)
            ret[keyy] = val.as<bool>();
        else if (valType == sol::type::number)
            ret[keyy] = val.as<int>();
        else if (valType == sol::type::string)
            ret[keyy] = val.as<std::string>();
    }
    return ret;
}
