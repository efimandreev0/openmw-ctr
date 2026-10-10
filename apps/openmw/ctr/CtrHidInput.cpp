// Native 3DS HID -> synthesized SDL events.
//
// The SDL n3ds joystick driver (through sdl2-compat) delivers
// CONTROLLERDEVICEADDED but no button/axis events in our dummy-video setup,
// so the whole input chain sits silent. Bypass it: poll HID directly every
// frame and push equivalent SDL events into the queue. The rest of OpenMW
// (bindings, controller GUI cursor, button overlay) works unchanged.
//
// The bottom touchscreen doubles as an absolute mouse pointer for MyGUI:
// touch coordinates (320x240) are scaled onto the 400x240 logical window,
// a touch begin/end becomes a left-button press/release.
#ifdef __3DS__

#include <3ds.h>

#include <cstdio>

#include <SDL_events.h>
#include <SDL_gamecontroller.h>
#include <SDL_joystick.h>
#include <SDL_mouse.h>

extern "C" void vitaBreadcrumb(const char*);

namespace
{
    void pushAxis(SDL_JoystickID jid, Uint8 axis, Sint16 value)
    {
        // Only push on change: 4 axes * 20-30fps of redundant events would
        // spam the queue and the input handlers.
        static Sint16 last[SDL_CONTROLLER_AXIS_MAX] = {};
        if (last[axis] == value)
            return;
        last[axis] = value;

        SDL_Event e{};
        e.type = SDL_CONTROLLERAXISMOTION;
        e.caxis.which = jid;
        e.caxis.axis = axis;
        e.caxis.value = value;
        const int rc = SDL_PushEvent(&e);
        // Chain diagnosis: a dozen LEFTY pushes with the queue verdict
        // (rc 1 = queued, 0 = filtered out, -1 = queue error).
        if (axis == SDL_CONTROLLER_AXIS_LEFTY && value != 0)
        {
            static int s_leftyLog = 0;
            if (++s_leftyLog <= 12)
            {
                char b[72];
                snprintf(b, sizeof(b), "[HID] push LEFTY=%d rc=%d", (int)value, rc);
                vitaBreadcrumb(b);
            }
        }
    }

    Sint16 scaleStick(int v)
    {
        // Circle pad reports roughly [-156, 156]; SDL wants [-32768, 32767].
        int scaled = v * 210;
        if (scaled > 32767)
            scaled = 32767;
        if (scaled < -32768)
            scaled = -32768;
        return static_cast<Sint16>(scaled);
    }
}

static bool ctrAutoWalkActive()
{
    // Dev hook: with sdmc:/3ds/openmw/auto_walk.flag present, feed forward
    // circle-pad pulses (2 s on / 4 s off) starting 90 s after boot. Proves
    // the axis->SDL->Lua->movement chain without touching the emulator.
    static int s_state = -1; // -1 unchecked, 0 off, 1 on
    static u64 s_t0 = 0;
    const u64 now = osGetTime();
    if (s_state == -1)
    {
        s_t0 = now;
        FILE* f = fopen("sdmc:/3ds/openmw/auto_walk.flag", "rb");
        if (f)
        {
            fclose(f);
            s_state = 1;
        }
        else
            s_state = 0;
    }
    if (s_state != 1 || now - s_t0 < 90000)
        return false;
    return ((now - s_t0) / 1000) % 6 < 2;
}

void ctrPumpNativeInput()
{
    static SDL_JoystickID jid = -2;
    if (jid == -2)
    {
        jid = SDL_NumJoysticks() > 0 ? SDL_JoystickGetDeviceInstanceID(0) : 0;
        vitaBreadcrumb("[HID] native input bridge armed");
    }

    hidScanInput();
    const u32 held = hidKeysHeld();
    static u32 s_prev = 0;
    const u32 changed = held ^ s_prev;
    s_prev = held;

    struct Map
    {
        u32 key;
        Uint8 button;
    };
    static const Map buttons[] = {
        { KEY_A, SDL_CONTROLLER_BUTTON_A },
        { KEY_B, SDL_CONTROLLER_BUTTON_B },
        { KEY_X, SDL_CONTROLLER_BUTTON_X },
        { KEY_Y, SDL_CONTROLLER_BUTTON_Y },
        { KEY_START, SDL_CONTROLLER_BUTTON_START },
        { KEY_SELECT, SDL_CONTROLLER_BUTTON_BACK },
        { KEY_DUP, SDL_CONTROLLER_BUTTON_DPAD_UP },
        { KEY_DDOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN },
        { KEY_DLEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT },
        { KEY_DRIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
        { KEY_L, SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
        { KEY_R, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
    };
    for (const Map& m : buttons)
    {
        if (changed & m.key)
        {
            SDL_Event e{};
            e.type = (held & m.key) ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
            e.cbutton.which = jid;
            e.cbutton.button = m.button;
            e.cbutton.state = (held & m.key) ? SDL_PRESSED : SDL_RELEASED;
            SDL_PushEvent(&e);
        }
    }

    // Dev hook: with sdmc:/3ds/openmw/auto_chargen.flag present, synthesize a
    // periodic A-button tap (down one frame, up the next) starting 100s after
    // boot. This drives the chargen intro's message boxes / dialogue confirms
    // (the intro is dialogue-driven, not walk-driven — the stick is correctly
    // gated off while a message box pauses the world). Proves whether in-game
    // message-box confirm input lands.
    {
        static int s_acState = -1; // -1 unchecked, 0 off, 1 on
        static u64 s_acT0 = 0;
        static int s_acPhase = 0;
        const u64 now = osGetTime();
        if (s_acState == -1)
        {
            s_acT0 = now;
            FILE* f = fopen("sdmc:/3ds/openmw/auto_chargen.flag", "rb");
            if (f) { fclose(f); s_acState = 1; } else s_acState = 0;
        }
        if (s_acState == 1 && now - s_acT0 > 100000)
        {
            // 3s cycle: frame at t%3==0 -> A down, t%3==1 (next tick) -> A up.
            const u64 sec = (now - s_acT0) / 1000;
            const bool wantDown = (sec % 3 == 0);
            static bool s_aHeld = false;
            if (wantDown != s_aHeld)
            {
                s_aHeld = wantDown;
                SDL_Event e{};
                e.type = wantDown ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
                e.cbutton.which = jid;
                e.cbutton.button = SDL_CONTROLLER_BUTTON_A;
                e.cbutton.state = wantDown ? SDL_PRESSED : SDL_RELEASED;
                SDL_PushEvent(&e);
                if (wantDown && (++s_acPhase % 4 == 1))
                    vitaBreadcrumb("[AutoChargen] A-tap");
            }
        }
    }

    // New3DS ZL/ZR: SDL models triggers as axes.
    if (changed & KEY_ZL)
        pushAxis(jid, SDL_CONTROLLER_AXIS_TRIGGERLEFT, (held & KEY_ZL) ? 32767 : 0);
    if (changed & KEY_ZR)
        pushAxis(jid, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, (held & KEY_ZR) ? 32767 : 0);

    // Circle pad -> left stick, C-stick (New3DS) -> right stick (camera).
    circlePosition cp;
    hidCircleRead(&cp);
    if (ctrAutoWalkActive())
    {
        cp.dy = 120; // forward push, ~75% deflection
        static int s_pulseLog = 0;
        if (++s_pulseLog % 150 == 1)
            vitaBreadcrumb("[AutoWalk] pulsing dy=120");
    }
    pushAxis(jid, SDL_CONTROLLER_AXIS_LEFTX, scaleStick(cp.dx));
    pushAxis(jid, SDL_CONTROLLER_AXIS_LEFTY, scaleStick(-cp.dy)); // SDL: +Y is down

    // C-stick lives behind the irrst service (New3DS); tolerate absence.
    static int s_irrst = -1; // -1 unknown, 0 unavailable, 1 ready
    if (s_irrst == -1)
        s_irrst = R_SUCCEEDED(irrstInit()) ? 1 : 0;
    if (s_irrst == 1)
    {
        irrstScanInput();
        circlePosition cs;
        irrstCstickRead(&cs);
        pushAxis(jid, SDL_CONTROLLER_AXIS_RIGHTX, scaleStick(cs.dx));
        pushAxis(jid, SDL_CONTROLLER_AXIS_RIGHTY, scaleStick(-cs.dy));
    }

    // Touchscreen -> absolute mouse.
    static bool s_touchWas = false;
    static int s_lastX = 0, s_lastY = 0;
    const bool touchNow = (held & KEY_TOUCH) != 0;
    if (touchNow)
    {
        touchPosition tp;
        hidTouchRead(&tp);
        const int mx = tp.px + (400 - 320) / 2;
        const int my = tp.py;

        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.which = 0;
        e.motion.state = SDL_BUTTON_LMASK;
        e.motion.x = mx;
        e.motion.y = my;
        e.motion.xrel = mx - s_lastX;
        e.motion.yrel = my - s_lastY;
        SDL_PushEvent(&e);
        s_lastX = mx;
        s_lastY = my;

        if (!s_touchWas)
        {
            SDL_Event d{};
            d.type = SDL_MOUSEBUTTONDOWN;
            d.button.which = 0;
            d.button.button = SDL_BUTTON_LEFT;
            d.button.state = SDL_PRESSED;
            d.button.clicks = 1;
            d.button.x = mx;
            d.button.y = my;
            SDL_PushEvent(&d);
        }
    }
    else if (s_touchWas)
    {
        SDL_Event u{};
        u.type = SDL_MOUSEBUTTONUP;
        u.button.which = 0;
        u.button.button = SDL_BUTTON_LEFT;
        u.button.state = SDL_RELEASED;
        u.button.clicks = 1;
        u.button.x = s_lastX;
        u.button.y = s_lastY;
        SDL_PushEvent(&u);
    }
    s_touchWas = touchNow;
}

#endif // __3DS__
