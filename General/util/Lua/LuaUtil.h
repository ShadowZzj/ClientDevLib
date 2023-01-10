#pragma once
#include <json.hpp>
#include <sol/sol.hpp>
#include <General/util/Lua/LuaExport.hpp>
#include <optional>
namespace zzj
{
class LuaUtil
{
  public:
    template <class T> static std::optional<sol::usertype<T>> GetUserType(const std::string& name,sol::state &_state)
    {
        sol::object obj = _state[name];
        sol::usertype<T> userType;
        if (obj == sol::lua_nil)
        {
            return {};
        }
        else if (obj.get_type() != sol::type::table)
        {
            return {};
        }
        else
            return _state[name];
    }
    static nlohmann::json TableToJson(const sol::table &table);
    static bool CanTableConvertToJsonArray(const sol::table &table);
    static sol::table JsonToTable(sol::state& lua, const nlohmann::json& input);
    
  private:
    static nlohmann::json ArrayTableToJson(const sol::table &table);
    static sol::table JsonArrayToTable(sol::state& lua,const nlohmann::json& input);
    DECLARE_LUA_EXPORT(LuaUtil);
};
}; // namespace zzj
