#ifndef OPENMW_COMPONENTS_MISC_STRINGS_HETEROGENEOUS_HPP
#define OPENMW_COMPONENTS_MISC_STRINGS_HETEROGENEOUS_HPP

// GCC 10 does not support C++20 heterogeneous lookup on unordered containers.
// These helpers convert string_view to string before map/set lookup.

#include <string>
#include <string_view>

namespace Misc
{
    template <typename Container>
    auto heterogeneousFind(Container& container, std::string_view key)
        -> decltype(container.find(std::string(key)))
    {
        return container.find(std::string(key));
    }

    template <typename Container>
    bool heterogeneousContains(const Container& container, std::string_view key)
    {
        return container.find(std::string(key)) != container.end();
    }
}

#endif
