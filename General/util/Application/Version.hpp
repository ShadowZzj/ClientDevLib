#pragma once
#include <boost/algorithm/string.hpp>
#include <string>

namespace zzj
{
class Version
{
  public:
    Version(const std::string &version, std::string delimiter = ".")
    {
        this->delimiter = delimiter;
        this->version   = version;
    }

    bool operator<(const Version &other) const
    {
        std::vector<std::string> version1Vec;
        boost::algorithm::split(version1Vec, version, boost::is_any_of(delimiter));
        std::vector<std::string> version2Vec;
        boost::algorithm::split(version2Vec, other.version, boost::is_any_of(other.delimiter));

        size_t maxSize = version1Vec.size() > version2Vec.size() ? version1Vec.size() : version2Vec.size();

        for (size_t i = 0; i < maxSize; i++)
        {
            int v1 = i < version1Vec.size() ? std::stoi(version1Vec[i]) : 0;
            int v2 = i < version2Vec.size() ? std::stoi(version2Vec[i]) : 0;

            if (v1 < v2)
            {
                return true;
            }
            else if (v1 > v2)
            {
                return false;
            }
        }
        return false;
    }
    bool operator==(const Version &other) const
    {
        std::vector<std::string> version1Vec;
        boost::algorithm::split(version1Vec, version, boost::is_any_of(delimiter));
        std::vector<std::string> version2Vec;
        boost::algorithm::split(version2Vec, other.version, boost::is_any_of(other.delimiter));

        size_t maxSize = version1Vec.size() > version2Vec.size() ? version1Vec.size() : version2Vec.size();

        for (size_t i = 0; i < maxSize; i++)
        {
            int v1 = i < version1Vec.size() ? std::stoi(version1Vec[i]) : 0;
            int v2 = i < version2Vec.size() ? std::stoi(version2Vec[i]) : 0;

            if (v1 != v2)
            {
                return false;
            }
        }
        return true;
    }
    bool operator>(const Version &other) const
    {
        return !(*this < other) && !(*this == other);
    }
    bool operator<=(const Version &other) const
    {
        return *this < other || *this == other;
    }
    bool operator>=(const Version &other) const
    {
        return *this > other || *this == other;
    }

  private:
    std::string version;
    std::string delimiter;
};
} // namespace zzj