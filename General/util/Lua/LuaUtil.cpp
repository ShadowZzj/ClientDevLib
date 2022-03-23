#include "LuaUtil.h"

using namespace zzj;

nlohmann::json LuaUtil::TableToJson(const sol::table &table)
{
    nlohmann::json ret;
    if (CanTableConvertToJsonArray(table))
        return ArrayTableToJson(table);
    for (auto [key, val] : table)
    {
        std::string keyy  = key.as<std::string>();
        sol::type valType = val.get_type();
        if (valType == sol::type::nil)
            ret[keyy] = "";
        else if (valType == sol::type::table)
        {
            ret[keyy] = TableToJson(val.as<sol::table>());
        }
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

bool zzj::LuaUtil::CanTableConvertToJsonArray(const sol::table &table)
{
    sol::type previousKeyType = sol::type::none;
    sol::type previousValType = sol::type::none;
    for (auto [key, val] : table)
    {
        
        sol::type keyType = key.get_type();
        sol::type valType = val.get_type();
        if (previousKeyType == sol::type::none)
            previousKeyType = keyType;

        if (previousValType == sol::type::none)
            previousValType = valType;

        //key type same, val type same
        if (previousKeyType != keyType || previousValType != valType)
            return false;

        if (previousKeyType != sol::type::number)
            return false;
    }
    return true;
}

nlohmann::json zzj::LuaUtil::ArrayTableToJson(const sol::table &table)
{
    nlohmann::json ret;
    std::vector<nlohmann::json> jsonArray;
    std::vector<bool> boolArray;
    std::vector<int> numberArray;
    std::vector<std::string> stringArray;

    for (auto [key, val] : table)
    {
        int keyy = key.as<int>();
        sol::type valType = val.get_type();
        if (sol::type::table == valType)
            jsonArray.push_back(TableToJson(val.as<sol::table>()));
        else if (valType == sol::type::boolean)
            boolArray.push_back(val.as<bool>());
        else if (valType == sol::type::number)
            numberArray.push_back(val.as<int>());
        else if (valType == sol::type::string)
            stringArray.push_back(val.as<std::string>());
    }

    if (!jsonArray.empty())
        ret = jsonArray;
    else if (!boolArray.empty())
        ret = boolArray;
    else if (!numberArray.empty())
        ret = numberArray;
    else if (!stringArray.empty())
        ret = stringArray;

    return ret;
}
