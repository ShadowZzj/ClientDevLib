#pragma once
#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <wuapi.h>
#include <atlbase.h>
#include <General/util/LuaExport.hpp>
namespace zzj
{
	namespace WindowsUpdate {
		class UpdateInfo
		{
		public:
			std::optional<std::string> title;
			std::optional<bool> autoSeleteOnWebSites;
			std::optional<bool> canRequireSource;
			std::optional<double> deadLine;
			std::optional<std::string> description;
			
			bool isDownloaded;
			bool isHidden;
			bool isInstalled;
			bool isMandatory;
			bool isUninstallable;
			UpdateType type;
			std::vector<UpdateInfo> bundledUpdates;
			std::vector<std::string> kbArticleIDs;
			static std::tuple<int,std::vector<UpdateInfo>> GetUpdateInfo(const std::string& criteria);
            DECLARE_LUA_EXPORT(UpdateInfo)

          private:
            static UpdateInfo GetUpdateInfoFromUpdateItem(CComPtr<IUpdate> updateItem);
		};
	};
};