#include <components/debug/debugging.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#ifndef OPENMW_CTR_NATIVE
#include <components/misc/osgpluginchecker.hpp>
#endif
#include <components/misc/rng.hpp>
#include <components/platform/platform.hpp>
#include <components/version/version.hpp>

#include "mwgui/debugwindow.hpp"

#include "engine.hpp"
#include "options.hpp"

#include <boost/program_options/variables_map.hpp>

#if defined(_WIN32)
#include <components/misc/windows.hpp>
// makes __argc and __argv available on windows
#include <cstdlib>

extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
#endif

#include <filesystem>

#if (defined(__APPLE__) || defined(__linux) || defined(__unix) || defined(__posix))
#include <unistd.h>
#endif

#if defined(__vita__) || defined(__3DS__)
#include "vita/VitaInit.h"
#endif
#ifdef __vita__
// Force the linker to keep sceUserMainThreadStackSize in the binary.
// With -fdata-sections + --gc-sections, the symbol gets its own section
// which the linker strips because it can't see the Vita loader's reference.
// This extern declaration + volatile read in main() creates a GC root.
extern "C" unsigned int sceUserMainThreadStackSize;
#endif

/**
 * \brief Parses application command line and calls \ref Cfg::ConfigurationManager
 * to parse configuration files.
 *
 * Results are directly written to \ref Engine class.
 *
 * \retval true - Everything goes OK
 * \retval false - Error
 */
bool parseOptions(int argc, char** argv, OMW::Engine& engine, Files::ConfigurationManager& cfgMgr)
{
#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() enter");
#endif
    // Create a local alias for brevity
    namespace bpo = boost::program_options;
    typedef std::vector<std::string> StringsVector;

    bpo::options_description desc = OpenMW::makeOptionsDescription();
    bpo::variables_map variables;

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() desc built");
#endif

    Files::parseArgs(argc, argv, variables, desc);
    bpo::notify(variables);

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() args parsed");
#endif

    if (variables.count("help"))
    {
        Debug::getRawStdout() << desc << std::endl;
        return false;
    }

    if (variables.count("version"))
    {
        Debug::getRawStdout() << Version::getOpenmwVersionDescription() << std::endl;
        return false;
    }

#if defined(__3DS__)
    // 3DS has no CWD concept; use romfs:/ (CIA read-only mount) as the base path
    cfgMgr.processPaths(variables, std::filesystem::path("romfs:/"));
    Vita::breadcrumb("parseOptions() processPaths done");
#elif defined(__vita__)
    // Vita has no CWD concept; use app0: (VPK root) as the base path
    cfgMgr.processPaths(variables, std::filesystem::path("app0:"));
    Vita::breadcrumb("parseOptions() processPaths done");
#else
    cfgMgr.processPaths(variables, std::filesystem::current_path());
#endif

    cfgMgr.readConfiguration(variables, desc);

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() readConfiguration done");
#endif

    Debug::setupLogging(cfgMgr.getLogPath(), "OpenMW");
    Log(Debug::Info) << Version::getOpenmwVersionDescription();

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() logging setup done");
#endif

    Settings::Manager::load(cfgMgr);

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() settings loaded");
#endif

#if defined(__vita__) || defined(__3DS__)
    Vita::applySettingsOverrides();
#endif

    MWGui::DebugWindow::startLogRecording();

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() startLogRecording done");
#endif

    engine.setGrabMouse(!variables["no-grab"].as<bool>());

    // Font encoding settings
    std::string encoding(variables["encoding"].as<std::string>());
    Log(Debug::Info) << ToUTF8::encodingUsingMessage(encoding);
    engine.setEncoding(ToUTF8::calculateEncoding(encoding));

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() encoding set");
#endif

    Files::PathContainer dataDirs(asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>()));

    Files::PathContainer::value_type local(variables["data-local"]
                                               .as<Files::MaybeQuotedPathContainer::value_type>()
                                               .u8string()); // This call to u8string is redundant, but required to
                                                             // build on MSVC 14.26 due to implementation bugs.
    if (!local.empty())
        dataDirs.push_back(std::move(local));

    cfgMgr.filterOutNonExistingPaths(dataDirs);

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() dataDirs resolved");
#endif

    engine.setResourceDir(variables["resources"]
                              .as<Files::MaybeQuotedPath>()
                              .u8string()); // This call to u8string is redundant, but required to build on MSVC 14.26
                                            // due to implementation bugs.
    engine.setDataDirs(dataDirs);

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() resourceDir and dataDirs set");
#endif

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() about to read fallback-archive");
#endif

    // fallback archives
    StringsVector archives = variables["fallback-archive"].as<StringsVector>();
#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() fallback-archive read OK");
#endif
    for (StringsVector::const_iterator it = archives.begin(); it != archives.end(); ++it)
    {
        engine.addArchive(*it);
    }

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() archives added");
#endif

    StringsVector content = variables["content"].as<StringsVector>();
    if (content.empty())
    {
        Log(Debug::Error) << "No content file given (esm/esp, nor omwgame/omwaddon). Aborting...";
        return false;
    }
#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() content read OK");
#endif
    engine.addContentFile("builtin.omwscripts");
#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() builtin.omwscripts added");
#endif
    std::set<std::string> contentDedupe{ "builtin.omwscripts" };
    for (const auto& contentFile : content)
    {
        if (!contentDedupe.insert(contentFile).second)
        {
            Log(Debug::Error) << "Content file specified more than once: " << contentFile << ". Aborting...";
            return false;
        }
    }

    for (auto& file : content)
    {
        engine.addContentFile(file);
    }

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() content files added");
#endif

    StringsVector groundcover = variables["groundcover"].as<StringsVector>();
#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() groundcover read OK");
#endif
    for (auto& file : groundcover)
    {
        engine.addGroundcoverFile(file);
    }

    if (variables.count("lua-scripts"))
    {
        Log(Debug::Warning) << "Lua scripts have been specified via the old lua-scripts option and will not be loaded. "
                               "Please update them to a version which uses the new omwscripts format.";
    }

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() about to set startup options");
#endif

    // startup-settings
    engine.setCell(variables["start"].as<std::string>());
#ifdef __3DS__
    // boost::program_options ignores bool_switch options in config files and
    // CIA titles have no command line — a flag file on the SD card is the
    // pragmatic "start a new game immediately" switch (drop it to boot
    // straight into chargen; delete it to get the menu back).
    {
        FILE* flag = fopen("sdmc:/3ds/openmw/newgame.flag", "rb");
        if (flag)
        {
            fclose(flag);
            Vita::breadcrumb("newgame.flag found - skipping menu");
            engine.setSkipMenu(true, true);
        }
        else
            engine.setSkipMenu(variables["skip-menu"].as<bool>(), variables["new-game"].as<bool>());
    }
#else
    engine.setSkipMenu(variables["skip-menu"].as<bool>(), variables["new-game"].as<bool>());
#endif
    if (!variables["skip-menu"].as<bool>() && variables["new-game"].as<bool>())
        Log(Debug::Warning) << "Warning: new-game used without skip-menu -> ignoring it";

    // scripts
    engine.setCompileAll(variables["script-all"].as<bool>());
    engine.setCompileAllDialogue(variables["script-all-dialogue"].as<bool>());
    engine.setScriptConsoleMode(variables["script-console"].as<bool>());
    engine.setStartupScript(variables["script-run"].as<std::string>());
    engine.setWarningsMode(variables["script-warn"].as<int>());
    engine.setSaveGameFile(variables["load-savegame"].as<Files::MaybeQuotedPath>().u8string());

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("parseOptions() content and scripts set");
#endif

    // other settings
    Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);
    // 3DS: portlibs OpenAL ships the ndsp backend; needs dspfirm.cdc at
    // sdmc:/3ds/ (falls back to the "No Output" null device without it).
    engine.setSoundUsage(!variables["no-sound"].as<bool>());
    engine.setActivationDistanceOverride(variables["activate-dist"].as<int>());
    engine.enableFontExport(variables["export-fonts"].as<bool>());
    engine.setRandomSeed(variables["random-seed"].as<unsigned int>());

    return true;
}

#ifndef OPENMW_CTR_NATIVE
namespace
{
    class OSGLogHandler : public osg::NotifyHandler
    {
        void notify(osg::NotifySeverity severity, const char* msg) override
        {
            // Copy, because osg logging is not thread safe.
            std::string msgCopy(msg);
            if (msgCopy.empty())
                return;

            Debug::Level level;
            switch (severity)
            {
                case osg::ALWAYS:
                case osg::FATAL:
                    level = Debug::Error;
                    break;
                case osg::WARN:
                case osg::NOTICE:
                    level = Debug::Warning;
                    break;
                case osg::INFO:
                    level = Debug::Info;
                    break;
                case osg::DEBUG_INFO:
                case osg::DEBUG_FP:
                default:
                    level = Debug::Debug;
            }
            std::string_view s(msgCopy);
            if (s.size() < 1024)
                Log(level) << (s.back() == '\n' ? s.substr(0, s.size() - 1) : s);
            else
            {
                while (!s.empty())
                {
                    size_t lineSize = 1;
                    while (lineSize < s.size() && s[lineSize - 1] != '\n')
                        lineSize++;
                    Log(level) << s.substr(0, s[lineSize - 1] == '\n' ? lineSize - 1 : lineSize);
                    s = s.substr(lineSize);
                }
            }
        }
    };
}
#endif

int runApplication(int argc, char* argv[])
{
#if defined(__vita__) || defined(__3DS__)
    Vita::initialize();
    Vita::breadcrumb("runApplication() enter");
#endif
    Platform::init();

#ifdef __APPLE__
    setenv("OSG_GL_TEXTURE_STORAGE", "OFF", 0);
#endif

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("Platform::init() done");
#endif

#ifndef OPENMW_CTR_NATIVE
    osg::setNotifyHandler(new OSGLogHandler());
#endif
    Files::ConfigurationManager cfgMgr;

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("ConfigurationManager created");
#endif

    std::unique_ptr<OMW::Engine> engine = std::make_unique<OMW::Engine>(cfgMgr);

#if defined(__vita__) || defined(__3DS__)
    Vita::breadcrumb("Engine created");
#endif

    engine->setRecastMaxLogLevel(Debug::getRecastMaxLogLevel());

    if (parseOptions(argc, argv, *engine, cfgMgr))
    {
#if defined(__vita__) || defined(__3DS__)
        Vita::breadcrumb("parseOptions() done, checking SELECT");
        engine->setForceRescan(Vita::isSelectHeld());
        Vita::breadcrumb("parseOptions() done, starting engine");
#endif
#ifndef OPENMW_CTR_NATIVE
        if (!Misc::checkRequiredOSGPluginsArePresent())
            return 1;
#endif

        engine->go();
    }

    return 0;
}

#if defined(ANDROID)
extern "C" int SDL_main(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
#ifdef __vita__
    // Volatile read prevents the compiler from optimizing this away,
    // which keeps the .data section containing sceUserMainThreadStackSize
    // reachable from main() — a GC root — preventing --gc-sections from
    // stripping it.  The Vita loader reads this symbol to set the main
    // thread's stack size (default is only 256KB, we need 2MB).
    (void)*(volatile unsigned int*)&sceUserMainThreadStackSize;
#endif
#ifdef __3DS__
    // CIA titles launched from the HOME menu get argc=0/argv=NULL.
    // boost::program_options builds a vector from argv+1..argv+argc — with a
    // null argv that's a garbage range. Synthesize a minimal argv.
    static char s_programName[] = "openmw";
    static char* s_fakeArgv[] = { s_programName, nullptr };
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr)
    {
        argc = 1;
        argv = s_fakeArgv;
    }
#endif
    return Debug::wrapApplication(&runApplication, argc, argv, "OpenMW");
}

// Platform specific for Windows when there is no console built into the executable.
// Windows will call the WinMain function instead of main in this case, the normal
// main function is then called with the __argc and __argv parameters.
#if defined(_WIN32) && !defined(_CONSOLE)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    return main(__argc, __argv);
}
#endif
