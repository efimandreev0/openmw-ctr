#include "engine.hpp"

#include <cerrno>
#include <chrono>
#include <future>
#include <system_error>

#include <osgDB/ReaderWriter>
#include <osgDB/Registry>
#include <osgUtil/RenderBin>
#include <osgViewer/ViewerEventHandlers>

#include <SDL.h>

#include <components/debug/debuglog.hpp>
#include <components/debug/gldebug.hpp>

#if defined(__vita__) || defined(__3DS__)
#include <malloc.h>
#ifdef __3DS__
#include <NovaGL.h>
#endif
#include "vita/VitaInit.h"
#define VITA_CRUMB(msg) Vita::breadcrumb(msg)
#else
#define VITA_CRUMB(msg)
#endif
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#endif

#include <components/misc/rng.hpp>
#include <components/misc/strings/format.hpp>

#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include <components/sdlutil/imagetosurface.hpp>
#include <components/sdlutil/sdlgraphicswindow.hpp>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/resource/stats.hpp>

#include <components/compiler/extensions0.hpp>

#include <components/stereo/stereomanager.hpp>

#include <components/sceneutil/glextensions.hpp>
#include <components/sceneutil/workqueue.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/files/scancache.hpp>

#include <components/version/version.hpp>

#include <components/l10n/manager.hpp>

#include <components/loadinglistener/asynclistener.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include <components/misc/frameratelimiter.hpp>

#include <components/sceneutil/color.hpp>
#include <components/sceneutil/depth.hpp>
#include <components/sceneutil/screencapture.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/sceneutil/util.hpp>

#include <components/settings/settings.hpp>
#include <components/settings/shadermanager.hpp>
#include <components/settings/values.hpp>

#include "mwinput/inputmanagerimp.hpp"

#include "mwgui/windowmanagerimp.hpp"

#include "mwlua/luamanagerimp.hpp"
#include "mwlua/worker.hpp"

#include "mwscript/interpretercontext.hpp"
#include "mwscript/scriptmanagerimp.hpp"

#include "mwsound/constants.hpp"
#include "mwsound/soundmanagerimp.hpp"

#include "mwworld/class.hpp"
#include "mwworld/datetimemanager.hpp"
#include "mwworld/worldimp.hpp"

#include "mwrender/vismask.hpp"

#include "mwclass/classes.hpp"

#include "mwdialogue/dialoguemanagerimp.hpp"
#include "mwdialogue/journalimp.hpp"
#include "mwdialogue/scripttest.hpp"

#include "mwmechanics/mechanicsmanagerimp.hpp"

#include "mwstate/statemanagerimp.hpp"

#include "profile.hpp"

namespace
{
    void checkSDLError(int ret)
    {
        if (ret != 0)
            Log(Debug::Error) << "SDL error: " << SDL_GetError();
    }

    void initStatsHandler(Resource::Profiler& profiler)
    {
        const osg::Vec4f textColor(1.f, 1.f, 1.f, 1.f);
        const osg::Vec4f barColor(1.f, 1.f, 1.f, 1.f);
        const float multiplier = 1000;
        const bool average = true;
        const bool averageInInverseSpace = false;
        const float maxValue = 10000;

        OMW::forEachUserStatsValue([&](const OMW::UserStats& v) {
            profiler.addUserStatsLine(v.mLabel, textColor, barColor, v.mTaken, multiplier, average,
                averageInInverseSpace, v.mBegin, v.mEnd, maxValue);
        });
        // the forEachUserStatsValue loop is "run" at compile time, hence the settings manager is not available.
        // Unconditionnally add the async physics stats, and then remove it at runtime if necessary
        if (Settings::physics().mAsyncNumThreads == 0)
            profiler.removeUserStatsLine(" -Async");
    }

    struct ScreenCaptureMessageBox
    {
        void operator()(std::string filePath) const
        {
            if (filePath.empty())
            {
                MWBase::Environment::get().getWindowManager()->scheduleMessageBox(
                    "#{OMWEngine:ScreenshotFailed}", MWGui::ShowInDialogueMode_Never);

                return;
            }

            auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
            std::string message = l10n->formatMessage("ScreenshotMade", { "file" }, { L10n::toUnicode(filePath) });

            MWBase::Environment::get().getWindowManager()->scheduleMessageBox(
                std::move(message), MWGui::ShowInDialogueMode_Never);
        }
    };

    struct IgnoreString
    {
        void operator()(std::string) const {}
    };

    class IdentifyOpenGLOperation : public osg::GraphicsOperation
    {
    public:
        IdentifyOpenGLOperation()
            : GraphicsOperation("IdentifyOpenGLOperation", false)
        {
        }

        void operator()(osg::GraphicsContext* graphicsContext) override
        {
            Log(Debug::Info) << "OpenGL Vendor: " << glGetString(GL_VENDOR);
            Log(Debug::Info) << "OpenGL Renderer: " << glGetString(GL_RENDERER);
            Log(Debug::Info) << "OpenGL Version: " << glGetString(GL_VERSION);
            glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &mMaxTextureImageUnits);
        }

        int getMaxTextureImageUnits() const
        {
            if (mMaxTextureImageUnits == 0)
                throw std::logic_error("mMaxTextureImageUnits is not initialized");
            return mMaxTextureImageUnits;
        }

    private:
        int mMaxTextureImageUnits = 0;
    };

    void reportStats(unsigned frameNumber, osgViewer::Viewer& viewer, std::ostream& stream)
    {
        viewer.getViewerStats()->report(stream, frameNumber);
        osgViewer::Viewer::Cameras cameras;
        viewer.getCameras(cameras);
        for (osg::Camera* camera : cameras)
            camera->getStats()->report(stream, frameNumber);
    }
}

void OMW::Engine::executeLocalScripts()
{
    MWWorld::LocalScripts& localScripts = mWorld->getLocalScripts();

    localScripts.startIteration();
    std::pair<ESM::RefId, MWWorld::Ptr> script;
#ifdef __3DS__
    // One-shot census: the chargen intro is driven by LOCAL scripts
    // (CharGenNameNPC's Say/SayDone state machine) — if it silently isn't
    // in this list, the whole intro deadlocks with zero errors logged.
    static int s_dumpCountdown = 90;
    const bool dump = s_dumpCountdown > 0 && --s_dumpCountdown == 0;
    int scriptCount = 0;
#endif
    while (localScripts.getNext(script))
    {
#ifdef __3DS__
        ++scriptCount;
        if (dump)
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "[LocalScript] '%s' on '%s'", script.first.toDebugString().c_str(),
                script.second.getCellRef().getRefId().toDebugString().c_str());
            Vita::breadcrumb(buf);
        }
        // Chargen intro tracer: the CharGenNameNPC state machine drives the
        // whole intro (Say → name menu → guards). Dump its locals ~1/s so a
        // silent stall is attributable to a concrete guard variable.
        {
            static const ESM::RefId sNameScript = ESM::RefId::stringRefId("CharGenNameNPC");
            static int s_traceTick = 0;
            if (script.first == sNameScript && (++s_traceTick & 0x3F) == 1)
            {
                MWScript::Locals& locals = script.second.getRefData().getLocals();
                char buf[160];
                snprintf(buf, sizeof(buf), "[NameNPC] state=%d timer=%.2f wander=%d dt=%.3f menumode=%d",
                    locals.getIntVar(sNameScript, "state"), locals.getFloatVar(sNameScript, "timer"),
                    locals.getIntVar(sNameScript, "wandering"),
                    MWBase::Environment::get().getFrameDuration(),
                    MWBase::Environment::get().getWindowManager()->isGuiMode() ? 1 : 0);
                Vita::breadcrumb(buf);
            }
        }
#endif
        MWScript::InterpreterContext interpreterContext(&script.second.getRefData().getLocals(), script.second);
        mScriptManager->run(script.first, interpreterContext);
    }
#ifdef __3DS__
    if (dump)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "[LocalScript] total=%d", scriptCount);
        Vita::breadcrumb(buf);
    }
#endif
}

#ifdef __3DS__
// Frame-phase accumulators for the [FRAME] tick: how much of the frame goes
// to engine updates vs the render traversals (cull+draw+swap).
static double s_updAccMs = 0.0;
static double s_rtAccMs = 0.0;
// Sub-phase split of the update half: 0=input+sound 1=luaSync 2=state+script
// 3=mechanics 4=physics 5=world 6=gui 7=osg-traversals/focus/unref tail.
static double s_phAccMs[8] = {};
static std::chrono::steady_clock::time_point s_phT;
extern "C" {
extern volatile int g_ctrFrameNo;
extern const char* volatile g_ctrBeacon;
// ctr/CtrThreads.cpp — placement hints for the next std::thread creations.
void ctrSetThreadSpawnHint(int core, int prio, size_t minStack);
void ctrClearThreadSpawnHint(void);
}
#define CTR_PH_MARK(idx)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        const auto phNow = std::chrono::steady_clock::now();                                                           \
        s_phAccMs[idx] += std::chrono::duration<double, std::milli>(phNow - s_phT).count();                            \
        s_phT = phNow;                                                                                                 \
        g_ctrBeacon = "post-ph" #idx;                                                                                  \
    } while (0)
#else
#define CTR_PH_MARK(idx)
#endif

bool OMW::Engine::frame(unsigned frameNumber, float frametime)
{
#ifdef __3DS__
    const auto frameT0 = std::chrono::steady_clock::now();
    s_phT = frameT0;
    ++g_ctrFrameNo;
    g_ctrBeacon = "frame-begin";
    // Dev hook: with sdmc:/3ds/openmw/auto_newgame.flag present, press
    // "New Game" from the main menu on frame 300 — mirrors
    // MWGui::MainMenu::onNewGameConfirmed exactly. Synthetic input into
    // citra is dead, so menu-path repros need an in-engine trigger.
    {
        static bool s_autoNewGameChecked = false;
        if (!s_autoNewGameChecked && g_ctrFrameNo >= 300
            && mStateManager->getState() == MWBase::StateManager::State_NoGame)
        {
            s_autoNewGameChecked = true;
            if (FILE* ang = fopen("sdmc:/3ds/openmw/auto_newgame.flag", "rb"))
            {
                fclose(ang);
                Vita::breadcrumb("[AutoNewGame] pressing New Game");
                mWindowManager->removeGuiMode(MWGui::GM_MainMenu);
                mStateManager->newGame();
            }
        }
        // Dev hook: auto_settings.flag opens the settings window on frame
        // 600 — reproduces the reported settings-menu crash headlessly.
        static bool s_autoSettingsChecked = false;
        if (!s_autoSettingsChecked && g_ctrFrameNo >= 600)
        {
            s_autoSettingsChecked = true;
            if (FILE* asf = fopen("sdmc:/3ds/openmw/auto_settings.flag", "rb"))
            {
                fclose(asf);
                Vita::breadcrumb("[AutoSettings] opening settings window");
                mWindowManager->toggleSettingsWindow();
            }
        }
        // Dev hook: auto_menu.flag pushes the in-game main menu on frame 600
        // — deterministic repro for the GUI-scene renderTrav wedge.
        static bool s_autoMenuChecked = false;
        if (!s_autoMenuChecked && g_ctrFrameNo >= 600)
        {
            s_autoMenuChecked = true;
            if (FILE* amf = fopen("sdmc:/3ds/openmw/auto_menu.flag", "rb"))
            {
                fclose(amf);
                Vita::breadcrumb("[AutoMenu] pushing main menu");
                mWindowManager->pushGuiMode(MWGui::GM_MainMenu);
            }
        }
    }
#endif
    const osg::Timer_t frameStart = mViewer->getStartTick();
    const osg::Timer* const timer = osg::Timer::instance();
    osg::Stats* const stats = mViewer->getViewerStats();

    mEnvironment.setFrameDuration(frametime);

    try
    {
        // update input
        {
            ScopedProfile<UserStatsType::Input> profile(frameStart, frameNumber, *timer, *stats);
            mInputManager->update(frametime, false);
        }

        // When the window is minimized, pause the game. Currently this *has* to be here to work around a MyGUI bug.
        // If we are not currently rendering, then RenderItems will not be reused resulting in a memory leak upon
        // changing widget textures (fixed in MyGUI 3.3.2), and destroyed widgets will not be deleted (not fixed yet,
        // https://github.com/MyGUI/mygui/issues/21)
        {
            ScopedProfile<UserStatsType::Sound> profile(frameStart, frameNumber, *timer, *stats);

            if (!mWindowManager->isWindowVisible())
            {
                mSoundManager->pausePlayback();
                return false;
            }
            else
                mSoundManager->resumePlayback();

            // sound
            if (mUseSound)
                mSoundManager->update(frametime);
        }
        CTR_PH_MARK(0);

        {
            ScopedProfile<UserStatsType::LuaSyncUpdate> profile(frameStart, frameNumber, *timer, *stats);
            // Should be called after input manager update and before any change to the game world.
            // It applies to the game world queued changes from the previous frame.
            mLuaManager->synchronizedUpdate();
        }
        CTR_PH_MARK(1);

        // update game state
        {
            ScopedProfile<UserStatsType::State> profile(frameStart, frameNumber, *timer, *stats);
            mStateManager->update(frametime);
        }

        bool paused = mWorld->getTimeManager()->isPaused();

        {
            ScopedProfile<UserStatsType::Script> profile(frameStart, frameNumber, *timer, *stats);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
                if (!mWindowManager->containsMode(MWGui::GM_MainMenu) || !paused)
                {
                    if (mWorld->getScriptsEnabled())
                    {
                        // local scripts
                        executeLocalScripts();

                        // global scripts
                        mScriptManager->getGlobalScripts().run();
                    }

                    mWorld->getWorldScene().markCellAsUnchanged();
                }

                if (!paused)
                {
                    double hours = (frametime * mWorld->getTimeManager()->getGameTimeScale()) / 3600.0;
                    mWorld->advanceTime(hours, true);
#if defined(__vita__) || defined(__3DS__)
                    // rechargeItems iterates every NPC/Creature/Container in
                    // every active cell each frame. Recharge math is
                    // FPS-invariant; throttle to ~4 Hz with accumulated dt.
                    static float s_rechargeAccum = 0.f;
                    s_rechargeAccum += frametime;
                    if (s_rechargeAccum >= 0.25f)
                    {
                        mWorld->rechargeItems(s_rechargeAccum, true);
                        s_rechargeAccum = 0.f;
                    }
#else
                    mWorld->rechargeItems(frametime, true);
#endif
                }
            }
        }

        CTR_PH_MARK(2);
        // update mechanics
        {
            ScopedProfile<UserStatsType::Mechanics> profile(frameStart, frameNumber, *timer, *stats);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
                mMechanicsManager->update(frametime, paused);
            }

            if (mStateManager->getState() == MWBase::StateManager::State_Running)
            {
                MWWorld::Ptr player = mWorld->getPlayerPtr();
                if (!paused && player.getClass().getCreatureStats(player).isDead())
                    mStateManager->endGame();
            }
        }

        CTR_PH_MARK(3);
        // update physics
        {
            ScopedProfile<UserStatsType::Physics> profile(frameStart, frameNumber, *timer, *stats);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
#if defined(__vita__) || defined(__3DS__)
                try {
                    mWorld->updatePhysics(frametime, paused, frameStart, frameNumber, *stats);
                } catch (const std::exception& e) {
                    Vita::breadcrumb(("[PhysCrash] std::exception: " + std::string(e.what())).c_str());
                } catch (...) {
                    Vita::breadcrumb("[PhysCrash] non-std exception caught");
                }
#else
                mWorld->updatePhysics(frametime, paused, frameStart, frameNumber, *stats);
#endif
            }
        }

        CTR_PH_MARK(4);
        // update world
        {
            ScopedProfile<UserStatsType::World> profile(frameStart, frameNumber, *timer, *stats);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
                mWorld->update(frametime, paused);
            }
        }

        CTR_PH_MARK(5);
        // update GUI
#ifdef __3DS__
        if (Vita::firstFramesTrace())
            VITA_CRUMB("frame: pre-GUI");
#endif
        {
            ScopedProfile<UserStatsType::Gui> profile(frameStart, frameNumber, *timer, *stats);
            mWindowManager->update(frametime);
        }
#ifdef __3DS__
        if (Vita::firstFramesTrace())
            VITA_CRUMB("frame: post-GUI");
#endif
        CTR_PH_MARK(6);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "Error in frame: " << e.what();
    }

    const bool reportResource = stats->collectStats("resource");

    if (reportResource)
        stats->setAttribute(frameNumber, "UnrefQueue", static_cast<double>(mUnrefQueue->getSize()));

    mUnrefQueue->flush(*mWorkQueue);
#ifdef __3DS__
    if (Vita::firstFramesTrace())
        VITA_CRUMB("frame: post-unref");
#endif

    if (reportResource)
    {
        stats->setAttribute(frameNumber, "FrameNumber", frameNumber);

        mResourceSystem->reportStats(frameNumber, stats);

        stats->setAttribute(frameNumber, "WorkQueue", static_cast<double>(mWorkQueue->getNumItems()));
        stats->setAttribute(frameNumber, "WorkThread", static_cast<double>(mWorkQueue->getNumActiveThreads()));

        mMechanicsManager->reportStats(frameNumber, *stats);
        mWorld->reportStats(frameNumber, *stats);
        mLuaManager->reportStats(frameNumber, *stats);

        stats->setAttribute(frameNumber, "StringRefId Count", static_cast<double>(ESM::StringRefId::totalCount()));
    }

    mStereoManager->updateSettings(Settings::camera().mNearClip, Settings::camera().mViewingDistance);

    mViewer->eventTraversal();
#ifdef __3DS__
    if (Vita::firstFramesTrace())
        VITA_CRUMB("frame: post-eventTrav");
#endif
    mViewer->updateTraversal();
#ifdef __3DS__
    if (Vita::firstFramesTrace())
        VITA_CRUMB("frame: post-updateTrav");
#endif

    // update focus object for GUI
    {
        ScopedProfile<UserStatsType::Focus> profile(frameStart, frameNumber, *timer, *stats);
        mWorld->updateFocusObject();
    }

    // if there is a separate Lua thread, it starts the update now
    mLuaWorker->allowUpdate(frameStart, frameNumber, *stats);

#ifdef __3DS__
    if (Vita::firstFramesTrace())
    {
        VITA_CRUMB("frame: pre-renderTrav");
        nova_wait_tag_arm(96); // trace NovaGL GPU-wait sites for this frame
    }
    else
    {
        // Rolling low-noise budget: if a frame wedges in a NovaGL wait
        // (GUI-scene hangs land in renderTrav), the last tags name the site.
        nova_wait_tag_arm(8);
    }
    const auto rtT0 = std::chrono::steady_clock::now();
    s_updAccMs += std::chrono::duration<double, std::milli>(rtT0 - frameT0).count();
    s_phT = rtT0;
    s_phAccMs[7] += std::chrono::duration<double, std::milli>(rtT0 - s_phT).count();
    g_ctrBeacon = "renderTrav";
#endif
    mViewer->renderingTraversals();
#ifdef __3DS__
    s_rtAccMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rtT0).count();
    g_ctrBeacon = "frame-tail";
    if (Vita::firstFramesTrace())
        VITA_CRUMB("frame: post-renderTrav");
#endif

    mLuaWorker->finishUpdate(frameStart, frameNumber, *stats);

    return true;
}

OMW::Engine::Engine(Files::ConfigurationManager& configurationManager)
    : mWindow(nullptr)
    , mEncoding(ToUTF8::WINDOWS_1252)
    , mScreenCaptureOperation(nullptr)
    , mSelectDepthFormatOperation(new SceneUtil::SelectDepthFormatOperation())
    , mSelectColorFormatOperation(new SceneUtil::Color::SelectColorFormatOperation())
    , mStereoManager(nullptr)
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
#if SDL_VERSION_ATLEAST(2, 24, 0)
    SDL_SetHint(SDL_HINT_MAC_OPENGL_ASYNC_DISPATCH, "1");
#endif
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0"); // We use only gamepads

#ifdef __3DS__
    // SDL2's n3ds video driver calls gfxInit itself, reconfiguring the
    // framebuffer format/stride underneath NovaGL — the screen turns into
    // interleaved-stripe garbage. The dummy driver never touches the GPU;
    // rendering goes through NovaGL and input through the n3ds joystick
    // driver, neither of which needs SDL video.
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif

    Uint32 flags
        = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR;
    if (SDL_WasInit(flags) == 0)
    {
        SDL_SetMainReady();
        if (SDL_Init(flags) != 0)
        {
            throw std::runtime_error("Could not initialize SDL! " + std::string(SDL_GetError()));
        }
    }
}

OMW::Engine::~Engine()
{
    if (mScreenCaptureOperation != nullptr)
    {
        mScreenCaptureOperation->stop();
        mScreenCaptureOperation = nullptr;
    }
    mScreenCaptureHandler = nullptr;

    mMechanicsManager = nullptr;
    mDialogueManager = nullptr;
    mJournal = nullptr;
    mWindowManager = nullptr;
    mScriptManager = nullptr;
    mWorld = nullptr;
    mStereoManager = nullptr;
    mSoundManager = nullptr;
    mInputManager = nullptr;
    mStateManager = nullptr;
    mLuaWorker = nullptr;
    mLuaManager = nullptr;
    mL10nManager = nullptr;

    mScriptContext = nullptr;

    mUnrefQueue = nullptr;
    mWorkQueue = nullptr;

    mViewer = nullptr;

    mResourceSystem.reset();

    mEncoder = nullptr;

    if (mWindow)
    {
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }

    SDL_Quit();

    Log(Debug::Info) << "Quitting peacefully.";
}

// Set data dir

void OMW::Engine::setDataDirs(const Files::PathContainer& dataDirs)
{
    mDataDirs = dataDirs;
    mDataDirs.insert(mDataDirs.begin(), mResDir / "vfs");
    mFileCollections = Files::Collections(mDataDirs);

#if defined(__vita__) || defined(__3DS__)
    const auto cachePath = mCfgMgr.getUserConfigPath() / "scan_cache.bin";
    Files::Collections::CollectionsMap cached;
    if (Files::loadScanCache(cachePath, mDataDirs, cached))
        mFileCollections.setCollections(std::move(cached));
#endif
}

// Add BSA archive
void OMW::Engine::addArchive(const std::string& archive)
{
    mArchives.push_back(archive);
}

// Set resource dir
void OMW::Engine::setResourceDir(const std::filesystem::path& parResDir)
{
    mResDir = parResDir;
    if (!Version::checkResourcesVersion(mResDir))
        Log(Debug::Error) << "Resources dir " << mResDir
                          << " doesn't match OpenMW binary, the game may work incorrectly.";
}

// Set start cell name
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
    const int screen = Settings::video().mScreen;
    const int width = Settings::video().mResolutionX;
    const int height = Settings::video().mResolutionY;
    const Settings::WindowMode windowMode = Settings::video().mWindowMode;
    const bool windowBorder = Settings::video().mWindowBorder;
    const SDLUtil::VSyncMode vsync = Settings::video().mVsyncMode;
    unsigned antialiasing = static_cast<unsigned>(Settings::video().mAntialiasing);

    int posX = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);
    int posY = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);

    if (windowMode == Settings::WindowMode::Fullscreen || windowMode == Settings::WindowMode::WindowedFullscreen)
    {
        posX = SDL_WINDOWPOS_UNDEFINED_DISPLAY(screen);
        posY = SDL_WINDOWPOS_UNDEFINED_DISPLAY(screen);
    }

#if defined(__vita__) || defined(__3DS__)
    Uint32 flags = SDL_WINDOW_SHOWN;
#else
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
    if (windowMode == Settings::WindowMode::Fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    else if (windowMode == Settings::WindowMode::WindowedFullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    // Allows for Windows snapping features to properly work in borderless window
    SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
    SDL_SetHint("SDL_BORDERLESS_RESIZABLE_STYLE", "1");

    if (!windowBorder)
        flags |= SDL_WINDOW_BORDERLESS;

    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, Settings::video().mMinimizeOnFocusLoss ? "1" : "0");

    checkSDLError(SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8));
    checkSDLError(SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8));
    checkSDLError(SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8));
    checkSDLError(SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0));
    checkSDLError(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24));
    if (Debug::shouldDebugOpenGL())
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG));

    if (antialiasing > 0)
    {
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1));
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
    }

    osg::ref_ptr<SDLUtil::GraphicsWindowSDL2> graphicsWindow;
    while (!graphicsWindow || !graphicsWindow->valid())
    {
        while (!mWindow)
        {
            mWindow = SDL_CreateWindow("OpenMW", posX, posY, width, height, flags);
            if (!mWindow)
            {
                // Try with a lower AA
                if (antialiasing > 0)
                {
                    Log(Debug::Warning) << "Warning: " << antialiasing << "x antialiasing not supported, trying "
                                        << antialiasing / 2;
                    antialiasing /= 2;
                    Settings::video().mAntialiasing.set(antialiasing);
                    checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
                    continue;
                }
                else
                {
                    std::stringstream error;
                    error << "Failed to create SDL window: " << SDL_GetError();
                    throw std::runtime_error(error.str());
                }
            }
        }

        // Since we use physical resolution internally, we have to create the window with scaled resolution,
        // but we can't get the scale before the window exists, so instead we have to resize aftewards.
        int w, h;
        SDL_GetWindowSize(mWindow, &w, &h);
        int dw, dh;
        SDL_GL_GetDrawableSize(mWindow, &dw, &dh);
        if (dw != w || dh != h)
        {
            SDL_SetWindowSize(mWindow, width / (dw / w), height / (dh / h));
        }

        setWindowIcon();

        osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
        SDL_GetWindowPosition(mWindow, &traits->x, &traits->y);
        SDL_GL_GetDrawableSize(mWindow, &traits->width, &traits->height);
        traits->windowName = SDL_GetWindowTitle(mWindow);
        traits->windowDecoration = !(SDL_GetWindowFlags(mWindow) & SDL_WINDOW_BORDERLESS);
        traits->screenNum = SDL_GetWindowDisplayIndex(mWindow);
        traits->vsync = 0;
        traits->inheritedWindowData = new SDLUtil::GraphicsWindowSDL2::WindowData(mWindow);

        graphicsWindow = new SDLUtil::GraphicsWindowSDL2(traits, vsync);
        if (!graphicsWindow->valid())
            throw std::runtime_error("Failed to create GraphicsContext");

        if (traits->samples < antialiasing)
        {
            Log(Debug::Warning) << "Warning: Framebuffer MSAA level is only " << traits->samples << "x instead of "
                                << antialiasing << "x. Trying " << antialiasing / 2 << "x instead.";
            graphicsWindow->closeImplementation();
            SDL_DestroyWindow(mWindow);
            mWindow = nullptr;
            antialiasing /= 2;
            Settings::video().mAntialiasing.set(antialiasing);
            checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
            continue;
        }

        if (traits->red < 8)
            Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->red << " bit red channel.";
        if (traits->green < 8)
            Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->green << " bit green channel.";
        if (traits->blue < 8)
            Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->blue << " bit blue channel.";
        if (traits->depth < 24)
            Log(Debug::Warning) << "Warning: Framebuffer only has " << traits->depth << " bits of depth precision.";

        traits->alpha = 0; // set to 0 to stop ScreenCaptureHandler reading the alpha channel
    }

    osg::ref_ptr<osg::Camera> camera = mViewer->getCamera();
    camera->setGraphicsContext(graphicsWindow);
    camera->setViewport(0, 0, graphicsWindow->getTraits()->width, graphicsWindow->getTraits()->height);

    osg::ref_ptr<SceneUtil::OperationSequence> realizeOperations = new SceneUtil::OperationSequence(false);
    mViewer->setRealizeOperation(realizeOperations);
    osg::ref_ptr<IdentifyOpenGLOperation> identifyOp = new IdentifyOpenGLOperation();
    realizeOperations->add(identifyOp);
    realizeOperations->add(new SceneUtil::GetGLExtensionsOperation());

    if (Debug::shouldDebugOpenGL())
        realizeOperations->add(new Debug::EnableGLDebugOperation());

    realizeOperations->add(mSelectDepthFormatOperation);
    realizeOperations->add(mSelectColorFormatOperation);

    if (Stereo::getStereo())
    {
        Stereo::Settings settings;

        settings.mMultiview = Settings::stereo().mMultiview;
        settings.mAllowDisplayListsForMultiview = Settings::stereo().mAllowDisplayListsForMultiview;
        settings.mSharedShadowMaps = Settings::stereo().mSharedShadowMaps;

        if (Settings::stereo().mUseCustomView)
        {
            const osg::Vec3 leftEyeOffset(Settings::stereoView().mLeftEyeOffsetX,
                Settings::stereoView().mLeftEyeOffsetY, Settings::stereoView().mLeftEyeOffsetZ);

            const osg::Quat leftEyeOrientation(Settings::stereoView().mLeftEyeOrientationX,
                Settings::stereoView().mLeftEyeOrientationY, Settings::stereoView().mLeftEyeOrientationZ,
                Settings::stereoView().mLeftEyeOrientationW);

            const osg::Vec3 rightEyeOffset(Settings::stereoView().mRightEyeOffsetX,
                Settings::stereoView().mRightEyeOffsetY, Settings::stereoView().mRightEyeOffsetZ);

            const osg::Quat rightEyeOrientation(Settings::stereoView().mRightEyeOrientationX,
                Settings::stereoView().mRightEyeOrientationY, Settings::stereoView().mRightEyeOrientationZ,
                Settings::stereoView().mRightEyeOrientationW);

            settings.mCustomView = Stereo::CustomView{
                .mLeft = Stereo::View{
                    .pose = Stereo::Pose{
                        .position = leftEyeOffset,
                        .orientation = leftEyeOrientation,
                    },
                    .fov = Stereo::FieldOfView{
                        .angleLeft = Settings::stereoView().mLeftEyeFovLeft,
                        .angleRight = Settings::stereoView().mLeftEyeFovRight,
                        .angleUp = Settings::stereoView().mLeftEyeFovUp,
                        .angleDown = Settings::stereoView().mLeftEyeFovDown,
                    },
                },
                .mRight = Stereo::View{
                    .pose = Stereo::Pose{
                        .position = rightEyeOffset,
                        .orientation = rightEyeOrientation,
                    },
                    .fov = Stereo::FieldOfView{
                        .angleLeft = Settings::stereoView().mRightEyeFovLeft,
                        .angleRight = Settings::stereoView().mRightEyeFovRight,
                        .angleUp = Settings::stereoView().mRightEyeFovUp,
                        .angleDown = Settings::stereoView().mRightEyeFovDown,
                    },
                },
            };
        }

        if (Settings::stereo().mUseCustomEyeResolution)
            settings.mEyeResolution
                = osg::Vec2i(Settings::stereoView().mEyeResolutionX, Settings::stereoView().mEyeResolutionY);

        realizeOperations->add(new Stereo::InitializeStereoOperation(settings));
    }

    mViewer->realize();
    mGlMaxTextureImageUnits = identifyOp->getMaxTextureImageUnits();

#if defined(__vita__)
    // Enable OSG matrix uniforms and vertex attribute aliasing for custom GLSL shaders.
    // Matrix uniforms: OSG provides osg_ModelViewProjectionMatrix, osg_ModelViewMatrix, osg_NormalMatrix.
    // Attribute aliasing: OSG maps glVertexPointer->glVertexAttribPointer(0), etc., so custom shaders
    // receive vertex data through generic attribute slots instead of FFP-only arrays.
    // NOT on 3DS: NovaGL is fixed-function — glVertexAttribPointer is a stub,
    // aliasing would route all geometry into a black hole. FFP arrays only.
    if (auto* gc = mViewer->getCamera()->getGraphicsContext())
    {
        gc->getState()->setUseModelViewAndProjectionUniforms(true);
        gc->getState()->setUseVertexAttributeAliasing(true);
    }
#endif

    mViewer->getEventQueue()->getCurrentEventState()->setWindowRectangle(
        0, 0, graphicsWindow->getTraits()->width, graphicsWindow->getTraits()->height);
}

void OMW::Engine::setWindowIcon()
{
    std::ifstream windowIconStream;
    const auto windowIcon = mResDir / "openmw.png";
    windowIconStream.open(windowIcon, std::ios_base::in | std::ios_base::binary);
    if (windowIconStream.fail())
        Log(Debug::Error) << "Error: Failed to open " << windowIcon;
    osgDB::ReaderWriter* reader = osgDB::Registry::instance()->getReaderWriterForExtension("png");
    if (!reader)
    {
        Log(Debug::Error) << "Error: Failed to read window icon, no png readerwriter found";
        return;
    }
    osgDB::ReaderWriter::ReadResult result = reader->readImage(windowIconStream);
    if (!result.success())
        Log(Debug::Error) << "Error: Failed to read " << windowIcon << ": " << result.message() << " code "
                          << result.status();
    else
    {
        osg::ref_ptr<osg::Image> image = result.getImage();
        auto surface = SDLUtil::imageToSurface(image, true);
        SDL_SetWindowIcon(mWindow, surface.get());
    }
}

void OMW::Engine::prepareEngine()
{
    VITA_CRUMB("prepareEngine() enter");
#ifdef __3DS__
    // Force every osg::Geometry through VBOs. The GL2/FFP profile defaults
    // to client arrays (display lists are unsupported), which re-streams the
    // whole world's vertex data through NovaGL's per-frame rings — tens of
    // MB per frame, unpayable on the 3DS. With VBOs geometry uploads once
    // into linear memory. Must be set before the first frame initializes
    // osg::State (it reads the hint in initializeExtensionProcs).
    osg::DisplaySettings::instance()->setVertexBufferHint(osg::DisplaySettings::VERTEX_BUFFER_OBJECT);
#endif
    mStateManager = std::make_unique<MWState::StateManager>(mCfgMgr.getUserDataPath() / "saves", mContentFiles);
    mEnvironment.setStateManager(*mStateManager);

    const bool stereoEnabled = Settings::stereo().mStereoEnabled || osg::DisplaySettings::instance().get()->getStereo();
    mStereoManager = std::make_unique<Stereo::Manager>(
        mViewer, stereoEnabled, Settings::camera().mNearClip, Settings::camera().mViewingDistance);

    osg::ref_ptr<osg::Group> rootNode(new osg::Group);
    mViewer->setSceneData(rootNode);

    VITA_CRUMB("prepareEngine() createWindow");
#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Pre-createWindow");
#endif
    createWindow();

    mVFS = std::make_unique<VFS::Manager>();

#if defined(__vita__) || defined(__3DS__)
    {
        auto cacheDir = mCfgMgr.getUserConfigPath();
        if (mForceRescan)
        {
            Log(Debug::Info) << "Force rescan — clearing VFS caches";
            Files::clearScanCache(cacheDir / "scan_cache.bin");
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(cacheDir, ec))
                if (entry.path().filename().string().starts_with("vfs_dir_"))
                    std::filesystem::remove(entry.path(), ec);
        }
        VFS::registerArchives(mVFS.get(), mFileCollections, mArchives, true,
            &mEncoder.get()->getStatelessEncoder(), cacheDir);
    }
#else
    VFS::registerArchives(mVFS.get(), mFileCollections, mArchives, true, &mEncoder.get()->getStatelessEncoder());
#endif

    mResourceSystem = std::make_unique<Resource::ResourceSystem>(
        mVFS.get(), Settings::cells().mCacheExpiryDelay, &mEncoder.get()->getStatelessEncoder());
    mResourceSystem->getSceneManager()->getShaderManager().setMaxTextureUnits(mGlMaxTextureImageUnits);
#if defined(__vita__) || defined(__3DS__)
    mResourceSystem->getSceneManager()->setUnRefImageDataAfterApply(true); // release CPU-side image after GPU upload
#else
    mResourceSystem->getSceneManager()->setUnRefImageDataAfterApply(
        false); // keep to Off for now to allow better state sharing
#endif
    mResourceSystem->getSceneManager()->setFilterSettings(Settings::general().mTextureMagFilter,
        Settings::general().mTextureMinFilter, Settings::general().mTextureMipmap,
        static_cast<float>(Settings::general().mAnisotropy));
    mEnvironment.setResourceSystem(*mResourceSystem);

#ifdef __3DS__
    // Cell-preload workers belong on the New3DS app-exclusive core 2, just
    // below render priority; 128KB stack (NIF loading recurses). Falls back
    // to core 0 on Old3DS inside the wrapper (see ctr/CtrThreads.cpp).
    ctrSetThreadSpawnHint(2, 0x39, 128 * 1024);
#endif
    mWorkQueue = new SceneUtil::WorkQueue(Settings::cells().mPreloadNumThreads);
#ifdef __3DS__
    ctrClearThreadSpawnHint();
#endif
    mUnrefQueue = std::make_unique<SceneUtil::UnrefQueue>();

    mScreenCaptureOperation = new SceneUtil::AsyncScreenCaptureOperation(mWorkQueue,
        new SceneUtil::WriteScreenshotToFileOperation(mCfgMgr.getScreenshotPath(),
            Settings::general().mScreenshotFormat,
            Settings::general().mNotifyOnSavedScreenshot ? std::function<void(std::string)>(ScreenCaptureMessageBox{})
                                                         : std::function<void(std::string)>(IgnoreString{})));

    mScreenCaptureHandler = new osgViewer::ScreenCaptureHandler(mScreenCaptureOperation);

    mViewer->addEventHandler(mScreenCaptureHandler);

    mL10nManager = std::make_unique<L10n::Manager>(mVFS.get());
    mL10nManager->setPreferredLocales(Settings::general().mPreferredLocales, Settings::general().mGmstOverridesL10n);
    mEnvironment.setL10nManager(*mL10nManager);

    mLuaManager = std::make_unique<MWLua::LuaManager>(mVFS.get(), mResDir / "lua_libs");
    mEnvironment.setLuaManager(*mLuaManager);

    // Create input and UI first to set up a bootstrapping environment for
    // showing a loading screen and keeping the window responsive while doing so

    const auto keybinderUser = mCfgMgr.getUserConfigPath() / "input_v3.xml";
    bool keybinderUserExists = std::filesystem::exists(keybinderUser);
    if (!keybinderUserExists)
    {
        const auto input2 = (mCfgMgr.getUserConfigPath() / "input_v2.xml");
        if (std::filesystem::exists(input2))
        {
            keybinderUserExists = std::filesystem::copy_file(input2, keybinderUser);
            Log(Debug::Info) << "Loading keybindings file: " << keybinderUser;
        }
    }
    else
        Log(Debug::Info) << "Loading keybindings file: " << keybinderUser;

    const auto userdefault = mCfgMgr.getUserConfigPath() / "gamecontrollerdb.txt";
    const auto localdefault = mCfgMgr.getLocalPath() / "gamecontrollerdb.txt";

    std::filesystem::path userGameControllerdb;
    if (std::filesystem::exists(userdefault))
        userGameControllerdb = userdefault;

    std::filesystem::path gameControllerdb;
    if (std::filesystem::exists(localdefault))
        gameControllerdb = localdefault;
    else if (!mCfgMgr.getGlobalPath().empty())
    {
        const auto globaldefault = mCfgMgr.getGlobalPath() / "gamecontrollerdb.txt";
        if (std::filesystem::exists(globaldefault))
            gameControllerdb = globaldefault;
    }
    // else if it doesn't exist, pass in an empty path

    // gui needs our shaders path before everything else
    mResourceSystem->getSceneManager()->setShaderPath(mResDir / "shaders");

    osg::GLExtensions& exts = SceneUtil::getGLExtensions();
    bool shadersSupported = exts.glslLanguageVersion >= 1.2f;
#if defined(__vita__) || defined(__3DS__)
    // vitaGL cannot compile OpenMW's GLSL shaders — force fixed-function path
    shadersSupported = false;
#endif

#if OSG_VERSION_LESS_THAN(3, 6, 6)
    // hack fix for https://github.com/openscenegraph/OpenSceneGraph/issues/1028
    if (!osg::isGLExtensionSupported(exts.contextID, "NV_framebuffer_multisample_coverage"))
        exts.glRenderbufferStorageMultisampleCoverageNV = nullptr;
#endif

    osg::ref_ptr<osg::Group> guiRoot = new osg::Group;
    guiRoot->setName("GUI Root");
    guiRoot->setNodeMask(MWRender::Mask_GUI);
    mStereoManager->disableStereoForNode(guiRoot);
    rootNode->addChild(guiRoot);

#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Post-VFS");
#endif
    VITA_CRUMB("prepareEngine() creating WindowManager");
    mWindowManager = std::make_unique<MWGui::WindowManager>(mWindow, mViewer, guiRoot, mResourceSystem.get(),
        mWorkQueue.get(), mCfgMgr.getLogPath(), mScriptConsoleMode, mTranslationDataStorage, mEncoding, mExportFonts,
        Version::getOpenmwVersionDescription(), shadersSupported, mCfgMgr);
    mEnvironment.setWindowManager(*mWindowManager);

    VITA_CRUMB("prepareEngine() creating InputManager");
    mInputManager = std::make_unique<MWInput::InputManager>(mWindow, mViewer, mScreenCaptureHandler, keybinderUser,
        keybinderUserExists, userGameControllerdb, gameControllerdb, mGrab);
    mEnvironment.setInputManager(*mInputManager);

    // Create sound system
#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Post-WindowManager");
#endif
    VITA_CRUMB("prepareEngine() creating SoundManager");
#ifdef __3DS__
    // The OpenAL mixer thread goes to syscore (core 1) at high priority —
    // audio underruns are audible, and APT_SetAppCpuTimeLimit (CtrInit)
    // reserves the one app-thread slot there. 64KB stack for the mixer.
    ctrSetThreadSpawnHint(1, 0x20, 64 * 1024);
#endif
    mSoundManager = std::make_unique<MWSound::SoundManager>(mVFS.get(), mUseSound);
#ifdef __3DS__
    ctrClearThreadSpawnHint();
#endif
    mEnvironment.setSoundManager(*mSoundManager);

    // Create the world
    VITA_CRUMB("prepareEngine() creating World");
#ifdef __3DS__
    // Physics worker (and any navmesh updater threads spawned with it) run
    // on core 2 just below main-thread priority. Generous stack: Bullet's
    // collision dispatch recurses much deeper than the 32KB glue default.
    ctrSetThreadSpawnHint(2, 0x31, 192 * 1024);
#endif
    mWorld = std::make_unique<MWWorld::World>(
        mResourceSystem.get(), mActivationDistanceOverride, mCellName, mCfgMgr.getUserDataPath());
#ifdef __3DS__
    ctrClearThreadSpawnHint();
#endif
    mEnvironment.setWorld(*mWorld);
    mEnvironment.setWorldModel(mWorld->getWorldModel());
    mEnvironment.setESMStore(mWorld->getStore());

    Loading::Listener* listener = MWBase::Environment::get().getWindowManager()->getLoadingScreen();
    Loading::AsyncListener asyncListener(*listener);
#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Pre-data-load");
    VITA_CRUMB("prepareEngine() loading data sync");
    // Load data synchronously on Vita — saves async thread stack (~1MB)
    // Pass listener directly (not asyncListener) since loading is synchronous.
    // AsyncListener buffers updates for cross-thread use, but nobody calls update()
    // on the main thread during sync loading, so the loading screen never draws.
    listener->loadingOn();
    mWorld->loadData(mFileCollections, mContentFiles, mGroundcoverFiles, mEncoder.get(), listener);
    listener->loadingOff();
    Files::saveScanCache(
        mCfgMgr.getUserConfigPath() / "scan_cache.bin", mDataDirs, mFileCollections.getCollections());
    Vita::logMemoryStatus("Post-data-load");
#else
    VITA_CRUMB("prepareEngine() loading data async");
    auto dataLoading = std::async(std::launch::async,
        [&] { mWorld->loadData(mFileCollections, mContentFiles, mGroundcoverFiles, mEncoder.get(), &asyncListener); });

    if (!mSkipMenu)
    {
        std::string_view logo = Fallback::Map::getString("Movies_Company_Logo");
        if (!logo.empty())
            mWindowManager->playVideo(logo, true);
    }

    listener->loadingOn();
    {
        using namespace std::chrono_literals;
        while (dataLoading.wait_for(50ms) != std::future_status::ready)
            asyncListener.update();
        dataLoading.get();
    }
    listener->loadingOff();
#endif
    VITA_CRUMB("prepareEngine() data loaded");

    VITA_CRUMB("prepareEngine() world init");
#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Pre-World::init");
#endif
    mWorld->init(mMaxRecastLogLevel, mViewer, std::move(rootNode), mWorkQueue.get(), *mUnrefQueue);
#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Post-World::init");
#endif
    VITA_CRUMB("prepareEngine() world init done");
    mEnvironment.setWorldScene(mWorld->getWorldScene());
    mWorld->setupPlayer();
    mWorld->setRandomSeed(mRandomSeed);

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
#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Pre-initUI");
#endif
    mWindowManager->initUI();
#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Post-initUI");
#endif

    // Load translation data
    mTranslationDataStorage.setEncoder(mEncoder.get());
    for (auto& mContentFile : mContentFiles)
    {
        VITA_CRUMB(("translation: " + mContentFile).c_str());
        mTranslationDataStorage.loadTranslationData(mFileCollections, mContentFile);
    }
    VITA_CRUMB("translation done");

    Compiler::registerExtensions(mExtensions);

    // Create script system
    mScriptContext = std::make_unique<MWScript::CompilerContext>(MWScript::CompilerContext::Type_Full);
    mScriptContext->setExtensions(&mExtensions);

    mScriptManager = std::make_unique<MWScript::ScriptManager>(mWorld->getStore(), *mScriptContext, mWarningsMode);
    mEnvironment.setScriptManager(*mScriptManager);
    VITA_CRUMB("ScriptManager done");

    // Create game mechanics system
    mMechanicsManager = std::make_unique<MWMechanics::MechanicsManager>();
    mEnvironment.setMechanicsManager(*mMechanicsManager);
    VITA_CRUMB("MechanicsManager done");

    // Create dialog system
    mJournal = std::make_unique<MWDialogue::Journal>();
    mEnvironment.setJournal(*mJournal);

    mDialogueManager = std::make_unique<MWDialogue::DialogueManager>(mExtensions, mTranslationDataStorage);
    mEnvironment.setDialogueManager(*mDialogueManager);
    VITA_CRUMB("DialogueManager done");

    // scripts
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

#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Pre-LuaInit");
#endif
    mLuaManager->loadPermanentStorage(mCfgMgr.getUserConfigPath());
    mLuaManager->init();

    // starts a separate lua thread if "lua num threads" > 0
    mLuaWorker = std::make_unique<MWLua::Worker>(*mLuaManager);
#if defined(__vita__) || defined(__3DS__)
    Vita::logMemoryStatus("Post-LuaInit");
#endif
    VITA_CRUMB("prepareEngine() done");
}

// Initialise and enter main loop.
void OMW::Engine::go()
{
    assert(!mContentFiles.empty());

    Log(Debug::Info) << "OSG version: " << osgGetVersion();
    SDL_version sdlVersion;
    SDL_GetVersion(&sdlVersion);
    Log(Debug::Info) << "SDL version: " << (int)sdlVersion.major << "." << (int)sdlVersion.minor << "."
                     << (int)sdlVersion.patch;

    Misc::Rng::init(mRandomSeed);

    Settings::ShaderManager::get().load(mCfgMgr.getUserConfigPath() / "shaders.yaml");

    MWClass::registerClasses();

    // Create encoder
    mEncoder = std::make_unique<ToUTF8::Utf8Encoder>(mEncoding);

    // Setup viewer
    VITA_CRUMB("go() creating viewer");
    mViewer = new osgViewer::Viewer;
    mViewer->setReleaseContextAtEndOfFrameHint(false);

#if defined(__vita__) || defined(__3DS__)
    // DrawThreadPerContext crashes on launch — vitaGL/SceGxm isn't safe
    // for draw submission from a non-main thread.
    mViewer->setThreadingModel(osgViewer::ViewerBase::SingleThreaded);
    // Cheapest sort — skip per-frame state sorting in render bins.
    osgUtil::RenderBin::setDefaultRenderBinSortMode(osgUtil::RenderBin::TRAVERSAL_ORDER);
#endif

    // Do not try to outsmart the OS thread scheduler (see bug #4785).
    mViewer->setUseConfigureAffinity(false);

    mEnvironment.setFrameRateLimit(Settings::video().mFramerateLimit);

    prepareEngine();

#ifdef _WIN32
    const auto* statsFile = _wgetenv(L"OPENMW_OSG_STATS_FILE");
#else
    const auto* statsFile = std::getenv("OPENMW_OSG_STATS_FILE");
#endif

    std::filesystem::path path;
    if (statsFile != nullptr)
        path = statsFile;

    std::ofstream stats;
    if (!path.empty())
    {
        stats.open(path, std::ios_base::out);
        if (stats.is_open())
            Log(Debug::Info) << "OSG stats will be written to: " << path;
        else
            Log(Debug::Warning) << "Failed to open file to write OSG stats \"" << path
                                << "\": " << std::generic_category().message(errno);
    }

#ifndef __3DS__
    // Setup profiler
    osg::ref_ptr<Resource::Profiler> statsHandler = new Resource::Profiler(stats.is_open(), *mVFS);

    initStatsHandler(*statsHandler);

    mViewer->addEventHandler(statsHandler);

    osg::ref_ptr<Resource::StatsHandler> resourcesHandler = new Resource::StatsHandler(stats.is_open(), *mVFS);
    mViewer->addEventHandler(resourcesHandler);

    if (stats.is_open())
        Resource::collectStatistics(*mViewer);
#else
    // 3DS: the F3/F4 stats overlays build osgText fonts on a heap that is
    // already scraping bottom — several MB and minutes of boot for a debug
    // HUD nobody can open. Skipped.
    VITA_CRUMB("go() profiler skipped");
#endif

    // Start the game
    VITA_CRUMB("go() starting game");
    if (!mSaveGameFile.empty())
    {
        mStateManager->loadGame(mSaveGameFile);
    }
    else if (!mSkipMenu)
    {
        // start in main menu
        VITA_CRUMB("go() pushing main menu");
        mWindowManager->pushGuiMode(MWGui::GM_MainMenu);

        if (mVFS->exists(MWSound::titleMusic))
            mSoundManager->streamMusic(MWSound::titleMusic, MWSound::MusicType::Normal);
        else
            Log(Debug::Warning) << "Title music not found";

        std::string_view logo = Fallback::Map::getString("Movies_Morrowind_Logo");
        if (!logo.empty())
            mWindowManager->playVideo(logo, /*allowSkipping*/ true, /*overrideSounds*/ false);
    }
    else
    {
#ifdef __3DS__
        // Menu skipped: whatever the load path cached is dead weight now.
        // Hand it back before the cell load asks for contiguous runs.
        // (Deferring newGame past a few rendered frames was tried and made
        // things WORSE: first-frame machinery allocated ~10MB and uploaded
        // nothing, because no visible widgets meant no texture applies.)
        mResourceSystem->clearCache();
        malloc_trim(0);
        Vita::logMemoryStatus("Pre-newGame");
#endif
        mStateManager->newGame(!mNewGame);
    }

    if (!mStartupScript.empty() && mStateManager->getState() == MWState::StateManager::State_Running)
    {
        mWindowManager->executeInConsole(mStartupScript);
    }

    // Start the main rendering loop
    VITA_CRUMB("go() entering main loop");
    MWWorld::DateTimeManager& timeManager = *mWorld->getTimeManager();
    Misc::FrameRateLimiter frameRateLimiter = Misc::makeFrameRateLimiter(mEnvironment.getFrameRateLimit());
    const std::chrono::steady_clock::duration maxSimulationInterval(std::chrono::milliseconds(200));
#if defined(__vita__) || defined(__3DS__)
    constexpr float kDynFogMin = 1500.f;
    constexpr float kDynFogMinInterior = 700.f;
    constexpr float kDynFogMax = 5000.f;
    constexpr float kDynFogUserMoveThreshold = 300.f;
    constexpr float kDynFogDeadband = 0.5f;
    constexpr float kDynFogEmaAlpha = 0.15f;
    constexpr float kDynFogLerpHz = 4.0f;
    constexpr auto kDynFogAdjustInterval = std::chrono::milliseconds(250);
    float vitaDynFogTarget = Settings::camera().mViewingDistance;
    float vitaDynFogLastWritten = Settings::camera().mViewingDistance;
    bool vitaDynFogWasOn = false;
    // Seed EMA with the configured target period so the first tick isn't biased.
    float vitaDynFogEmaDt = 1.0f / std::max(15.f, Settings::camera().mVitaDynFogTargetFps.get());
    auto vitaDynFogLastAdjust = std::chrono::steady_clock::now();
    bool vitaDynFogWasRunning = false;
    bool vitaDynFogWasInInterior = false;
#endif
    while (!mViewer->done() && !mStateManager->hasQuitRequest())
    {
#ifdef __3DS__
        // THE frozen-simulation fix. frameRateLimiter.limit() is bypassed on
        // 3DS (NovaGL paces the swap), but getLastFrameDuration() is only
        // ever updated inside limit() — so dt stayed 0.0 FOREVER: simulation
        // time frozen, every mwscript timer dead (the chargen intro's
        // Say/name-menu chain never started), the player immovable while
        // input worked. Measure the frame duration ourselves instead.
        static std::chrono::steady_clock::time_point s_prevFrameStart = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point frameNow = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::duration measuredFrameDuration = frameNow - s_prevFrameStart;
        s_prevFrameStart = frameNow;
        const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(
                              std::min(measuredFrameDuration, maxSimulationInterval))
                              .count()
            * timeManager.getSimulationTimeScale();
#else
        const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(
                              std::min(frameRateLimiter.getLastFrameDuration(), maxSimulationInterval))
                              .count()
            * timeManager.getSimulationTimeScale();
#endif

        mViewer->advance(timeManager.getRenderingSimulationTime());

        const unsigned frameNumber = mViewer->getFrameStamp()->getFrameNumber();

        if (!frame(frameNumber, static_cast<float>(dt)))
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

        if (stats)
        {
            // The delay is required because rendering happens in parallel to the main thread and stats from there is
            // available with delay.
            constexpr unsigned statsReportDelay = 3;
            if (frameNumber >= statsReportDelay)
            {
                // Viewer frame number can be different from frameNumber because of loading screens which render new
                // frames inside a simulation frame.
                const unsigned currentFrameNumber = mViewer->getFrameStamp()->getFrameNumber();
                for (unsigned i = frameNumber; i <= currentFrameNumber; ++i)
                    reportStats(i - statsReportDelay, *mViewer, stats);
            }
        }

#if defined(__vita__) || defined(__3DS__)
        {
#ifdef __3DS__
            // The limiter is bypassed on 3DS — use the measured duration.
            double frameDt = std::chrono::duration<double>(measuredFrameDuration).count();
#else
            double frameDt = std::chrono::duration<double>(
                frameRateLimiter.getLastFrameDuration()).count();
#endif
            // Skip cell-load / pause spikes so they don't poison the fps EMA.
            if (frameDt > 0.001 && frameDt < 0.2)
                vitaDynFogEmaDt = (1.0f - kDynFogEmaAlpha) * vitaDynFogEmaDt
                    + kDynFogEmaAlpha * static_cast<float>(frameDt);

            bool dynFogOn = Settings::camera().mVitaDynamicFog;
            const float dynFogTargetFps
                = std::max(15.f, Settings::camera().mVitaDynFogTargetFps.get());
            auto pickFloor = [&]() -> std::pair<float, float> {
                struct Tier { float fps, ext, intr; };
                static constexpr Tier kTable[] = {
                    { 15.f, 2000.f, 1000.f },
                    { 18.f, 1700.f,  850.f },
                    { 20.f, 1500.f,  700.f },
                };
                const Tier* pick = &kTable[2]; // default "20 (Balanced)"
                float best = std::abs(dynFogTargetFps - kTable[2].fps);
                for (const Tier& t : kTable)
                {
                    float d = std::abs(dynFogTargetFps - t.fps);
                    if (d < best) { best = d; pick = &t; }
                }
                return { pick->ext, pick->intr };
            };
            const auto [floorExt, floorInt] = pickFloor();
            const bool isRunning = (mStateManager->getState() == MWBase::StateManager::State_Running);
            bool isInInterior = false;
            float effectiveMin = floorExt;
            if (isRunning)
            {
                MWWorld::Ptr player = mWorld->getPlayerPtr();
                MWWorld::CellStore* cell = player.getCell();
                if (cell && !cell->isExterior())
                {
                    isInInterior = true;
                    effectiveMin = floorInt;
                }
            }

            // Snap fog to the floor so it always *grows* from tight toward the
            const bool dynFogJustEnabled = dynFogOn && !vitaDynFogWasOn;
            const bool justLoadedGame = dynFogOn && isRunning && !vitaDynFogWasRunning;
            const bool enteredInterior = dynFogOn && isInInterior && !vitaDynFogWasInInterior;
            if (dynFogJustEnabled || justLoadedGame || enteredInterior)
            {
                vitaDynFogTarget = effectiveMin;
                vitaDynFogLastWritten = effectiveMin;
                Settings::camera().mViewingDistance.set(effectiveMin);
                static const Settings::CategorySettingVector kFilter{ { "Camera", "viewing distance" } };
                const auto changes = Settings::Manager::getPendingChanges(kFilter);
                if (!changes.empty())
                {
                    mWorld->processChangedSettings(changes);
                    Settings::Manager::resetPendingChanges(kFilter);
                }
                vitaDynFogLastAdjust = std::chrono::steady_clock::now();
            }
            vitaDynFogWasOn = dynFogOn;
            vitaDynFogWasRunning = isRunning;
            vitaDynFogWasInInterior = isInInterior;

            if (dynFogOn)
            {
                float current = Settings::camera().mViewingDistance;
                // Compare to what we last wrote, not to target — the lerp lags target naturally.
                if (std::abs(current - vitaDynFogLastWritten) > kDynFogUserMoveThreshold)
                {
                    Settings::camera().mVitaDynamicFog.set(false);
                    vitaDynFogWasOn = false;
                }
                else
                {
                    // Resolve aggression profile each tick (cheap, allows live UI changes)
                    const std::string& aggStr = Settings::camera().mVitaDynFogAggression.get();
                    float shrinkCoef = 50.f, shrinkMaxAbs = 500.f, shrinkLerpHz = 6.f; // aggressive default
                    if (aggStr == "normal")
                    {
                        shrinkCoef = 30.f; shrinkMaxAbs = 300.f; shrinkLerpHz = 4.f;
                    }
                    else if (aggStr == "very aggressive")
                    {
                        shrinkCoef = 80.f; shrinkMaxAbs = 800.f; shrinkLerpHz = 8.f;
                    }

                    auto now = std::chrono::steady_clock::now();
                    if (now - vitaDynFogLastAdjust >= kDynFogAdjustInterval)
                    {
                        vitaDynFogLastAdjust = now;
                        float fps = 1.0f / vitaDynFogEmaDt;
                        float fpsGap = fps - dynFogTargetFps;
                        float step = 0.f;
                        if (fpsGap < -kDynFogDeadband)
                        {
                            // Two-tier shrink: catastrophic snap is fixed (always
                            // saves you), proportional response scales with the
                            // aggression setting.
                            if (fps < 15.f)
                                step = -2000.f;
                            else
                                step = std::clamp(fpsGap * shrinkCoef, -shrinkMaxAbs, -20.f);
                        }
                        else if (fpsGap > kDynFogDeadband)
                            step = std::clamp(fpsGap * 20.f, 20.f, 150.f);
                        vitaDynFogTarget
                            = std::clamp(vitaDynFogTarget + step, effectiveMin, kDynFogMax);
                    }


                    const float targetDelta = vitaDynFogTarget - current;
                    float lerpHz = kDynFogLerpHz;
                    if (targetDelta < -1200.f)
                        lerpHz = 16.f;
                    else if (targetDelta < 0.f && targetDelta > -1200.f)
                        lerpHz = shrinkLerpHz; // proportional shrink uses aggression-tuned rate
                    float lerpT = 1.0f - std::exp(-lerpHz * static_cast<float>(frameDt));
                    float newDist = current + targetDelta * lerpT;
                    if (std::abs(newDist - current) > 0.5f)
                    {
                        Settings::camera().mViewingDistance.set(newDist);
                        vitaDynFogLastWritten = newDist;
                        static const Settings::CategorySettingVector kFilter{
                            { "Camera", "viewing distance" } };
                        const auto changes
                            = Settings::Manager::getPendingChanges(kFilter);
                        if (!changes.empty())
                        {
                            mWorld->processChangedSettings(changes);
                            Settings::Manager::resetPendingChanges(kFilter);
                        }
                    }
                }
            }
        }
#endif
#ifndef __3DS__
        frameRateLimiter.limit();
#else
        // 3DS: limiter disabled while std::this_thread::sleep_for /
        // steady_clock behaviour on devkitARM is unverified (suspected cause
        // of the 0-FPS menu). NovaGL vsync paces real hardware.
        {
            static unsigned s_frameTick = 0;
            if ((++s_frameTick & 31) == 1)
            {
                static std::chrono::steady_clock::time_point s_lastTick = std::chrono::steady_clock::now();
                const auto now = std::chrono::steady_clock::now();
                const double dt = std::chrono::duration<double>(now - s_lastTick).count();
                s_lastTick = now;
                const double fps = dt > 0.0 ? 32.0 / dt : 0.0;
                char buf[96];
                snprintf(buf, sizeof(buf),
                    "[FRAME] n=%u fps=%d.%d upd=%dms rt=%dms ph=%d/%d/%d/%d/%d/%d/%d", s_frameTick,
                    static_cast<int>(fps), static_cast<int>(fps * 10) % 10, static_cast<int>(s_updAccMs / 32.0),
                    static_cast<int>(s_rtAccMs / 32.0), static_cast<int>(s_phAccMs[0] / 32.0),
                    static_cast<int>(s_phAccMs[1] / 32.0), static_cast<int>(s_phAccMs[2] / 32.0),
                    static_cast<int>(s_phAccMs[3] / 32.0), static_cast<int>(s_phAccMs[4] / 32.0),
                    static_cast<int>(s_phAccMs[5] / 32.0), static_cast<int>(s_phAccMs[6] / 32.0));
                s_updAccMs = 0.0;
                s_rtAccMs = 0.0;
                for (int phi = 0; phi < 8; ++phi)
                    s_phAccMs[phi] = 0.0;
                Vita::breadcrumb(buf);
            }
        }
#endif
    }

    mLuaWorker->join();

    // Save user settings
    Settings::Manager::saveUser(mCfgMgr.getUserConfigPath() / "settings.cfg");
    Settings::ShaderManager::get().save();
    mLuaManager->savePermanentStorage(mCfgMgr.getUserConfigPath());

#if defined(__vita__) || defined(__3DS__)
    // Skip C++ static destructors — vitaGL/OSG/Bullet teardown paths can hang
    // on shutdown, leaving the app stuck instead of returning to LiveArea.
    // Saves above are already complete; nothing else needs to run for a clean
    // exit from the user's perspective.
#ifdef __vita__
    sceKernelExitProcess(0);
#else
    std::_Exit(0);
#endif
#endif
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
