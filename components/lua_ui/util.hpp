#ifndef OPENMW_LUAUI_WIDGETLIST
#define OPENMW_LUAUI_WIDGETLIST

#ifndef OPENMW_NO_LUA
#include <sol/table.hpp>
#endif
#include <string>
#include <unordered_map>
#include <vector>

namespace LuaUi
{
    void registerAllWidgets();

    const std::unordered_map<std::string, std::string>& widgetTypeToName();

    void clearGameInterface();
    void clearMenuInterface();
    void updateAllElementCoords();

#ifndef OPENMW_NO_LUA
    bool warnUnused(std::vector<std::string>& warnings, sol::object table, const std::string& tableName,
        const std::vector<std::string_view>& usedKeys, bool generateWarningStrings);
#endif
}

#endif // OPENMW_LUAUI_WIDGETLIST
