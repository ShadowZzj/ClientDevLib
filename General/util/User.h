#pragma once
#include <vector>
#include <string>
namespace zzj
{
    class User
    {
    public:
        std::string name;
        std::string homeDirectory;
        static std::vector<User> GetComputerUsers();

    };
}