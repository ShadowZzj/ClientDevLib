#ifndef _G_BASEUTIL_H_
#define _G_BASEUTIL_H_
#include <exception>
#define CONCAT_INTERNAL(x, y) x##y
#define CONCAT(x, y) CONCAT_INTERNAL(x, y)
#define DEFER const auto &CONCAT(defer__, __LINE__) = zzj::ExitScopeHelp() + [&]()
namespace zzj
{
// simulate Golang defer keyword??wrap ExitScope with lambda function??when exits its scope, the deconstruct function is
// called.
template <typename T, size_t N>
size_t GetDim(T(&array)[N]) {
	return N;
}

inline void CrashMe() {
	throw std::exception("CrashMe");
}
template <typename T> struct ExitScope
{
    T lambda;
    ExitScope(T lambda) : lambda(lambda)
    {
    }
    ~ExitScope()
    {
        lambda();
    }
    ExitScope(const ExitScope &);

  private:
    ExitScope &operator=(const ExitScope &);
};

class ExitScopeHelp
{
  public:
    template <typename T> ExitScope<T> operator+(T t)
    {
        return t;
    }
    ~ExitScopeHelp()
    {
    }
};

}; // namespace zzj
#endif
