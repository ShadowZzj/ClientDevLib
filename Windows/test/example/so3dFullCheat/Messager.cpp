#include <General/ThirdParty/cpp-httplib/httplib.h>
#include "Messager.h"
#include "GameManager.h"
#include <spdlog/spdlog.h>

nlohmann::json Messager::GetBasicInfo()
{
    nlohmann::json ret;
    GameManager::CLocalUser *localUser = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
    if (localUser == nullptr)
    {
        return ret;
    }

    if (gameManager.config.find("userid") != gameManager.config.end())
    {
        ret["userid"] = gameManager.config["userid"];
    }
    else
    {
        ret["userid"] = "default";
    }

    ret["username"] = localUser->loginUserName;
    ret["rolename"] = localUser->GetName();
    ret["money"]    = localUser->money;
    ret["hp"]       = localUser->GetCurrentHP();
    ret["mp"]       = localUser->GetCurrentMP();
    return ret;
}

int Messager::PostMoney()
{
    nlohmann::json postJson = GetBasicInfo();
    if (postJson.empty())
    {
        spdlog::error("PostMoney GetBasicInfo failed");
        return -1;
    }

    GameManager::CLocalUser *localUser = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
    if (localUser == nullptr)
    {
        spdlog::error("PostMoney GetLocalPlayerBase failed");
        return -1;
    }

    postJson["money"] = localUser->money;
    httplib::Client cli(serverDestination.c_str());
    // set timeout
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    cli.set_write_timeout(5, 0);

    auto res = cli.Post("/post_money", postJson.dump(), "application/json");
    if (res && res->status == 200)
    {
        spdlog::info("PostMoney success");
        return 0;
    }
    else
    {
        spdlog::error("PostMoney failed");
        return -1;
    }
}

int Messager::PostDieInfo()
{
    nlohmann::json postJson = GetBasicInfo();
    if (postJson.empty())
    {
        spdlog::error("PostDieInfo GetBasicInfo failed");
        return -1;
    }
    httplib::Client cli(serverDestination.c_str());
    // set timeout
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    cli.set_write_timeout(5, 0);

    auto res = cli.Post("/post_dieinfo", postJson.dump(), "application/json");
    if (res && res->status == 200)
    {
        spdlog::info("PostDieInfo success");
        return 0;
    }
    else
    {
        spdlog::error("PostDieInfo failed");
        return -1;
    }
}

int Messager::PostInfo()
{
    nlohmann::json postJson = GetBasicInfo();
    if (postJson.empty())
    {
        spdlog::error("PostDieInfo GetBasicInfo failed");
        return -1;
    }

    httplib::Client cli(serverDestination.c_str());
    // set timeout
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    cli.set_write_timeout(5, 0);

    auto res = cli.Post("/post_info", postJson.dump(), "application/json");
    if (res && res->status == 200)
    {
        spdlog::info("PostInfo success");
        return 0;
    }
    else
    {
        spdlog::error("PostInfo failed");
        return -1;
    }
}

void Messager::CardHandler()
{
    int failCount = 0;
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (gameManager.config.find("card") == gameManager.config.end())
        {
            spdlog::error("No card config");
            exit(0);
        }

        std::string card = gameManager.config["card"];
        httplib::Client cli(serverDestination.c_str());
        // set timeout
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(5, 0);
        cli.set_write_timeout(5, 0);

        auto res = cli.Get(fmt::format("/verify_card?card_uuid={}", card).c_str());
        if (res && res->status == 200)
        {
            auto json_response = nlohmann::json::parse(res->body);
            if (json_response["code"] == 200)
            {
                spdlog::info("Card is valid, register_time: {}, expiry_time: {}", json_response["register_time"],
                             json_response["expiry_time"]);
                failCount = 0; // Reset fail count on success
            }
            else if (json_response["code"] == 400)
            {
                spdlog::error("Card has expired");
                exit(0);
            }
            else if (json_response["code"] == 404)
            {
                spdlog::error("Card not found");
                exit(0);
            }
        }
        else
        {
            spdlog::error("Failed to connect to server or receive response, status code: {}", res ? res->status : 0);
            failCount++;
        }

        if (failCount >= 5)
        {
            spdlog::error("Failed to validate card 5 times in a row, exiting...");
            exit(0);
        }
    }
}
