#ifdef __3DS__

// 3DS implementation of the IME interface declared in vita/VitaIme.h,
// backed by the system software keyboard applet (swkbd).

#include "../vita/VitaIme.h"

#include <3ds.h>

#include <MyGUI_EditBox.h>
#include <MyGUI_UString.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace Vita
{
    void initIme()
    {
        // swkbd is an applet — nothing to preload.
    }

    std::string openTextDialog(const char* title, const char* initial, int maxLen)
    {
        maxLen = std::clamp(maxLen, 1, 2048);

        SwkbdState swkbd;
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, maxLen);
        swkbdSetHintText(&swkbd, title ? title : "");
        if (initial && initial[0])
            swkbdSetInitialText(&swkbd, initial);
        swkbdSetFeatures(&swkbd, SWKBD_DEFAULT_QWERTY);
        swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
        swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);

        std::vector<char> buf(static_cast<size_t>(maxLen) * 4 + 1, 0);
        SwkbdButton pressed = swkbdInputText(&swkbd, buf.data(), buf.size());
        if (pressed != SWKBD_BUTTON_RIGHT)
            return {};
        return std::string(buf.data());
    }

    bool fillEditBoxFromIme(MyGUI::EditBox* edit, const char* title, int maxLen)
    {
        if (!edit)
            return false;

        const std::string initial = edit->getCaption().asUTF8();
        std::string result = openTextDialog(title, initial.c_str(), maxLen);
        if (result.empty())
            return false;

        edit->setCaption(MyGUI::UString(result));
        return true;
    }
}

#endif // __3DS__
