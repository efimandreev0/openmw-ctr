
#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <3ds.h>

#include <osg/Stats>

#include <components/compiler/extensions0.hpp>
#include <components/ctr/gpu/ctrgpu.hpp>
#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/l10n/manager.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#include <components/misc/frameratelimiter.hpp>
#include <components/misc/rng.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/settings/values.hpp>
#include <components/version/version.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include "ctr/ctrframe.hpp"
#include "mwrender/ctr/context.hpp"
#include "vita/VitaInit.h"

#include "mwinput/inputmanagerimp.hpp"

#include "mwgui/windowmanagerimp.hpp"

#include "nolua/nullluamanager.hpp"

#include "mwscript/interpretercontext.hpp"
#include "mwscript/scriptmanagerimp.hpp"

#include "mwsound/constants.hpp"
#include "mwsound/soundmanagerimp.hpp"

#include "mwworld/class.hpp"
#include "mwworld/datetimemanager.hpp"
#include "mwworld/worldimp.hpp"

#include "mwrender/renderingmanager.hpp"

#include "mwclass/classes.hpp"

#include "mwdialogue/dialoguemanagerimp.hpp"
#include "mwdialogue/journalimp.hpp"
#include "mwdialogue/scripttest.hpp"

#include "mwmechanics/mechanicsmanagerimp.hpp"

#include "mwstate/statemanagerimp.hpp"

extern "C"
{
    extern volatile int g_ctrFrameNo;
    extern const char* volatile g_ctrBeacon;
    void ctrSetThreadSpawnHint(int core, int prio, size_t minStack);
    void ctrDetachConsole(void);
    void ctrClearThreadSpawnHint(void);
    void vitaBreadcrumb(const char* msg);
}

namespace
{
    enum Phase
    {
        Phase_Input,
        Phase_Sound,
        Phase_State,
        Phase_Scripts,
        Phase_Mechanics,
        Phase_Physics,
        Phase_World,
        Phase_Gui,
        Phase_GpuWait,
        Phase_Scene,
        Phase_GuiDraw,
        Phase_Count
    };

    constexpr const char* sPhaseNames[Phase_Count] = { "input", "sound", "lua+state", "scripts", "mechanics",
        "physics", "world", "gui", "gpu-wait", "scene", "gui-draw" };

    struct FrameProfile
    {
        bool mActive = false;
        bool mDiscard = false;
        u64 mMark = 0;
        u64 mFrame[Phase_Count] = {};
        u64 mTicks[Phase_Count] = {};
        u64 mFrameStart = 0;
        u64 mFrameTicks = 0;
        u64 mDraws = 0;
        u64 mTriangles = 0;
        int mFrames = 0;
        int mSkipped = 0;

        void begin()
        {
            mActive = true;
            mDiscard = false;
            std::fill(std::begin(mFrame), std::end(mFrame), 0);
            mFrameStart = mMark = svcGetSystemTick();
        }

        void mark(Phase phase)
        {
            if (!mActive)
                return;
            const u64 now = svcGetSystemTick();
            mFrame[phase] += now - mMark;
            mMark = now;
        }

        void discard()
        {
            if (mActive)
                mDiscard = true;
        }

        void end()
        {
            if (!mActive)
                return;
            mActive = false;
            if (mDiscard)
            {
                ++mSkipped;
                return;
            }
            for (int i = 0; i < Phase_Count; ++i)
                mTicks[i] += mFrame[i];
            mFrameTicks += svcGetSystemTick() - mFrameStart;
            if (MWRender::CtrContext* context = MWRender::CtrContext::get())
            {
                mDraws += context->getRenderer().getScreenStats().mDraws;
                mTriangles += context->getRenderer().getScreenStats().mTriangles;
            }
            if (++mFrames < 300)
                return;
            const double msPerTick = 1000.0 / SYSCLOCK_ARM11 / mFrames;
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "[Perf] %d frames, avg %.1f ms:", mFrames, mFrameTicks * msPerTick);
            std::string line = buffer;
            for (int i = 0; i < Phase_Count; ++i)
            {
                std::snprintf(buffer, sizeof(buffer), " %s %.1f", sPhaseNames[i], mTicks[i] * msPerTick);
                line += buffer;
            }
            std::snprintf(buffer, sizeof(buffer), " | draws %llu, triangles %llu",
                static_cast<unsigned long long>(mDraws / mFrames), static_cast<unsigned long long>(mTriangles / mFrames));
            line += buffer;
            if (mSkipped > 0)
            {
                std::snprintf(buffer, sizeof(buffer), " (%d loading frames left out)", mSkipped);
                line += buffer;
            }
            Log(Debug::Info) << line;
            vitaBreadcrumb(line.c_str());
            *this = FrameProfile();
        }
    };

    FrameProfile sProfile;
}

void OMW::Engine::executeLocalScripts()
{
    MWWorld::LocalScripts& localScripts = mWorld->getLocalScripts();

    localScripts.startIteration();
    std::pair<ESM::RefId, MWWorld::Ptr> script;
    while (localScripts.getNext(script))
    {
        MWScript::InterpreterContext interpreterContext(&script.second.getRefData().getLocals(), script.second);
        mScriptManager->run(script.first, interpreterContext);
    }
}

void OMW::Engine::present(bool drawScene)
{
    if (!CtrGpu::isInitialised())
        return;

    g_ctrBeacon = "present";
    sProfile.mark(Phase_Gui);
    CtrGpu::beginFrame();
    sProfile.mark(Phase_GpuWait);

    const bool sceneVisible = drawScene && mWorld != nullptr && mStateManager != nullptr
        && mStateManager->getState() != MWBase::StateManager::State_NoGame
        && (mWindowManager == nullptr || mWindowManager->isSceneEnabled());
    if (sceneVisible)
    {
        mWorld->getRenderingManager()->draw();
        sProfile.mark(Phase_Scene);
    }
    else
    {
        C3D_RenderTarget* top = CtrGpu::getTopTarget();
        C3D_RenderTargetClear(top, C3D_CLEAR_ALL, 0x000000FF, 0);
        C3D_FrameDrawOn(top);
    }

    if (mWindowManager != nullptr)
    {
        C3D_FrameDrawOn(CtrGpu::getTopTarget());
        mWindowManager->renderGui(false);

        if (mWindowManager->hasBottomScreen() && g_ctrFrameNo > 0)
        {
            static bool consoleDetached = false;
            if (!consoleDetached)
            {
                ctrDetachConsole();
                consoleDetached = true;
            }
            C3D_RenderTarget* bottom = CtrGpu::getBottomTarget();
            C3D_RenderTargetClear(bottom, C3D_CLEAR_COLOR, 0x000000FF, 0);
            C3D_FrameDrawOn(bottom);
            mWindowManager->renderGui(true);
        }
    }

    sProfile.mark(Phase_GuiDraw);
    CtrGpu::endFrame();
    sProfile.mark(Phase_GpuWait);
    g_ctrBeacon = "present-done";
}

bool OMW::Engine::frame(unsigned frameNumber, float frametime)
{
    ++g_ctrFrameNo;
    g_ctrBeacon = "frame-begin";
    if (g_ctrFrameNo % 300 == 1 && g_ctrFrameNo < 6000)
    {
        char label[32];
        std::snprintf(label, sizeof(label), "frame %d", static_cast<int>(g_ctrFrameNo));
        Vita::logMemoryStatus(label);
    }

    mEnvironment.setFrameDuration(frametime);
    sProfile.begin();

    try
    {
        mInputManager->update(frametime, false);
        sProfile.mark(Phase_Input);

        if (!aptMainLoop())
        {
            mStateManager->requestQuit();
            return false;
        }

        if (!mWindowManager->isWindowVisible())
        {
            mSoundManager->pausePlayback();
            return false;
        }
        mSoundManager->resumePlayback();

        if (mUseSound)
            mSoundManager->update(frametime);
        sProfile.mark(Phase_Sound);

        mLuaManager->update(frametime);

        mStateManager->update(frametime);

        bool paused = mWorld->getTimeManager()->isPaused();
        sProfile.mark(Phase_State);

        g_ctrBeacon = "scripts";
        if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
        {
            if (!mWindowManager->containsMode(MWGui::GM_MainMenu) || !paused)
            {
                if (mWorld->getScriptsEnabled())
                {
                    executeLocalScripts();

                    mScriptManager->getGlobalScripts().run();
                }

                mWorld->getWorldScene().markCellAsUnchanged();
            }

            if (!paused)
            {
                double hours = (frametime * mWorld->getTimeManager()->getGameTimeScale()) / 3600.0;
                mWorld->advanceTime(hours, true);
                mWorld->rechargeItems(frametime, true);
            }
        }

        sProfile.mark(Phase_Scripts);

        g_ctrBeacon = "mechanics";
        if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            mMechanicsManager->update(frametime, paused);

        if (mStateManager->getState() == MWBase::StateManager::State_Running)
        {
            MWWorld::Ptr player = mWorld->getPlayerPtr();
            if (!paused && player.getClass().getCreatureStats(player).isDead())
                mStateManager->endGame();
        }

        static const osg::ref_ptr<osg::Stats> stats = new osg::Stats("ctr", 1);

        sProfile.mark(Phase_Mechanics);

        g_ctrBeacon = "physics";
        if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            mWorld->updatePhysics(frametime, paused, 0, frameNumber, *stats);

        sProfile.mark(Phase_Physics);

        g_ctrBeacon = "world";
        if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            mWorld->update(frametime, paused);

        sProfile.mark(Phase_World);

        g_ctrBeacon = "gui";
        mWindowManager->update(frametime);

        mWorld->updateFocusObject();
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "Error in frame: " << e.what();
    }

    g_ctrBeacon = "render";
    present(true);
    sProfile.end();
    return true;
}

OMW::Engine::Engine(Files::ConfigurationManager& configurationManager)
    : mWindow(nullptr)
    , mEncoding(ToUTF8::WINDOWS_1252)
    , mSkipMenu(false)
    , mUseSound(true)
    , mCompileAll(false)
    , mCompileAllDialogue(false)
    , mWarningsMode(1)
    , mScriptConsoleMode(false)
    , mActivationDistanceOverride(-1)
    , mGrab(true)
    , mExportFonts(false)
    , mRandomSeed(0)
    , mNewGame(false)
    , mCfgMgr(configurationManager)
    , mGlMaxTextureImageUnits(0)
{
}

OMW::Engine::~Engine()
{
    CtrFrame::setPresenter(nullptr);

    mMechanicsManager = nullptr;
    mDialogueManager = nullptr;
    mJournal = nullptr;
    mWindowManager = nullptr;
    mScriptManager = nullptr;
    mWorld = nullptr;
    mSoundManager = nullptr;
    mInputManager = nullptr;
    mStateManager = nullptr;
    mLuaManager = nullptr;
    mL10nManager = nullptr;

    mScriptContext = nullptr;

    mWorkQueue = nullptr;

    mResourceSystem.reset();

    mEncoder = nullptr;

    CtrGpu::shutdown();

    Log(Debug::Info) << "Quitting peacefully.";
}

void OMW::Engine::setDataDirs(const Files::PathContainer& dataDirs)
{
    mDataDirs = dataDirs;
    mDataDirs.insert(mDataDirs.begin(), mResDir / "vfs");
    mFileCollections = Files::Collections(mDataDirs);
}

void OMW::Engine::addArchive(const std::string& archive)
{
    mArchives.push_back(archive);
}

void OMW::Engine::setResourceDir(const std::filesystem::path& parResDir)
{
    mResDir = parResDir;
    if (!Version::checkResourcesVersion(mResDir))
        Log(Debug::Error) << "Resources dir " << mResDir
                          << " doesn't match OpenMW binary, the game may work incorrectly.";
}

void OMW::Engine::setCell(const std::string& cellName)
{
    mCellName = cellName;
}

void OMW::Engine::addContentFile(const std::string& file)
{
    mContentFiles.push_back(file);
}

void OMW::Engine::addGroundcoverFile(const std::string& file)
{
    mGroundcoverFiles.emplace_back(file);
}

void OMW::Engine::setSkipMenu(bool skipMenu, bool newGame)
{
    mSkipMenu = skipMenu;
    mNewGame = newGame;
}

void OMW::Engine::createWindow()
{
    if (!CtrGpu::isInitialised() && !CtrGpu::init())
        throw std::runtime_error("citro3d initialisation failed");
}

void OMW::Engine::setWindowIcon() {}

void OMW::Engine::prepareEngine()
{
    mStateManager = std::make_unique<MWState::StateManager>(mCfgMgr.getUserDataPath() / "saves", mContentFiles);
    mEnvironment.setStateManager(*mStateManager);

    createWindow();

    mVFS = std::make_unique<VFS::Manager>();

    VFS::registerArchives(mVFS.get(), mFileCollections, mArchives, true, &mEncoder.get()->getStatelessEncoder());

    mResourceSystem = std::make_unique<Resource::ResourceSystem>(
        mVFS.get(), Settings::cells().mCacheExpiryDelay, &mEncoder.get()->getStatelessEncoder());
    mEnvironment.setResourceSystem(*mResourceSystem);

    ctrSetThreadSpawnHint(2, 0x39, 128 * 1024);
    mWorkQueue = std::make_shared<SceneUtil::WorkQueue>(Settings::cells().mPreloadNumThreads);
    ctrClearThreadSpawnHint();

    mL10nManager = std::make_unique<L10n::Manager>(mVFS.get());
    mL10nManager->setPreferredLocales(Settings::general().mPreferredLocales, Settings::general().mGmstOverridesL10n);
    mEnvironment.setL10nManager(*mL10nManager);

    mLuaManager = std::make_unique<MWNoLua::NullLuaManager>();
    mEnvironment.setLuaManager(*mLuaManager);


    const auto keybinderUser = mCfgMgr.getUserConfigPath() / "input_v3.xml";
    const bool keybinderUserExists = std::filesystem::exists(keybinderUser);
    if (keybinderUserExists)
        Log(Debug::Info) << "Loading keybindings file: " << keybinderUser;

    mWindowManager = std::make_unique<MWGui::WindowManager>(mWindow, nullptr, nullptr, mResourceSystem.get(),
        mWorkQueue, mCfgMgr.getLogPath(), mScriptConsoleMode, mTranslationDataStorage, mEncoding, mExportFonts,
        Version::getOpenmwVersionDescription(), mCfgMgr);
    mEnvironment.setWindowManager(*mWindowManager);

    mWindowManager->setBottomScreenLayers({ "Windows", "JournalBooks", "DragAndDrop", "Settings", "Console", "Debug",
                                              "ControllerButtons", "Modal", "Popup", "MainMenu" },
        { "MainMenuBackground", "LoadingScreenBackground", "LoadingScreen" });

    CtrFrame::setPresenter([this](bool drawScene) {
        sProfile.discard();
        present(drawScene);
    });

    mInputManager = std::make_unique<MWInput::InputManager>(mWindow, nullptr, [] {}, keybinderUser,
        keybinderUserExists, std::filesystem::path(), std::filesystem::path(), mGrab);
    mEnvironment.setInputManager(*mInputManager);

    mSoundManager = std::make_unique<MWSound::SoundManager>(mVFS.get(), mUseSound);
    mEnvironment.setSoundManager(*mSoundManager);

    mWorld = std::make_unique<MWWorld::World>(
        mResourceSystem.get(), mActivationDistanceOverride, mCellName, mCfgMgr.getUserDataPath());
    mEnvironment.setWorld(*mWorld);
    mEnvironment.setWorldModel(mWorld->getWorldModel());
    mEnvironment.setESMStore(mWorld->getStore());

    const MWWorld::Store<ESM::GameSetting>* gmst = &mWorld->getStore().get<ESM::GameSetting>();
    mL10nManager->setGmstLoader([gmst, misses = std::set<std::string, Misc::StringUtils::CiComp>()](
                                    std::string_view gmstName) mutable -> const std::string* {
        const ESM::GameSetting* res = gmst->search(gmstName);
        if (res && res->mValue.getType() == ESM::VT_String)
            return &res->mValue.getString();
        if (misses.emplace(gmstName).second)
            Log(Debug::Error) << "GMST " << gmstName << " not found";
        return nullptr;
    });

    mWindowManager->setStore(mWorld->getStore());

    mTranslationDataStorage.setEncoder(mEncoder.get());
    for (auto& mContentFile : mContentFiles)
        mTranslationDataStorage.loadTranslationData(mFileCollections, mContentFile);

    Compiler::registerExtensions(mExtensions);

    mScriptContext = std::make_unique<MWScript::CompilerContext>(MWScript::CompilerContext::Type_Full);
    mScriptContext->setExtensions(&mExtensions);

    mScriptManager = std::make_unique<MWScript::ScriptManager>(mWorld->getStore(), *mScriptContext, mWarningsMode);
    mEnvironment.setScriptManager(*mScriptManager);

    mMechanicsManager = std::make_unique<MWMechanics::MechanicsManager>();
    mEnvironment.setMechanicsManager(*mMechanicsManager);

    mJournal = std::make_unique<MWDialogue::Journal>();
    mEnvironment.setJournal(*mJournal);

    mDialogueManager = std::make_unique<MWDialogue::DialogueManager>(mExtensions, mTranslationDataStorage);
    mEnvironment.setDialogueManager(*mDialogueManager);

    Loading::Listener* listener = MWBase::Environment::get().getWindowManager()->getLoadingScreen();
    listener->loadingOn();
    mWorld->loadData(mFileCollections, mContentFiles, mGroundcoverFiles, mEncoder.get(), listener);
    listener->loadingOff();

    mWorld->init(mMaxRecastLogLevel, mWorkQueue);
    mEnvironment.setWorldScene(mWorld->getWorldScene());
    mWorld->setupPlayer();
    mWorld->setRandomSeed(mRandomSeed);
    mWindowManager->initUI();

    if (mCompileAll)
    {
        std::pair<int, int> result = mScriptManager->compileAll();
        if (result.first)
            Log(Debug::Info) << "compiled " << result.second << " of " << result.first << " scripts ("
                             << 100 * static_cast<double>(result.second) / result.first << "%)";
    }
    if (mCompileAllDialogue)
    {
        std::pair<int, int> result = MWDialogue::ScriptTest::compileAll(&mExtensions, mWarningsMode);
        if (result.first)
            Log(Debug::Info) << "compiled " << result.second << " of " << result.first << " dialogue scripts ("
                             << 100 * static_cast<double>(result.second) / result.first << "%)";
    }
}

void OMW::Engine::go()
{
    assert(!mContentFiles.empty());

    Misc::Rng::init(mRandomSeed);

    MWClass::registerClasses();

    mEncoder = std::make_unique<ToUTF8::Utf8Encoder>(mEncoding);

    mEnvironment.setFrameRateLimit(Settings::video().mFramerateLimit);

    prepareEngine();

    if (!mSaveGameFile.empty())
    {
        mStateManager->loadGame(mSaveGameFile);
    }
    else if (!mSkipMenu)
    {
        mWindowManager->pushGuiMode(MWGui::GM_MainMenu);

        if (mVFS->exists(MWSound::titleMusic))
            mSoundManager->streamMusic(MWSound::titleMusic, MWSound::MusicType::Normal);
        else
            Log(Debug::Warning) << "Title music not found";
    }
    else
    {
        mStateManager->newGame(!mNewGame);
    }

    if (!mStartupScript.empty() && mStateManager->getState() == MWState::StateManager::State_Running)
    {
        mWindowManager->executeInConsole(mStartupScript);
    }

    MWWorld::DateTimeManager& timeManager = *mWorld->getTimeManager();
    Misc::FrameRateLimiter frameRateLimiter = Misc::makeFrameRateLimiter(mEnvironment.getFrameRateLimit());
    const std::chrono::steady_clock::duration maxSimulationInterval(std::chrono::milliseconds(200));
    while (!mStateManager->hasQuitRequest())
    {
        const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(
                              std::min(frameRateLimiter.getLastFrameDuration(), maxSimulationInterval))
                              .count()
            * timeManager.getSimulationTimeScale();

        if (!frame(mFrameNumber++, static_cast<float>(dt)))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        timeManager.updateIsPaused();
        if (!timeManager.isPaused())
        {
            timeManager.setSimulationTime(timeManager.getSimulationTime() + dt);
            timeManager.setRenderingSimulationTime(timeManager.getRenderingSimulationTime() + dt);
        }

        frameRateLimiter.limit();
    }

    Settings::Manager::saveUser(mCfgMgr.getUserConfigPath() / "settings.cfg");
}

void OMW::Engine::setCompileAll(bool all)
{
    mCompileAll = all;
}

void OMW::Engine::setCompileAllDialogue(bool all)
{
    mCompileAllDialogue = all;
}

void OMW::Engine::setSoundUsage(bool soundUsage)
{
    mUseSound = soundUsage;
}

void OMW::Engine::setEncoding(const ToUTF8::FromType& encoding)
{
    mEncoding = encoding;
}

void OMW::Engine::setScriptConsoleMode(bool enabled)
{
    mScriptConsoleMode = enabled;
}

void OMW::Engine::setStartupScript(const std::filesystem::path& path)
{
    mStartupScript = path;
}

void OMW::Engine::setActivationDistanceOverride(int distance)
{
    mActivationDistanceOverride = distance;
}

void OMW::Engine::setWarningsMode(int mode)
{
    mWarningsMode = mode;
}

void OMW::Engine::enableFontExport(bool exportFonts)
{
    mExportFonts = exportFonts;
}

void OMW::Engine::setSaveGameFile(const std::filesystem::path& savegame)
{
    mSaveGameFile = savegame;
}

void OMW::Engine::setRandomSeed(unsigned int seed)
{
    mRandomSeed = seed;
}
