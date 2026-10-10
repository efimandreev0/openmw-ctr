#ifndef OPENMW_COMPONENTS_MYGUIPLATFORM_CTR_MYGUIPLATFORM_H
#define OPENMW_COMPONENTS_MYGUIPLATFORM_CTR_MYGUIPLATFORM_H

#include <filesystem>
#include <memory>
#include <string>

#include <components/vfs/pathutil.hpp>

namespace MyGUI
{
    class LogManager;
}
namespace VFS
{
    class Manager;
}

namespace MyGUIPlatform
{
    class RenderManager;
    class DataManager;
    class LogFacility;

    class Platform
    {
    public:
        Platform(const VFS::Manager* vfs, float uiScalingFactor, VFS::Path::NormalizedView resourcePath,
            const std::filesystem::path& logName = "MyGUI.log");
        ~Platform();

        void shutdown();

        RenderManager* getRenderManagerPtr();
        DataManager* getDataManagerPtr();

    private:
        std::unique_ptr<LogFacility> mLogFacility;
        std::unique_ptr<MyGUI::LogManager> mLogManager;
        std::unique_ptr<DataManager> mDataManager;
        std::unique_ptr<RenderManager> mRenderManager;
    };
}

#endif
