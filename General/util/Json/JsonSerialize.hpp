#pragma once
#include <json.hpp>

namespace zzj
{
#define JSON_SERIALIZABLE \
    static constexpr auto properties = std::make_tuple
#define JSON_PROPERTY(Class, member) zzj::property(&Class::member, #member)
template <typename Class, typename T> struct PropertyImpl
{
    constexpr PropertyImpl(T Class::*aMember, const char *aName) : member{aMember}, name{aName}
    {
    }

    using Type = T;

    T Class::*member;
    const char *name;
};

template <typename Class, typename T> constexpr auto property(T Class::*member, const char *name)
{
    return PropertyImpl<Class, T>{member, name};
}

template <typename T, T... S, typename F> constexpr void for_sequence(std::integer_sequence<T, S...>, F &&f)
{
    (static_cast<void>(f(std::integral_constant<T, S>{})), ...);
}

// unserialize function
template <typename T> T fromJson(const nlohmann::json &data)
{
    T object;

    // We first get the number of properties
    constexpr auto nbProperties = std::tuple_size<decltype(T::properties)>::value;

    // We iterate on the index sequence of size `nbProperties`
    for_sequence(std::make_index_sequence<nbProperties>{}, [&](auto i) {
        // get the property
        constexpr auto property = std::get<i>(T::properties);

        // get the type of the property
        using Type = typename decltype(property)::Type;

        // set the value to the member
        // you can also replace `asAny` by `fromJson` to recursively serialize
        object.*(property.member) = fromJson<Type>(data[property.name]);
    });

    return object;
}

template <typename T,typename =int>
struct HasProperties : std::false_type { };

template <typename T>
struct HasProperties <T, decltype((void) T::properties, 0)> : std::true_type { };

template <typename T> nlohmann::json toJson(const T &object)
{
    nlohmann::json data;
    
    if constexpr (HasProperties<T>::value)
    {
        // We first get the number of properties
        constexpr auto nbProperties = std::tuple_size<decltype(T::properties)>::value;

        // We iterate on the index sequence of size `nbProperties`
        for_sequence(std::make_index_sequence<nbProperties>{}, [&](auto i) {
            // get the property
            constexpr auto property = std::get<i>(T::properties);

            // get the type of the property
            using Type = typename decltype(property)::Type;

            // set the value to the member
            // you can also replace `asAny` by `toJson` to recursively serialize
            data[property.name] = toJson<Type>(object.*(property.member));
        });
    }
    else
    {
        data = object;
    }
    return data;
}


}; // namespace zzj
