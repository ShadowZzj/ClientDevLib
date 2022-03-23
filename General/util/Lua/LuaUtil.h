#pragma once
#include <json.hpp>
#include <sol/sol.hpp>
namespace zzj
{
class LuaUtil
{
  public:
    static nlohmann::json TableToJson(const sol::table &table);
    static bool CanTableConvertToJsonArray(const sol::table &table);

  private:
    static nlohmann::json ArrayTableToJson(const sol::table &table);
};
}; // namespace zzj