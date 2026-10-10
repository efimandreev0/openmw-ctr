
#include <components/lua_ui/adapter.hpp>
#include <components/lua_ui/input.hpp>
#include <components/lua_ui/scriptsettings.hpp>
#include <components/lua_ui/util.hpp>

#include <MyGUI_FactoryManager.h>

namespace LuaUi
{
    struct Element
    {
    };

    class LuaContainer
    {
    };

    LuaAdapter::LuaAdapter()
        : mElement(nullptr)
        , mContainer(nullptr)
    {
    }

    void LuaAdapter::attach(const std::shared_ptr<Element>& element)
    {
        mElement = element;
    }

    void LuaAdapter::detach()
    {
        mElement = nullptr;
    }

    void registerAllWidgets()
    {
        MyGUI::FactoryManager::getInstance().registerFactory<LuaAdapter>("Widget");
    }

    const std::unordered_map<std::string, std::string>& widgetTypeToName()
    {
        static const std::unordered_map<std::string, std::string> types;
        return types;
    }

    void clearGameInterface() {}
    void clearMenuInterface() {}
    void updateAllElementCoords() {}

    void dispatchMouseWheel(MyGUI::Widget*, float, float, float, float) {}

    size_t scriptSettingsPageCount()
    {
        return 0;
    }

    ScriptSettingsPage scriptSettingsPageAt(size_t)
    {
        return {};
    }

    void ensureScriptSettingsPage(size_t) {}
    void attachPageAt(size_t, LuaAdapter*) {}
}
