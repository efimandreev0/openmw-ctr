#include "sdlgraphicswindow.hpp"

#include <SDL_video.h>

#ifdef __vita__
#include <vitaGL.h>
#endif
#ifdef __3DS__
#include <GL/gl.h>
// NovaGL frame model: novaBeginEye(eye) selects the render target for the
// frame, novaSwapBuffers() presents. OpenMW renders a single "eye".
extern "C" void novaBeginEye(int eye);
extern "C" void novaSwapBuffers(void);
#endif

#ifdef OPENMW_GL4ES_MANUAL_INIT
#include "gl4esinit.h"
#endif

namespace SDLUtil
{

    GraphicsWindowSDL2::~GraphicsWindowSDL2()
    {
        close(true);
    }

    GraphicsWindowSDL2::GraphicsWindowSDL2(osg::GraphicsContext::Traits* traits, VSyncMode vsyncMode)
        : mWindow(nullptr)
        , mContext(nullptr)
        , mValid(false)
        , mRealized(false)
        , mOwnsWindow(false)
        , mVSyncMode(vsyncMode)
    {
        _traits = traits;

        init();
        if (GraphicsWindowSDL2::valid())
        {
            setState(new osg::State);
            getState()->setGraphicsContext(this);

            if (_traits.valid() && _traits->sharedContext.valid())
            {
                getState()->setContextID(_traits->sharedContext->getState()->getContextID());
                incrementContextIDUsageCount(getState()->getContextID());
            }
            else
            {
                getState()->setContextID(osg::GraphicsContext::createNewContextID());
            }
        }
    }

    bool GraphicsWindowSDL2::setWindowDecorationImplementation(bool flag)
    {
        if (!mWindow)
            return false;

        SDL_SetWindowBordered(mWindow, flag ? SDL_TRUE : SDL_FALSE);
        return true;
    }

    bool GraphicsWindowSDL2::setWindowRectangleImplementation(int x, int y, int width, int height)
    {
        if (!mWindow)
            return false;

#if defined(__vita__) || defined(__3DS__)
        // Fixed resolution on Vita
        (void)x; (void)y; (void)width; (void)height;
#else
        int w, h;
        SDL_GetWindowSize(mWindow, &w, &h);
        int dw, dh;
        SDL_GL_GetDrawableSize(mWindow, &dw, &dh);

        SDL_SetWindowPosition(mWindow, x, y);
        SDL_SetWindowSize(mWindow, width / (dw / w), height / (dh / h));
#endif
        return true;
    }

    void GraphicsWindowSDL2::setWindowName(const std::string& name)
    {
        if (!mWindow)
            return;

        SDL_SetWindowTitle(mWindow, name.c_str());
        _traits->windowName = name;
    }

    void GraphicsWindowSDL2::setCursor(MouseCursor mouseCursor)
    {
        _traits->useCursor = false;
    }

    void GraphicsWindowSDL2::init()
    {
        if (mValid)
            return;

        if (!_traits.valid())
            return;

        WindowData* inheritedWindowData = dynamic_cast<WindowData*>(_traits->inheritedWindowData.get());
        mWindow = inheritedWindowData ? inheritedWindowData->mWindow : nullptr;

        mOwnsWindow = (mWindow == nullptr);
        if (mOwnsWindow)
        {
            OSG_FATAL << "Error: No SDL window provided." << std::endl;
            return;
        }

#if defined(__vita__)
        // vitaGL is initialized in Vita::initialize() before SDL_Init.
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);

        _traits->width = 640;
        _traits->height = 368;
        _traits->red = 8;
        _traits->green = 8;
        _traits->blue = 8;
        _traits->alpha = 8;
        _traits->depth = 32;
        _traits->stencil = 8;
        _traits->doubleBuffer = true;
        _traits->sampleBuffers = 0;
        _traits->samples = 0;
#elif defined(__3DS__)
        // NovaGL is initialized in Vita::initialize() (CtrInit) before SDL_Init.
        novaBeginEye(0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        novaSwapBuffers();
        novaBeginEye(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        novaSwapBuffers();
        novaBeginEye(0);

        _traits->width = 400;
        _traits->height = 240;
        _traits->red = 8;
        _traits->green = 8;
        _traits->blue = 8;
        _traits->alpha = 8;
        _traits->depth = 24;
        _traits->stencil = 8;
        _traits->doubleBuffer = true;
        _traits->sampleBuffers = 0;
        _traits->samples = 0;
#else
        // SDL will change the current context when it creates a new one, so we
        // have to get the current one to be able to restore it afterward.
        SDL_Window* oldWin = SDL_GL_GetCurrentWindow();
        SDL_GLContext oldCtx = SDL_GL_GetCurrentContext();

#if defined(ANDROID) || defined(OPENMW_GL4ES_MANUAL_INIT)
        int major = 2;
        int minor = 0;

        if (const char* const version_env = getenv("OPENMW_GLES_VERSION"))
        {
            const std::string_view version(version_env);

            if (version == "2" || version == "20")
            {
                major = 2;
                minor = 0;
            }
            else if (version == "30")
            {
                major = 3;
                minor = 0;
            }
            else if (version == "31")
            {
                major = 3;
                minor = 1;
            }
            else if (version == "3" || version == "32")
            {
                major = 3;
                minor = 2;
            }
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
#endif

        mContext = SDL_GL_CreateContext(mWindow);
        if (!mContext)
        {
            OSG_FATAL << "Error: Unable to create OpenGL graphics context: " << SDL_GetError() << std::endl;
            return;
        }

#ifdef OPENMW_GL4ES_MANUAL_INIT
        openmw_gl4es_init(mWindow);
#endif

        setSwapInterval(mVSyncMode);

        // Update traits with what we've actually been given
        // Use intermediate to avoid signed/unsigned mismatch
        int intermediateLocation;
        SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &intermediateLocation);
        _traits->red = intermediateLocation;
        SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &intermediateLocation);
        _traits->green = intermediateLocation;
        SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &intermediateLocation);
        _traits->blue = intermediateLocation;
        SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &intermediateLocation);
        _traits->alpha = intermediateLocation;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &intermediateLocation);
        _traits->depth = intermediateLocation;
        SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &intermediateLocation);
        _traits->stencil = intermediateLocation;

        SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &intermediateLocation);
        _traits->doubleBuffer = intermediateLocation;
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &intermediateLocation);
        _traits->sampleBuffers = intermediateLocation;
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &intermediateLocation);
        _traits->samples = intermediateLocation;

        SDL_GL_MakeCurrent(oldWin, oldCtx);
#endif // !__vita__

        mValid = true;

        getEventQueue()->syncWindowRectangleWithGraphicsContext();
    }

    bool GraphicsWindowSDL2::realizeImplementation()
    {
        if (mRealized)
        {
            OSG_NOTICE << "GraphicsWindowSDL2::realizeImplementation() Already realized" << std::endl;
            return true;
        }

        if (!mValid)
            init();
        if (!mValid)
            return false;

        SDL_ShowWindow(mWindow);

        getEventQueue()->syncWindowRectangleWithGraphicsContext();

        mRealized = true;

        return true;
    }

    bool GraphicsWindowSDL2::makeCurrentImplementation()
    {
        if (!mRealized)
        {
            OSG_WARN << "Warning: GraphicsWindow not realized, cannot do makeCurrent." << std::endl;
            return false;
        }

#if defined(__vita__) || defined(__3DS__)
        return true;
#else
        return SDL_GL_MakeCurrent(mWindow, mContext) == 0;
#endif
    }

    bool GraphicsWindowSDL2::releaseContextImplementation()
    {
        if (!mRealized)
        {
            OSG_WARN << "Warning: GraphicsWindow not realized, cannot do releaseContext." << std::endl;
            return false;
        }

#if defined(__vita__) || defined(__3DS__)
        return true;
#else
        return SDL_GL_MakeCurrent(nullptr, nullptr) == 0;
#endif
    }

    void GraphicsWindowSDL2::closeImplementation()
    {
#if !defined(__vita__) && !defined(__3DS__)
        if (mContext)
            SDL_GL_DeleteContext(mContext);
#endif
        mContext = nullptr;

        if (mWindow && mOwnsWindow)
            SDL_DestroyWindow(mWindow);
        mWindow = nullptr;

        mValid = false;
        mRealized = false;
    }

    void GraphicsWindowSDL2::swapBuffersImplementation()
    {
        if (!mRealized)
            return;

#if defined(__vita__)
        vglSwapBuffers(GL_FALSE);
#elif defined(__3DS__)
        novaSwapBuffers();
        novaBeginEye(0); // open the next frame's render pass
#else
        SDL_GL_SwapWindow(mWindow);
#endif
    }

    void GraphicsWindowSDL2::setSyncToVBlank(bool on)
    {
        throw std::runtime_error(
            "setSyncToVBlank with bool argument is not supported. Use the VSyncMode argument instead.");
    }

    void GraphicsWindowSDL2::setSyncToVBlank(VSyncMode mode)
    {
#if !defined(__vita__) && !defined(__3DS__)
        SDL_Window* oldWin = SDL_GL_GetCurrentWindow();
        SDL_GLContext oldCtx = SDL_GL_GetCurrentContext();

        SDL_GL_MakeCurrent(mWindow, mContext);

        setSwapInterval(mode);

        SDL_GL_MakeCurrent(oldWin, oldCtx);
#else
        setSwapInterval(mode);
#endif
    }

    void GraphicsWindowSDL2::setSwapInterval(VSyncMode mode)
    {
        mVSyncMode = mode;

#if defined(__vita__) || defined(__3DS__)
        // vitaGL handles vsync via vglSwapBuffers parameter
        (void)mode;
#else
        if (mode == VSyncMode::Adaptive)
        {
            if (SDL_GL_SetSwapInterval(-1) == -1)
            {
                OSG_NOTICE << "Adaptive vsync unsupported" << std::endl;
                setSwapInterval(VSyncMode::Enabled);
            }
        }
        else if (mode == VSyncMode::Enabled)
        {
            if (SDL_GL_SetSwapInterval(1) == -1)
            {
                OSG_NOTICE << "Vertical synchronization unsupported, disabling" << std::endl;
                setSwapInterval(VSyncMode::Disabled);
            }
        }
        else
        {
            SDL_GL_SetSwapInterval(0);
        }
#endif
    }

    void GraphicsWindowSDL2::raiseWindow()
    {
        SDL_RaiseWindow(mWindow);
    }

}
