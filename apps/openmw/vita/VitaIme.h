#ifndef OPENMW_VITA_VITAIME_H
#define OPENMW_VITA_VITAIME_H

#if defined(__vita__) || defined(__3DS__)

#include <string>

namespace MyGUI
{
    class EditBox;
}

namespace Vita
{
    /// Load the SCE IME sysmodule. Call once at startup.
    void initIme();

    /// Open a modal Vita IME dialog for text input.
    /// Blocks until the user confirms or cancels.
    /// @param title     Dialog title (UTF-8).
    /// @param initial   Pre-filled text (UTF-8), may be empty.
    /// @param maxLen    Maximum character count (clamped to 2048).
    /// @return Entered text as UTF-8, or empty string if cancelled.
    std::string openTextDialog(const char* title, const char* initial = "", int maxLen = 128);

    /// Open the IME with the current text of `edit` and, on confirm,
    /// write the entered string back to `edit`. No-op if `edit` is null.
    /// Returns true if the user confirmed (with non-empty text) — caller
    /// can use this to decide whether to auto-submit the surrounding
    /// dialog. Returns false on cancel or empty submit.
    bool fillEditBoxFromIme(MyGUI::EditBox* edit, const char* title, int maxLen = 64);
}

#endif // __vita__
#endif // OPENMW_VITA_VITAIME_H
