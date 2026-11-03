// Apps/ChatCube/ui/ChatCubeProfileWindow.h
// The Edit profile window: the ChatCube account's name, e-mail address and
// phone number, and how signing in on this computer is protected.
//
//   Profile
//     Name           [ Erin                         ]
//     E-mail         [ erin@example.org             ]
//     Phone number   [ +49 151 2345 6789            ]
//     Kept for you alone, to recover your account ...       [Save profile]
//
//   Signing in on this computer
//     [ A password and a code from UltraAuthenticator  v ]
//     what that protects against, in one sentence
//     Current password [ ••••••• ]      (when there is one now)
//     Current code     [ ______ ]       (when a code is asked for now)
//     New password     [ ••••••• ]  Repeat [ ••••••• ]
//     [QR]  In UltraAuthenticator: Add account - scan this code
//           (or type the key: JBSW Y3DP ...), then type the code it shows
//           [ ______ ]
//                                                [Save sign-in protection]
//     status line
//
// The name goes to the contacts; the e-mail and the phone number into the
// identity's key vault and to the relay, for the owner alone (UltraIM's
// account profile). The protection is UltraIM's sign-in protection: the
// identity's vault behind a password, and/or the code of an authenticator
// app - UltraAuthenticator scans the QR code from this screen, any other
// authenticator from a phone. Changing it asks for what protects it now.
//
// Version: 0.1.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraIM/UltraIMTypes.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
class UltraCanvasButton;
class UltraCanvasContainer;
class UltraCanvasDropdown;
class UltraCanvasLabel;
class UltraCanvasQRCode;
class UltraCanvasTextInput;
}

namespace ChatCube {

class ChatCubeProfileWindow {
public:
    explicit ChatCubeProfileWindow(std::string providerId);
    ~ChatCubeProfileWindow();

    // Opens the window over `host` (or brings it forward) and fills it from
    // the provider. With `preset`, the protection list starts on it - the
    // choice made when the ID was created, finished here (an authenticator
    // to scan) - and `currentPassword` fills the current password's field.
    void Show(UltraCanvas::UltraCanvasWindowBase* host);
    void Show(UltraCanvas::UltraCanvasWindowBase* host, UltraIMSignInProtection preset,
              const std::string& currentPassword);
    void Close();

    // After a save: what was saved, for the main window's status line and header.
    std::function<void(const std::string& text)> onSaved;
    std::function<void(const UltraIMAccountProfile& profile)> onProfileChanged;

private:
    void Build(UltraCanvas::UltraCanvasWindowBase* host);
    void Load();
    void SaveProfile();
    // The fields the chosen protection needs, and the current one's proofs.
    void UpdateProtectionForm();
    // A new authenticator secret, shown as a QR code (and its key).
    void StartAuthenticator();
    void SaveProtection();
    void SetProfileStatus(const std::string& text, bool error = false);
    void SetProtectionStatus(const std::string& text, bool error = false);
    UltraIMSignInProtection Chosen() const;

    std::string providerId_;
    UltraIMSignInProtection current_ = UltraIMSignInProtection::DeviceKey;
    bool authenticatorShown_ = false;       // a set-up waits for its first code
    bool closed_ = true;

    std::shared_ptr<UltraCanvas::UltraCanvasWindow>    window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> name_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> email_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> phone_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    saveProfile_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     profileStatus_;

    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  protection_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     protectionText_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> currentPasswordRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> currentPassword_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> currentCodeRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> currentCode_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> newPasswordRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> newPassword_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> repeatPassword_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    newAuthenticator_;   // replace the one there is
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> authenticatorBox_;
    std::shared_ptr<UltraCanvas::UltraCanvasQRCode>    qr_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     authenticatorKey_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> newCode_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    saveProtection_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     protectionStatus_;
};

} // namespace ChatCube
