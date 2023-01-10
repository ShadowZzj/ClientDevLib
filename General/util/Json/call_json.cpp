#include "call_json.h"
#include <General/util/StrUtil.h>

using namespace zzj;
bool CallJ::AddMethod(method_template method)
{
	return AddMethod("nul", method);
}

bool CallJ::AddMethod(std::string name, method_template method)
{
	CallJ::MethodInfo param(name, method);

	for (auto pair : methods)
		if (pair.first == name || pair.second == method)
			return false;

	methods.push_back(param);
	return true;
}

CallJ::MethodInfo CallJ::GetMethod(int index, bool& success)
{
	if ((size_t)index >= methods.size()) {
		success = false;
		return CallJ::MethodInfo();
	}

	success = true;
	return methods[index];
}


bool CallJ::DeleteMethod(method_template method)
{
	for (auto i = methods.begin(); i != methods.end(); i++) {
		if (i->second == method) {
			methods.erase(i);
			return true;
		}
	}
	return false;
}

bool CallJ::DeleteMethod(std::string name)
{
	for (auto i = methods.begin(); i != methods.end(); i++) {
		if (i->first == name) {
			methods.erase(i);
			return true;
		}
	}
	return false;
}

bool CallJ::ExecCall(char* injson, int injsonlen, char** outjson, int* outjsonlen)
{
	try
	{
		json inj;
		nlohmann::detail::input_adapter inadapter(injson, (size_t)injsonlen);

		try
		{
			inj = json::parse(std::move(inadapter));
			std::string all = inj.dump();
			
		}
		catch (...)
		{
			
			return false;
		}

		auto methodIndex_iter = inj.find(METHODINDEX_STR);
		auto methodParam_iter = inj.find(METHODPARAM_STR);
		//check input param
		if (!isIndexParamValid(methodIndex_iter, methodParam_iter, inj))
			return false;

		int iMethodIndex = methodIndex_iter->get<int>();
		bool success;

		CallJ::MethodInfo mt = GetMethod(iMethodIndex, success);
		if (!success)
			return false;
		

		std::string outStr;
		outStr.clear();

		json iter= methodParam_iter->get<json>();
		std::string paramString = iter.dump();
		const char* params = paramString.c_str();
		

		std::tuple<int, std::string> stat;
		try {
			stat = mt.second(methodParam_iter, outStr);
		}
		catch (std::exception except) {
			
			std::get<0>(stat) = UnHandledException;
			std::get<1>(stat) =except.what();
		}

		std::string ret;
		ret.clear();

		try{
			json tmp;
			if (!outStr.empty())
				tmp = json::parse(outStr);
			SetRetJson(std::get<0>(stat), tmp, ret, std::get<1>(stat));
		}
		catch (...) {
			
			return false;
		}

		char *temp = ret.data() ? _strdup(ret.data()) : nullptr;
		if (temp == nullptr)
			return false;


		*outjson = temp;
		*outjsonlen = ret.size();
		
		return true;
	}
	catch (const nlohmann::detail::exception & nde)
	{
		
		return false;
	}
	catch (const std::exception & stde)
	{
		
		return false;
	}


}

void CallJ::SetRetJson(int iStatus, json& jmsg, std::string& retj, std::string errorMsg)
{
	json j;
	j[STATUS_STR] = iStatus;
	j[DATA] = jmsg;
	if (!errorMsg.empty())
		j[RETURNMSG_STR] = errorMsg;
	retj = j.dump();
}

bool CallJ::isIndexParamValid(nlohmann::json::iterator& indexIterator, nlohmann::json::iterator& paramIterator, json& inj)
{
	if (indexIterator == inj.end() ||
		paramIterator == inj.end())
	{
		return false;
	}
	if (!indexIterator->is_number_integer())
	{
		return false;
	}
	return true;
}

