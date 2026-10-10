
#include <SDL.h>

#include <3ds.h>

#include <cstdlib>
#include <cstring>
#include <string>

struct _SDL_GameController
{
    int mDummy;
};

struct _SDL_Joystick
{
    int mDummy;
};

namespace
{
    constexpr int sWindowWidth = 400;
    constexpr int sWindowHeight = 240;

    constexpr int sQueueSize = 256;
    SDL_Event sQueue[sQueueSize];
    int sQueueHead = 0;
    int sQueueCount = 0;

    _SDL_GameController sController;
    _SDL_Joystick sJoystick;

    int sMouseX = sWindowWidth / 2;
    int sMouseY = sWindowHeight / 2;
    bool sTextInput = false;
    std::string sClipboard;

    SDL_Event& queueAt(int index)
    {
        return sQueue[(sQueueHead + index) % sQueueSize];
    }

    void removeAt(int index)
    {
        for (int i = index; i + 1 < sQueueCount; ++i)
            queueAt(i) = queueAt(i + 1);
    }

    void trackMouse(const SDL_Event& event)
    {
        if (event.type == SDL_MOUSEMOTION)
        {
            sMouseX = event.motion.x;
            sMouseY = event.motion.y;
        }
        else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)
        {
            sMouseX = event.button.x;
            sMouseY = event.button.y;
        }
    }

    u32 controllerKey(SDL_GameControllerButton button)
    {
        switch (button)
        {
            case SDL_CONTROLLER_BUTTON_A:
                return KEY_A;
            case SDL_CONTROLLER_BUTTON_B:
                return KEY_B;
            case SDL_CONTROLLER_BUTTON_X:
                return KEY_X;
            case SDL_CONTROLLER_BUTTON_Y:
                return KEY_Y;
            case SDL_CONTROLLER_BUTTON_BACK:
                return KEY_SELECT;
            case SDL_CONTROLLER_BUTTON_START:
                return KEY_START;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                return KEY_L;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                return KEY_R;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                return KEY_DUP;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                return KEY_DDOWN;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                return KEY_DLEFT;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                return KEY_DRIGHT;
            default:
                return 0;
        }
    }
}

extern "C"
{

    const char* SDL_GetError(void)
    {
        return "";
    }

    void SDL_free(void* mem)
    {
        std::free(mem);
    }


    void SDL_PumpEvents(void) {}

    int SDL_PushEvent(SDL_Event* event)
    {
        if (sQueueCount == sQueueSize)
            return 0;
        queueAt(sQueueCount++) = *event;
        return 1;
    }

    int SDL_PollEvent(SDL_Event* event)
    {
        if (sQueueCount == 0)
            return 0;
        const SDL_Event front = sQueue[sQueueHead];
        sQueueHead = (sQueueHead + 1) % sQueueSize;
        trackMouse(front);
        if (event != nullptr)
            *event = front;
        return 1;
    }

    int SDL_PeepEvents(SDL_Event* events, int numevents, SDL_eventaction action, Uint32 minType, Uint32 maxType)
    {
        if (action == SDL_ADDEVENT)
        {
            int added = 0;
            for (; added < numevents; ++added)
                if (!SDL_PushEvent(&events[added]))
                    break;
            return added;
        }

        int found = 0;
        for (int i = 0; i < sQueueCount && found < numevents;)
        {
            const SDL_Event& event = queueAt(i);
            if (event.type < minType || event.type > maxType)
            {
                ++i;
                continue;
            }
            if (events != nullptr)
                events[found] = event;
            ++found;
            if (action == SDL_GETEVENT)
            {
                trackMouse(event);
                removeAt(i);
            }
            else
                ++i;
        }
        return found;
    }

    void SDL_FlushEvent(Uint32 type)
    {
        for (int i = 0; i < sQueueCount;)
        {
            if (queueAt(i).type == type)
                removeAt(i);
            else
                ++i;
        }
    }


    void SDL_GetWindowSize(SDL_Window*, int* w, int* h)
    {
        if (w != nullptr)
            *w = sWindowWidth;
        if (h != nullptr)
            *h = sWindowHeight;
    }

    void SDL_GL_GetDrawableSize(SDL_Window* window, int* w, int* h)
    {
        SDL_GetWindowSize(window, w, h);
    }

    void SDL_GetWindowPosition(SDL_Window*, int* x, int* y)
    {
        if (x != nullptr)
            *x = 0;
        if (y != nullptr)
            *y = 0;
    }

    Uint32 SDL_GetWindowFlags(SDL_Window*)
    {
        return SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS;
    }

    void SDL_SetWindowGrab(SDL_Window*, SDL_bool) {}

    int SDL_GetNumDisplayModes(int)
    {
        return 1;
    }

    int SDL_GetDisplayMode(int, int, SDL_DisplayMode* mode)
    {
        if (mode == nullptr)
            return -1;
        mode->format = SDL_PIXELFORMAT_RGB888;
        mode->w = sWindowWidth;
        mode->h = sWindowHeight;
        mode->refresh_rate = 60;
        mode->driverdata = nullptr;
        return 0;
    }

    SDL_DisplayOrientation SDL_GetDisplayOrientation(int)
    {
        return SDL_ORIENTATION_LANDSCAPE;
    }


    Uint32 SDL_GetMouseState(int* x, int* y)
    {
        if (x != nullptr)
            *x = sMouseX;
        if (y != nullptr)
            *y = sMouseY;
        return (hidKeysHeld() & KEY_TOUCH) ? SDL_BUTTON_LMASK : 0;
    }

    Uint32 SDL_GetRelativeMouseState(int* x, int* y)
    {
        if (x != nullptr)
            *x = 0;
        if (y != nullptr)
            *y = 0;
        return 0;
    }

    void SDL_WarpMouseInWindow(SDL_Window*, int x, int y)
    {
        sMouseX = x;
        sMouseY = y;
    }

    int SDL_ShowCursor(int)
    {
        return 0;
    }

    int SDL_SetRelativeMouseMode(SDL_bool)
    {
        return 0;
    }


    SDL_bool SDL_IsTextInputActive(void)
    {
        return sTextInput ? SDL_TRUE : SDL_FALSE;
    }

    void SDL_StartTextInput(void)
    {
        sTextInput = true;
    }

    void SDL_StopTextInput(void)
    {
        sTextInput = false;
    }

    const char* SDL_GetScancodeName(SDL_Scancode)
    {
        return "";
    }

    char* SDL_GetClipboardText(void)
    {
        char* text = static_cast<char*>(std::malloc(sClipboard.size() + 1));
        if (text != nullptr)
            std::memcpy(text, sClipboard.c_str(), sClipboard.size() + 1);
        return text;
    }

    int SDL_SetClipboardText(const char* text)
    {
        sClipboard = text != nullptr ? text : "";
        return 0;
    }


    int SDL_NumJoysticks(void)
    {
        return 1;
    }

    SDL_bool SDL_IsGameController(int joystickIndex)
    {
        return joystickIndex == 0 ? SDL_TRUE : SDL_FALSE;
    }

    const char* SDL_JoystickNameForIndex(int)
    {
        return "Nintendo 3DS";
    }

    const char* SDL_GameControllerNameForIndex(int)
    {
        return "Nintendo 3DS";
    }

    SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int)
    {
        return 0;
    }

    SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick*)
    {
        return 0;
    }

    SDL_GameController* SDL_GameControllerOpen(int joystickIndex)
    {
        return joystickIndex == 0 ? &sController : nullptr;
    }

    void SDL_GameControllerClose(SDL_GameController*) {}

    SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController*)
    {
        return &sJoystick;
    }

    SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController*)
    {
        return SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO;
    }

    Uint8 SDL_GameControllerGetButton(SDL_GameController*, SDL_GameControllerButton button)
    {
        const u32 key = controllerKey(button);
        return (key != 0 && (hidKeysHeld() & key)) ? 1 : 0;
    }

    SDL_bool SDL_GameControllerHasSensor(SDL_GameController*, SDL_SensorType)
    {
        return SDL_FALSE;
    }

    int SDL_GameControllerSetSensorEnabled(SDL_GameController*, SDL_SensorType, SDL_bool)
    {
        return -1;
    }

    int SDL_GameControllerGetSensorData(SDL_GameController*, SDL_SensorType, float*, int)
    {
        return -1;
    }

    SDL_RWops* SDL_RWFromFile(const char*, const char*)
    {
        return nullptr;
    }

    int SDL_GameControllerAddMappingsFromRW(SDL_RWops*, int)
    {
        return 0;
    }


    int SDL_NumSensors(void)
    {
        return 0;
    }

    SDL_Sensor* SDL_SensorOpen(int)
    {
        return nullptr;
    }

    void SDL_SensorClose(SDL_Sensor*) {}

    SDL_Sensor* SDL_SensorFromInstanceID(SDL_SensorID)
    {
        return nullptr;
    }

    const char* SDL_SensorGetDeviceName(int)
    {
        return "";
    }

    SDL_SensorType SDL_SensorGetDeviceType(int)
    {
        return SDL_SENSOR_INVALID;
    }

    SDL_SensorType SDL_SensorGetType(SDL_Sensor*)
    {
        return SDL_SENSOR_INVALID;
    }
}
