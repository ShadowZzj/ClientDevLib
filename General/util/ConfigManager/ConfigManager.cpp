#include "ConfigManager.h"
#include <sqlite3/sqlite3.h>
#include <sqlite3pp/sqlite3pp.h>
#include <sqlite3pp/sqlite3ppext.h>
#include <iostream>

ConfigManager::ConfigManager(std::string groupName, std::string moudleName)
{
	m_groupName = groupName;
	m_moudleName = moudleName;
	m_configBase = new ConfigBase(this);
	m_sqlLiteDBPtr = std::make_shared<sqlite3pp::database>(m_groupName.c_str(), SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
	std::string sql = CREATE_TABLE;
	sql.replace(sql.find("talbleName"), 10, m_moudleName.c_str());
	m_sqlLiteDBPtr->execute(sql.c_str());
}

ConfigManager::~ConfigManager()
{

}


ConfigBase& ConfigManager::operator[](std::string key)
{
	(*m_configBase)[key];
	return *m_configBase;
}

std::int32_t ConfigManager::BeginTransaction()
{
	return m_sqlLiteDBPtr->execute(SQL_BEGIN);
}

std::int32_t ConfigManager::CommitTransaction()
{
	return m_sqlLiteDBPtr->execute(SQL_COMMIT);
}

std::int32_t ConfigManager::RollBackTransaction()
{
	return m_sqlLiteDBPtr->execute(SQL_ROLLBACK);
}

std::string ConfigManager::GetErrorMessage()
{
	return m_sqlLiteDBPtr->error_msg();
}

std::shared_ptr<sqlite3pp::database> ConfigManager::GetDB()
{
    return m_sqlLiteDBPtr;
}

std::int32_t ConfigManager::GetConfig(std::string key, std::string& value)
{
	std::string sql = QUERY_DATA;
	sql.replace(sql.find("talbleName"), 10, m_moudleName.c_str());
	sql.replace(sql.find("keystring"), 9, key.c_str());
	sqlite3pp::query qry(*m_sqlLiteDBPtr.get(), sql.c_str());

	auto ret = qry.begin();
	value = (*ret).get<char const*>(0);
	return 0;
}

std::int32_t ConfigManager::SetConfig(std::string key, std::string value)
{
	if (QueryConfig(key))
		return _updateConfig(key, value);
	return _insertConfig(key, value);
}

std::int32_t ConfigManager::_insertConfig(std::string key, std::string value)
{
	std::string sql = INSERT_DATA;
	sql.replace(sql.find("talbleName"), 10, m_moudleName.c_str());
	sqlite3pp::command cmd(*m_sqlLiteDBPtr.get(), sql.c_str());
	cmd.bind(1, key.c_str(), sqlite3pp::nocopy);
	cmd.bind(2, value.c_str(), sqlite3pp::nocopy);
	return cmd.execute();
}

bool ConfigManager::QueryConfig(std::string key)
{
	std::string sql = QUERY_DATA;
	sql.replace(sql.find("talbleName"), 10, m_moudleName.c_str());
	sql.replace(sql.find("keystring"), 9, key.c_str());
	sqlite3pp::query qry(*m_sqlLiteDBPtr.get(), sql.c_str());

	if (qry.begin() == qry.end())
		return false;
	return true;
}

bool ConfigManager::DeleteConfig(std::string key)
{
    std::string sql = DELETE_DATA;
    sql.replace(sql.find("talbleName"), 10, m_moudleName.c_str());
    sql.replace(sql.find("keystring"), 9, key.c_str());

	return m_sqlLiteDBPtr->execute(sql.c_str());
}


std::int32_t ConfigManager::_updateConfig(std::string key, std::string value)
{
	std::string sql = UPDATE_DATA;
	sql.replace(sql.find("talbleName"), 10, m_moudleName.c_str());
	sql.replace(sql.find("keystring"), 9, key.c_str());
	sql.replace(sql.find("valuestring"), 11, value.c_str());
	return m_sqlLiteDBPtr->execute(sql.c_str());
}

ConfigBase::ConfigBase(ConfigManager* configManager)
{
	m_pConfigManager = configManager;
}

ConfigBase::~ConfigBase()
{
}

const nlohmann::json& ConfigBase::Get()
{
	return m_json;
}
