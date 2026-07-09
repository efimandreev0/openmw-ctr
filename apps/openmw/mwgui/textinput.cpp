#include "textinput.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_UString.h>

#include <components/esm/refid.hpp>

#if defined(__vita__) || defined(__3DS__)
#include "../vita/VitaIme.h"
#include "../vita/VitaInit.h"
#endif

namespace MWGui
{

    TextInputDialog::TextInputDialog()
        : WindowModal("openmw_text_input.layout")
    {
        // Centre dialog
        center();

        getWidget(mTextEdit, "TextEdit");
        mTextEdit->eventEditSelectAccept += newDelegate(this, &TextInputDialog::onTextAccepted);

        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");
        okButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TextInputDialog::onOkClicked);

        // Make sure the edit box has focus
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);

#if defined(__vita__) || defined(__3DS__)
        mDisableGamepadCursor = true;
#endif
        mControllerButtons.mA = "#{Interface:OK}";
    }

    void TextInputDialog::setNextButtonShow(bool shown)
    {
        MyGUI::Button* okButton;
        getWidget(okButton, "OKButton");

        if (shown)
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sNext", {})));
        else
            okButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sOK", {})));
    }

    void TextInputDialog::setTextLabel(std::string_view label)
    {
        setText("LabelT", label);
    }

    void TextInputDialog::onOpen()
    {
        WindowModal::onOpen();
        // Make sure the edit box has focus
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);
#if defined(__vita__) || defined(__3DS__)
        // Auto-open the IME so the user doesn't have to press A first.
        // On confirm we auto-submit the dialog (same flow as the A-button
        // handler below); on cancel we leave it visible so they can
        // press A to retry or B to back out.
        if (Vita::fillEditBoxFromIme(mTextEdit, "Enter Name", 64))
        {
            eventDone(this);
        }
#endif
    }

    // widget controls

    void TextInputDialog::onOkClicked(MyGUI::Widget* /*sender*/)
    {
#if defined(__vita__) || defined(__3DS__)
        // On Vita, if text is empty, open the IME keyboard instead of showing an error
        if (mTextEdit->getCaption().empty())
        {
            Vita::breadcrumb("[TextInput] onOkClicked: text empty, opening IME");
            std::string result = Vita::openTextDialog("Enter Name", "", 64);
            if (!result.empty())
            {
                mTextEdit->setCaption(result);
                eventDone(this);
            }
            return;
        }
#endif
        if (mTextEdit->getCaption().empty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage37}");
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);
        }
        else
            eventDone(this);
    }

    void TextInputDialog::onTextAccepted(MyGUI::EditBox* sender)
    {
        onOkClicked(sender);

        // To do not spam onTextAccepted() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    std::string TextInputDialog::getTextInput() const
    {
        return mTextEdit->getCaption();
    }

    void TextInputDialog::setTextInput(const std::string& text)
    {
        mTextEdit->setCaption(text);
    }

    bool TextInputDialog::onControllerButtonEvent(const SDL_ControllerButtonEvent& arg)
    {
#if defined(__vita__) || defined(__3DS__)
        Vita::breadcrumb("[TextInput] onControllerButtonEvent called");
#endif
        if (arg.button == SDL_CONTROLLER_BUTTON_A)
        {
#if defined(__vita__) || defined(__3DS__)
            Vita::breadcrumb("[TextInput] A button pressed, opening IME");
            std::string current = mTextEdit->getCaption().asUTF8();
            std::string result = Vita::openTextDialog("Enter Name", current.c_str(), 64);
            if (!result.empty())
            {
                mTextEdit->setCaption(result);
                eventDone(this);
            }
#else
            onOkClicked(nullptr);
#endif
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
            return true;
        }

        return false;
    }
}
