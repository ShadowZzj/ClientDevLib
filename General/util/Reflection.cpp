#include "Reflection.h"
#include <map>

using namespace std;
std::map<string, ClassInfo*> *mapClass;

ClassInfo::ClassInfo(std::string _name, CtorFunc _ctor) {
	name = _name;
	ctor = _ctor;
	if (!mapClass)
		mapClass = new std::map<std::string, ClassInfo*>();
	RegisterClass(this);
}

Reflect* Reflect::CreateObject(std::string object) {
	auto iter = mapClass->find(object);
	if (iter == mapClass->end()) {
		return (Reflect*)nullptr;
	}

	return iter->second->ctor();
}

void RegisterClass(ClassInfo* classInfo) {
	if (!classInfo)
		return;
	(*mapClass)[classInfo->name] = classInfo;
}