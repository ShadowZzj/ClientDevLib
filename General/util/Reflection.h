#pragma once
#include <string>
#include <map>
struct ClassInfo;
class Reflect;
typedef Reflect* (*CtorFunc)(void);
extern std::map<std::string, ClassInfo*> *mapClass;
void RegisterClass(ClassInfo* classInfo);

struct ClassInfo {
	std::string name;
	CtorFunc ctor;
	ClassInfo(std::string _name, CtorFunc _ctor);
};


class Reflect {
public:
	friend struct ClassInfo;
	static Reflect* CreateObject(std::string object);
protected:
	virtual std::string ToString() = 0;
};

#define DLR_REFLECT(name) \
public:\
	friend class Reflect;\
	static const ClassInfo* GetClassInfo(){return classInfo;}\
protected: \
    std::string ToString() override{return #name;}\
	static Reflect* CreateInstance(){return new name();}\
	static ClassInfo* classInfo;

#define IMP_REFLECT(name) \
ClassInfo* name::classInfo=new ClassInfo("ReflectClass",ReflectClass::CreateInstance);