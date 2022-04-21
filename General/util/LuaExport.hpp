#pragma once
#include <General/util/BaseUtil.hpp>
#include <functional>
#include <mutex>
#include <vector>

#define ExportToLua inline static const LuaExportBase CONCAT(luaexport__, __LINE__);

namespace sol
{
class state;
};
#define DECLARE_LUA_EXPORT(className)                                                                                  \
  public:                                                                                                              \
    class className##_LuaExport : public LuaExportBase                                                                 \
    {                                                                                                                  \
      public:                                                                                                          \
        virtual void ExportLua(sol::state &_state)                                                                     \
        {                                                                                                              \
            if (func)                                                                                                  \
                func(_state);                                                                                          \
        }                                                                                                              \
        static className##_LuaExport *CreateInstance()                                                                 \
        {                                                                                                              \
            std::unique_lock<std::mutex> lock(rwMutex);                                                                \
            if (!instance)                                                                                             \
                instance = new className##_LuaExport();                                                                \
            return instance;                                                                                           \
        }                                                                                                              \
        char SetFunc(std::function<void(sol::state &)> &&_func)                                                        \
        {                                                                                                              \
            std::unique_lock<std::mutex> lock(rwMutex);                                                                \
            func = _func;                                                                                              \
            return 1;                                                                                                  \
        }                                                                                                              \
        char operator+(std::function<void(sol::state &)> _func)                                                        \
        {                                                                                                              \
            return SetFunc(std::move(_func));                                                                          \
        }                                                                                                              \
                                                                                                                       \
      private:                                                                                                         \
        className##_LuaExport()                                                                                        \
        {                                                                                                              \
            LuaExportBase::PushLuaExportList(this);                                                                    \
        }                                                                                                              \
        std::function<void(sol::state &)> func;                                                                        \
        inline static std::mutex rwMutex;                                                                              \
        inline static className##_LuaExport *instance = nullptr;                                                       \
    };                                                                                                                 \
                                                                                                                       \
  private:                                                                                                             \
    inline static className##_LuaExport *_test = className##_LuaExport::CreateInstance();

#define DEFINE_LUA_EXPORT(className)                                                                                   \
    static void className##ExportFunction(sol::state &_state);                                                         \
    auto className##instance = className::className##_LuaExport::CreateInstance();                                     \
    static auto className##nonse =                                                                                     \
        *className##instance + [](sol::state &_state) { className##ExportFunction(_state); };                          \
    static void className##ExportFunction(sol::state &_state)
#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, NAME, ...) NAME
#define EXPAND(x) x
#define LUA_EXPORT_MEMBER(...)                                                                                         \
    EXPAND(GET_MACRO(__VA_ARGS__, LUA_EXPORT_MEMBER10, LUA_EXPORT_MEMBER9, LUA_EXPORT_MEMBER8, LUA_EXPORT_MEMBER7,     \
                     LUA_EXPORT_MEMBER6, LUA_EXPORT_MEMBER5, LUA_EXPORT_MEMBER4, LUA_EXPORT_MEMBER3,                   \
                     LUA_EXPORT_MEMBER2, LUA_EXPORT_MEMBER1)(__VA_ARGS__))

#define LUA_EXPORT_MEMBER1(a1) userType[#a1] = &CurClassName::a1

#define LUA_EXPORT_MEMBER2(a1, a2)                                                                                     \
    userType[#a1] = &CurClassName::a1;                                                                                 \
    userType[#a2] = &CurClassName::a2

#define LUA_EXPORT_MEMBER3(a1, a2, a3)                                                                                 \
    userType[#a1] = &CurClassName::a1;                                                                                 \
    userType[#a2] = &CurClassName::a2;                                                                                 \
    userType[#a3] = &CurClassName::a3

#define LUA_EXPORT_MEMBER4(a1, a2, a3, a4)                                                                             \
    userType[#a1] = &CurClassName::a1;                                                                                 \
    userType[#a2] = &CurClassName::a2;                                                                                 \
    userType[#a3] = &CurClassName::a3;                                                                                 \
    userType[#a4] = &CurClassName::a4

#define LUA_EXPORT_MEMBER5(a1, a2, a3, a4, a5)                                                                         \
    userType[#a1] = &CurClassName::a1;                                                                                 \
    userType[#a2] = &CurClassName::a2;                                                                                 \
    userType[#a3] = &CurClassName::a3;                                                                                 \
    userType[#a4] = &CurClassName::a4;                                                                                 \
    userType[#a5] = &CurClassName::a5

#define LUA_EXPORT_MEMBER6(a1, a2, a3, a4, a5, a6)                                                                     \
    userType[#a1] = &CurClassName::a1;                                                                                 \
    userType[#a2] = &CurClassName::a2;                                                                                 \
    userType[#a3] = &CurClassName::a3;                                                                                 \
    userType[#a4] = &CurClassName::a4;                                                                                 \
    userType[#a5] = &CurClassName::a5;                                                                                 \
    userType[#a6] = &CurClassName::a6

#define LUA_EXPORT_MEMBER7(a1, a2, a3, a4, a5, a6, a7)                                                                 \
    userType[#a1] = &CurClassName::a1;                                                                                 \
    userType[#a2] = &CurClassName::a2;                                                                                 \
    userType[#a3] = &CurClassName::a3;                                                                                 \
    userType[#a4] = &CurClassName::a4;                                                                                 \
    userType[#a5] = &CurClassName::a5;                                                                                 \
    userType[#a6] = &CurClassName::a6;                                                                                 \
    userType[#a7] = &CurClassName::a7

#define LUA_EXPORT_MEMBER8(a1, a2, a3, a4, a5, a6, a7, a8)                                                             \
    userType[#a1] = &CurClassName::a1;                                                                                 \
    userType[#a2] = &CurClassName::a2;                                                                                 \
    userType[#a3] = &CurClassName::a3;                                                                                 \
    userType[#a4] = &CurClassName::a4;                                                                                 \
    userType[#a5] = &CurClassName::a5;                                                                                 \
    userType[#a6] = &CurClassName::a6;                                                                                 \
    userType[#a7] = &CurClassName::a7;                                                                                 \
    userType[#a8] = &CurClassName::a8

#define LUA_EXPORT_MEMBER9(a1, a2, a3, a4, a5, a6, a7, a8, a9)                                                         \
    userType[#a1] = &CurClassName::a1;                                                                                 \
    userType[#a2] = &CurClassName::a2;                                                                                 \
    userType[#a3] = &CurClassName::a3;                                                                                 \
    userType[#a4] = &CurClassName::a4;                                                                                 \
    userType[#a5] = &CurClassName::a5;                                                                                 \
    userType[#a6] = &CurClassName::a6;                                                                                 \
    userType[#a7] = &CurClassName::a7;                                                                                 \
    userType[#a8] = &CurClassName::a8;                                                                                 \
    userType[#a9] = &CurClassName::a9

#define LUA_EXPORT_MEMBER10(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)                                                   \
    userType[#a1]  = &CurClassName::a1;                                                                                \
    userType[#a2]  = &CurClassName::a2;                                                                                \
    userType[#a3]  = &CurClassName::a3;                                                                                \
    userType[#a4]  = &CurClassName::a4;                                                                                \
    userType[#a5]  = &CurClassName::a5;                                                                                \
    userType[#a6]  = &CurClassName::a6;                                                                                \
    userType[#a7]  = &CurClassName::a7;                                                                                \
    userType[#a8]  = &CurClassName::a8;                                                                                \
    userType[#a9]  = &CurClassName::a9;                                                                                \
    userType[#a10] = &CurClassName::a10

class LuaExportBase
{
  public:
    virtual void ExportLua(sol::state &_state)
    {
        return;
    }

    static std::vector<LuaExportBase *> &GetLuaExportList()
    {
        std::unique_lock<std::mutex> lock(rwMutex);
        return exports;
    }
    static void PushLuaExportList(LuaExportBase *item)
    {
        std::unique_lock<std::mutex> lock(rwMutex);
        exports.push_back(item);
    }
    inline static std::mutex rwMutex;
    inline static std::vector<LuaExportBase *> exports;
};
