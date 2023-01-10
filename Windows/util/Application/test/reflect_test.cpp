#include <iostream>
#include <General/util/Reflection.h>

class ReflectClass :public Reflect {
public:
	ReflectClass() {}
	void Print() {
		printf("haha");
	}

	friend class Reflect;

	std::string ToString() override {
		return "ReflectClass";
	}
	static Reflect* CreateInstance() {
		return new ReflectClass();
	}
	static const ClassInfo* GetClassInfo() {
		return classInfo;
	}
protected:
	static ClassInfo* classInfo;

};

ClassInfo* ReflectClass::classInfo = new ClassInfo("ReflectClass", ReflectClass::CreateInstance);

class ReflectClass2 :public Reflect {
public:
	ReflectClass2() {}
	void Print() {
		printf("haha");
	}
	DLR_REFLECT(ReflectClass2)
};
IMP_REFLECT(ReflectClass2)

int main(int argc, char* argv[])
{

	ReflectClass2* ref = (ReflectClass2*)Reflect::CreateObject("ReflectClass2");
	ref->Print();

}

