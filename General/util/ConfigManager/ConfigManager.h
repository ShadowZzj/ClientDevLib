#ifndef _G_CONFIGMANAGER_H_
#define _G_CONFIGMANAGER_H_

#include <memory>
#include <string>
#include <json.hpp>


#define SQLITE_IS_OK(code) SQLITE_OK == code

namespace sqlite3pp
{
	class database;
};

class ConfigBase;
class ConfigManager
{
public:
	ConfigManager(std::string groupName, std::string moudleName);
	~ConfigManager();
	ConfigBase& operator[] (std::string key);

	std::int32_t BeginTransaction();
	std::int32_t CommitTransaction();
	std::int32_t RollBackTransaction();

	std::string GetErrorMessage();
    std::shared_ptr<sqlite3pp::database> GetDB();

	std::int32_t SetConfig(std::string key, std::string value);
	std::int32_t GetConfig(std::string key, std::string &value);
    bool QueryConfig(std::string key);
    bool DeleteConfig(std::string key);

protected:
	std::int32_t _insertConfig(std::string key, std::string value);
	std::int32_t _updateConfig(std::string key, std::string value);

private:
	std::string m_groupName;
	std::string m_moudleName;
	std::shared_ptr<sqlite3pp::database> m_sqlLiteDBPtr;
	ConfigBase* m_configBase;
	
};

class ConfigBase
{
public:
	ConfigBase(ConfigManager* configManager);
	~ConfigBase();

	const nlohmann::json& Get();

	template <typename T>
	ConfigBase& operator[] (T key);



	template <typename T>
	void operator= (T dst);

	template <typename ValueType>
	operator ValueType()
	{
		
		std::string value;
		std::string key = m_key;
		m_key.clear();
		m_json.clear();
		m_pConfigManager->GetConfig(key, value);
		m_json = nlohmann::json::parse(value);
		return m_json[key];
	}

private:
	nlohmann::json m_json;
	std::string m_key;
	ConfigManager* m_pConfigManager;
};

template <typename T>
ConfigBase& ConfigBase::operator[](T key)
{
	m_key.append(key);
	m_key.append(":");
	return *this;
}

template <typename T>
void ConfigBase::operator=(T dst)
{
	m_json[m_key] = dst;
	m_pConfigManager->SetConfig(m_key, m_json.dump());
	m_key.clear();
	m_json.clear();
}




constexpr auto CREATE_TABLE = "CREATE TABLE talbleName"
"(  "
"id INTEGER PRIMARY KEY     AUTOINCREMENT,"
"key TEXT,"
"value TEXT);";

constexpr auto INSERT_DATA = " INSERT INTO talbleName (key, value) VALUES (?, ?) ";
constexpr auto QUERY_DATA = " SELECT value FROM talbleName where key = 'keystring'";
constexpr auto DELETE_DATA = " DELETE FROM talbleName where key = 'keystring'";
constexpr auto UPDATE_DATA = " UPDATE talbleName SET value = 'valuestring' where key = 'keystring'";


constexpr auto SQL_BEGIN = "BEGIN;";
constexpr auto SQL_COMMIT = "COMMIT;";
constexpr auto SQL_ROLLBACK = "ROLLBACK;";

#endif