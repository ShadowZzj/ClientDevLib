#pragma once
#include <json.hpp>
#include <sol/sol.hpp>
#include <General/util/LuaExport.hpp>
namespace zzj
{
class LuaUtil
{
  public:
    static nlohmann::json TableToJson(const sol::table &table);
    static bool CanTableConvertToJsonArray(const sol::table &table);
    static sol::table JsonToTable(sol::state& lua, const nlohmann::json& input);
    
  private:
    static nlohmann::json ArrayTableToJson(const sol::table &table);
    static sol::table JsonArrayToTable(sol::state& lua,const nlohmann::json& input);
    DECLARE_LUA_EXPORT(LuaUtil);
};
}; // namespace zzj
