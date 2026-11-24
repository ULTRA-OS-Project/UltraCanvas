// OS/iOS/UltraCanvasiOSNativeDialogs.mm
// UltraCanvasNativeDialogs on iOS.
//
// Messages, questions and text input are UIAlertControllers. UIKit answers
// them through a callback, while this API returns the answer, so - as the
// Android backend does - the call presents the alert and pumps the main run
// loop until a button was tapped. Opening a file is the document picker
// (UIDocumentPickerViewController), the same way: the picked documents are
// copied into the app's sandbox (asCopy), so the caller gets plain paths it
// can read, as Android's SAF copies do. Saving through the picker's export
// mode, and choosing a folder, are still to come (iOSPortInvestigation.md
// phase 3); they return Cancel and say so. SaveContent is the shared
// definition in core/UltraCanvasFileLoader.cpp (SaveFile, then the bytes),
// so it fails the same way for now.
// Version: 1.1.0 - OpenFile and OpenMultipleFiles through the document picker
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasiOSWindow.h"
#include "UltraCanvasDebug.h"
#include "UltraLocalisation/UltraLocalisation.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <string>
#include <vector>

// The document picker's answer: the picked documents' paths, or none.
@interface UltraCanvasiOSPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic) BOOL done;
@property (nonatomic, strong) NSArray<NSURL*>* urls;
@end

@implementation UltraCanvasiOSPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller
        didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    self.urls = urls;
    self.done = YES;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    self.urls = @[];
    self.done = YES;
}
@end

namespace UltraCanvas {

    namespace {

        NSString* ToNS(const std::string& text) {
            NSString* value = [NSString stringWithUTF8String:text.c_str()];
            return value ? value : @"";
        }

        // The view controller an alert is presented from: the root one, or
        // whatever it is already presenting.
        UIViewController* PresentingController() {
            UIView* root = (__bridge UIView*)GetiOSRootView();
            UIViewController* controller = root.window.rootViewController;
            while (controller.presentedViewController) controller = controller.presentedViewController;
            return controller;
        }

        // Present `alert` and run the main run loop until `isDone` answers
        // true (one of its actions ran). A block rather than a bool&: the
        // actions' __block flag moves to the heap when they are copied.
        // Returns false when there is nothing to present it from.
        bool RunModal(UIAlertController* alert, bool (^isDone)(void)) {
            UIViewController* controller = PresentingController();
            if (!controller) {
                debugOutput << "UltraCanvas iOS: no view controller to present a dialog from"
                            << std::endl;
                return false;
            }
            [controller presentViewController:alert animated:YES completion:nil];
            while (!isDone()) {
                @autoreleasepool {
                    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
                }
            }
            return true;
        }

        struct ButtonSpec {
            std::string label;
            DialogResult result;
            UIAlertActionStyle style;
        };

        std::vector<ButtonSpec> ButtonsFor(DialogButtons buttons) {
            switch (buttons) {
                case DialogButtons::OKCancel:
                    return {{Tr("OK"), DialogResult::OK, UIAlertActionStyleDefault},
                            {Tr("Cancel"), DialogResult::Cancel, UIAlertActionStyleCancel}};
                case DialogButtons::YesNo:
                    return {{Tr("Yes"), DialogResult::Yes, UIAlertActionStyleDefault},
                            {Tr("No"), DialogResult::No, UIAlertActionStyleCancel}};
                case DialogButtons::YesNoCancel:
                    return {{Tr("Yes"), DialogResult::Yes, UIAlertActionStyleDefault},
                            {Tr("No"), DialogResult::No, UIAlertActionStyleDefault},
                            {Tr("Cancel"), DialogResult::Cancel, UIAlertActionStyleCancel}};
                case DialogButtons::RetryCancel:
                    return {{Tr("Retry"), DialogResult::Retry, UIAlertActionStyleDefault},
                            {Tr("Cancel"), DialogResult::Cancel, UIAlertActionStyleCancel}};
                case DialogButtons::AbortRetryIgnore:
                    return {{Tr("Abort"), DialogResult::Abort, UIAlertActionStyleDestructive},
                            {Tr("Retry"), DialogResult::Retry, UIAlertActionStyleDefault},
                            {Tr("Ignore"), DialogResult::Ignore, UIAlertActionStyleDefault}};
                case DialogButtons::NoButtons:
                case DialogButtons::OK:
                default:
                    return {{Tr("OK"), DialogResult::OK, UIAlertActionStyleDefault}};
            }
        }

        // What a dialog that could not be shown answers: the safe button -
        // Cancel or No where there is one, never Yes to a question.
        DialogResult SafeAnswer(DialogButtons buttons) {
            switch (buttons) {
                case DialogButtons::OKCancel:
                case DialogButtons::YesNoCancel:
                case DialogButtons::RetryCancel: return DialogResult::Cancel;
                case DialogButtons::YesNo:       return DialogResult::No;
                case DialogButtons::AbortRetryIgnore: return DialogResult::Abort;
                default:                         return DialogResult::OK;
            }
        }

        // The content types a filter list allows: each extension's type, or
        // any item when the list is empty or has an "all files" entry.
        NSArray<UTType*>* ContentTypesFor(const std::vector<FileFilter>& filters) {
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            for (const FileFilter& filter : filters) {
                for (std::string ext : filter.extensions) {
                    if (ext.rfind("*.", 0) == 0) ext = ext.substr(2);
                    else if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                    if (ext.empty() || ext == "*") return @[ UTTypeItem ];
                    UTType* type = [UTType typeWithFilenameExtension:ToNS(ext)];
                    if (type && ![types containsObject:type]) [types addObject:type];
                }
            }
            if (types.count == 0) return @[ UTTypeItem ];
            return types;
        }

        // Show the document picker and wait for its answer: the copied
        // documents' paths (UTF-8), none when cancelled.
        std::vector<std::string> PickDocuments(const std::vector<FileFilter>& filters, bool multiple) {
            std::vector<std::string> paths;
            @autoreleasepool {
                UIViewController* presenter = PresentingController();
                if (!presenter) {
                    debugOutput << "UltraCanvas iOS: no view controller to present the "
                                   "document picker from" << std::endl;
                    return paths;
                }
                UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
                        initForOpeningContentTypes:ContentTypesFor(filters) asCopy:YES];
                picker.allowsMultipleSelection = multiple;
                UltraCanvasiOSPickerDelegate* delegate = [[UltraCanvasiOSPickerDelegate alloc] init];
                picker.delegate = delegate;   // weak: `delegate` lives to the end of this scope
                [presenter presentViewController:picker animated:YES completion:nil];
                while (!delegate.done) {
                    @autoreleasepool {
                        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
                    }
                }
                for (NSURL* url in delegate.urls) {
                    const char* path = url.isFileURL ? url.path.fileSystemRepresentation : nullptr;
                    if (path && *path) paths.emplace_back(path);
                }
            }
            return paths;
        }

        std::string NotYet(const char* what) {
            debugOutput << "UltraCanvas iOS: native dialog '" << what
                        << "' is not available yet (the document picker is phase 3 of "
                           "iOSPortInvestigation.md) - returning Cancel" << std::endl;
            return {};
        }

    } // namespace

// ===== MESSAGE DIALOGS =====

    DialogResult UltraCanvasNativeDialogs::ShowMessage(
            const std::string& message, const std::string& title,
            DialogType /*type*/, DialogButtons buttons, UltraCanvasWindowBase* /*parent*/) {
        @autoreleasepool {
            UIAlertController* alert = [UIAlertController
                    alertControllerWithTitle:(title.empty() ? nil : ToNS(title))
                                     message:ToNS(message)
                              preferredStyle:UIAlertControllerStyleAlert];
            __block bool done = false;
            __block DialogResult answer = SafeAnswer(buttons);
            for (const ButtonSpec& spec : ButtonsFor(buttons)) {
                const DialogResult result = spec.result;
                [alert addAction:[UIAlertAction actionWithTitle:ToNS(spec.label)
                                                          style:spec.style
                                                        handler:^(UIAlertAction*) {
                    answer = result;
                    done = true;
                }]];
            }
            if (!RunModal(alert, ^bool { return done; })) return SafeAnswer(buttons);
            return answer;
        }
    }

    DialogResult UltraCanvasNativeDialogs::ShowInfo(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Information, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowWarning(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Warning, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowError(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Error, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowQuestion(
            const std::string& message, const std::string& title,
            DialogButtons buttons, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Question, buttons, parent);
    }

// ===== CONFIRMATION DIALOGS =====

    bool UltraCanvasNativeDialogs::Confirm(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Question,
                           DialogButtons::OKCancel, parent) == DialogResult::OK;
    }

    bool UltraCanvasNativeDialogs::ConfirmYesNo(
            const std::string& message, const std::string& title, UltraCanvasWindowBase* parent) {
        return ShowMessage(message, title, DialogType::Question,
                           DialogButtons::YesNo, parent) == DialogResult::Yes;
    }

// ===== FILE DIALOGS =====

    std::string UltraCanvasNativeDialogs::OpenFile(
            const std::string&, const std::vector<FileFilter>& filters,
            const std::string&, UltraCanvasWindowBase*) {
        const std::vector<std::string> paths = PickDocuments(filters, false);
        return paths.empty() ? std::string() : paths.front();
    }

    std::string UltraCanvasNativeDialogs::OpenFile(const FileDialogOptions& options) {
        const std::vector<std::string> paths = PickDocuments(options.filters, false);
        return paths.empty() ? std::string() : paths.front();
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const std::string&, const std::vector<FileFilter>& filters,
            const std::string&, UltraCanvasWindowBase*) {
        return PickDocuments(filters, true);
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(const FileDialogOptions& options) {
        return PickDocuments(options.filters, true);
    }

    std::string UltraCanvasNativeDialogs::SaveFile(
            const std::string&, const std::vector<FileFilter>&,
            const std::string&, const std::string&, UltraCanvasWindowBase*) {
        return NotYet("SaveFile");
    }

    std::string UltraCanvasNativeDialogs::SaveFile(const FileDialogOptions&) {
        return NotYet("SaveFile");
    }

    std::string UltraCanvasNativeDialogs::SelectFolder(
            const std::string&, const std::string&, UltraCanvasWindowBase*) {
        return NotYet("SelectFolder");
    }

// ===== INPUT DIALOGS =====

    NativeInputResult UltraCanvasNativeDialogs::InputText(const NativeInputDialogOptions& options) {
        NativeInputResult r;
        @autoreleasepool {
            UIAlertController* alert = [UIAlertController
                    alertControllerWithTitle:(options.title.empty() ? nil : ToNS(options.title))
                                     message:(options.prompt.empty() ? nil : ToNS(options.prompt))
                              preferredStyle:UIAlertControllerStyleAlert];
            const bool password = options.password;
            NSString* initial = ToNS(options.defaultValue);
            [alert addTextFieldWithConfigurationHandler:^(UITextField* field) {
                field.text = initial;
                field.secureTextEntry = password;
            }];
            __block bool done = false;
            __block bool accepted = false;
            __block std::string value;
            __weak UIAlertController* weakAlert = alert;
            [alert addAction:[UIAlertAction actionWithTitle:ToNS(Tr("OK"))
                                                      style:UIAlertActionStyleDefault
                                                    handler:^(UIAlertAction*) {
                NSString* text = weakAlert.textFields.firstObject.text;
                const char* utf8 = text.UTF8String;
                value = utf8 ? utf8 : "";
                accepted = true;
                done = true;
            }]];
            [alert addAction:[UIAlertAction actionWithTitle:ToNS(Tr("Cancel"))
                                                      style:UIAlertActionStyleCancel
                                                    handler:^(UIAlertAction*) {
                done = true;
            }]];
            if (RunModal(alert, ^bool { return done; }) && accepted) {
                r.result = DialogResult::OK;
                r.value = value;
            }
        }
        return r;
    }

    NativeInputResult UltraCanvasNativeDialogs::InputText(
            const std::string& prompt, const std::string& title,
            const std::string& defaultValue, UltraCanvasWindowBase* parent) {
        NativeInputDialogOptions options;
        options.title = title;
        options.prompt = prompt;
        options.defaultValue = defaultValue;
        options.parentWindow = parent;
        return InputText(options);
    }

    NativeInputResult UltraCanvasNativeDialogs::InputPassword(
            const std::string& prompt, const std::string& title, UltraCanvasWindowBase* parent) {
        NativeInputDialogOptions options;
        options.title = title;
        options.prompt = prompt;
        options.password = true;
        options.parentWindow = parent;
        return InputText(options);
    }

// ===== CONVENIENCE FUNCTIONS =====

    std::string UltraCanvasNativeDialogs::GetInput(
            const std::string& prompt, const std::string& title,
            const std::string& defaultValue, UltraCanvasWindowBase* parent) {
        NativeInputResult r = InputText(prompt, title, defaultValue, parent);
        return r.IsOK() ? r.value : std::string();
    }

    std::string UltraCanvasNativeDialogs::GetPassword(
            const std::string& prompt, const std::string& title, UltraCanvasWindowBase* parent) {
        NativeInputResult r = InputPassword(prompt, title, parent);
        return r.IsOK() ? r.value : std::string();
    }

// ===== PRINTING =====
// UIPrintInteractionController is a later phase; there is no IODeviceManager
// printer backend on iOS to send a job to either.

    NativePrintResult UltraCanvasNativeDialogs::RequestPrintSettings(
            const std::string&, UltraCanvasWindowBase*) {
        return NativePrintResult();
    }

    bool UltraCanvasNativeDialogs::ShowPrintDialog(
            const std::string&, const std::string&, UltraCanvasWindowBase*) {
        debugOutput << "UltraCanvas iOS: printing is not available yet" << std::endl;
        return false;
    }

} // namespace UltraCanvas
