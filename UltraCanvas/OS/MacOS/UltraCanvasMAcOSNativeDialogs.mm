// OS/MacOS/UltraCanvasNativeDialogsMacOS.mm
// macOS implementation of native OS dialogs using Cocoa/AppKit
// Uses unified DialogType, DialogButtons, DialogResult from UltraCanvasModalDialog.h
// Version: 2.0.1 - bridge-cast void* NativeWindowHandle back to NSWindow*
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"
#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"

#if defined(__APPLE__) && defined(__MACH__)
#include <TargetConditionals.h>
#if TARGET_OS_MAC && !TARGET_OS_IPHONE

#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>
#include <algorithm>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// ===== STRING CONVERSION HELPERS =====

NSString* ToNSString(const std::string& str) {
    return [NSString stringWithUTF8String:str.c_str()];
}

std::string FromNSString(NSString* nsStr) {
    if (!nsStr) return "";
    return std::string([nsStr UTF8String]);
}

// Convert DialogType to NSAlertStyle
NSAlertStyle ToNSAlertStyle(DialogType type) {
    switch (type) {
        case DialogType::Information: return NSAlertStyleInformational;
        case DialogType::Successful:     return NSAlertStyleInformational;
        case DialogType::Warning:     return NSAlertStyleWarning;
        case DialogType::Error:       return NSAlertStyleCritical;
        case DialogType::Question:    return NSAlertStyleInformational;
        case DialogType::Custom:
        default:                      return NSAlertStyleInformational;
    }
}

// Convert modal response to DialogResult
DialogResult FromNSModalResponse(NSModalResponse response, DialogButtons buttons) {
    // NSAlert button indices: first added button = 1000, second = 1001, etc.
    switch (buttons) {
        case DialogButtons::OK:
            return (response == NSAlertFirstButtonReturn) ?
                   DialogResult::OK : DialogResult::Cancel;

        case DialogButtons::OKCancel:
            if (response == NSAlertFirstButtonReturn) return DialogResult::OK;
            return DialogResult::Cancel;

        case DialogButtons::YesNo:
            if (response == NSAlertFirstButtonReturn) return DialogResult::Yes;
            return DialogResult::No;

        case DialogButtons::YesNoCancel:
            if (response == NSAlertFirstButtonReturn) return DialogResult::Yes;
            if (response == NSAlertSecondButtonReturn) return DialogResult::No;
            return DialogResult::Cancel;

        case DialogButtons::RetryCancel:
            if (response == NSAlertFirstButtonReturn) return DialogResult::Retry;
            return DialogResult::Cancel;

        case DialogButtons::AbortRetryIgnore:
            if (response == NSAlertFirstButtonReturn) return DialogResult::Abort;
            if (response == NSAlertSecondButtonReturn) return DialogResult::Retry;
            return DialogResult::Ignore;

        default:
            return DialogResult::Cancel;
    }
}

// Parse FileFilter extensions into array of extensions
NSArray<NSString*>* ParseExtensions(const FileFilter& filter) {
    NSMutableArray<NSString*>* result = [NSMutableArray array];

    for (const auto& ext : filter.extensions) {
        if (ext != "*" && !ext.empty()) {
            [result addObject:ToNSString(ext)];
        }
    }

    return result;
}

} // anonymous namespace

// ===== MESSAGE DIALOGS =====

DialogResult UltraCanvasNativeDialogs::ShowInfo(
    const std::string& message,
    const std::string& title,
    UltraCanvasWindowBase*  parent) {
    return ShowMessage(message, title, DialogType::Information, DialogButtons::OK, parent);
}

DialogResult UltraCanvasNativeDialogs::ShowWarning(
    const std::string& message,
    const std::string& title,
    UltraCanvasWindowBase*  parent) {
    return ShowMessage(message, title, DialogType::Warning, DialogButtons::OK, parent);
}

DialogResult UltraCanvasNativeDialogs::ShowError(
    const std::string& message,
    const std::string& title,
    UltraCanvasWindowBase*  parent) {
    return ShowMessage(message, title, DialogType::Error, DialogButtons::OK, parent);
}

DialogResult UltraCanvasNativeDialogs::ShowQuestion(
    const std::string& message,
    const std::string& title,
    DialogButtons buttons,
    UltraCanvasWindowBase*  parent) {
    return ShowMessage(message, title, DialogType::Question, buttons, parent);
}

DialogResult UltraCanvasNativeDialogs::ShowMessage(
    const std::string& message,
    const std::string& title,
    DialogType type,
    DialogButtons buttons,
    UltraCanvasWindowBase*  parent) {

    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];

        [alert setMessageText:ToNSString(title)];
        [alert setInformativeText:ToNSString(message)];
        [alert setAlertStyle:ToNSAlertStyle(type)];

        // Add buttons based on type
        switch (buttons) {
            case DialogButtons::OK:
                [alert addButtonWithTitle:@"OK"];
                break;

            case DialogButtons::OKCancel:
                [alert addButtonWithTitle:@"OK"];
                [alert addButtonWithTitle:@"Cancel"];
                break;

            case DialogButtons::YesNo:
                [alert addButtonWithTitle:@"Yes"];
                [alert addButtonWithTitle:@"No"];
                break;

            case DialogButtons::YesNoCancel:
                [alert addButtonWithTitle:@"Yes"];
                [alert addButtonWithTitle:@"No"];
                [alert addButtonWithTitle:@"Cancel"];
                break;

            case DialogButtons::RetryCancel:
                [alert addButtonWithTitle:@"Retry"];
                [alert addButtonWithTitle:@"Cancel"];
                break;

            case DialogButtons::AbortRetryIgnore:
                [alert addButtonWithTitle:@"Abort"];
                [alert addButtonWithTitle:@"Retry"];
                [alert addButtonWithTitle:@"Ignore"];
                break;
        }

        NSModalResponse response;

        // If parent window is provided, run as sheet (attached modal)
        // Otherwise run as application-modal dialog
        if (parent != nullptr) {
            NSWindow* parentWindow = (__bridge NSWindow*)parent->GetNativeHandle();

            // For sheet-style, we need to use beginSheetModalForWindow
            // But since we need a synchronous result, we use runModal with window level
            [[alert window] setLevel:NSModalPanelWindowLevel];

            // Make parent window the parent of the alert
            // This doesn't attach it as a sheet but ensures proper window ordering
            response = [alert runModal];
        } else {
            response = [alert runModal];
        }

        return FromNSModalResponse(response, buttons);
    }
}

// ===== CONFIRMATION DIALOGS =====

bool UltraCanvasNativeDialogs::Confirm(
    const std::string& message,
    const std::string& title,
    UltraCanvasWindowBase*  parent) {
    DialogResult result = ShowMessage(message, title,
        DialogType::Question, DialogButtons::OKCancel, parent);
    return result == DialogResult::OK;
}

bool UltraCanvasNativeDialogs::ConfirmYesNo(
    const std::string& message,
    const std::string& title,
    UltraCanvasWindowBase*  parent) {
    DialogResult result = ShowMessage(message, title,
        DialogType::Question, DialogButtons::YesNo, parent);
    return result == DialogResult::Yes;
}

// ===== FILE DIALOGS =====

std::string UltraCanvasNativeDialogs::OpenFile(
    const std::string& title,
    const std::vector<FileFilter>& filters,
    const std::string& initialDir,
    UltraCanvasWindowBase*  parent) {

    FileDialogOptions options;
    options.title = title;
    options.filters = filters;
    options.initialDirectory = initialDir;
    options.parentWindow = parent;
    return OpenFile(options);
}

std::string UltraCanvasNativeDialogs::OpenFile(const FileDialogOptions& options) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];

        // Configure panel
        [panel setTitle:ToNSString(options.title.empty() ? "Open File" : options.title)];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setShowsHiddenFiles:options.showHiddenFiles ? YES : NO];

        // Set panel level to stay on top
        [panel setLevel:NSModalPanelWindowLevel];

        // Set initial directory
        if (!options.initialDirectory.empty()) {
            NSURL* dirURL = [NSURL fileURLWithPath:ToNSString(options.initialDirectory) isDirectory:YES];
            [panel setDirectoryURL:dirURL];
        }

        // Set file type filters
        if (!options.filters.empty()) {
            NSMutableArray<NSString*>* allowedTypes = [NSMutableArray array];

            for (const auto& filter : options.filters) {
                NSArray<NSString*>* extensions = ParseExtensions(filter);
                [allowedTypes addObjectsFromArray:extensions];
            }

            if ([allowedTypes count] > 0) {
                [panel setAllowedFileTypes:allowedTypes];
            }
        }

        // Show panel
        NSModalResponse response;
        if (options.parentWindow != nullptr) {
            // Run as sheet attached to parent window would be async
            // For synchronous behavior, use runModal with proper level
            response = [panel runModal];
        } else {
            response = [panel runModal];
        }

        if (response == NSModalResponseOK) {
            NSURL* url = [panel URL];
            if (url) {
                return FromNSString([url path]);
            }
        }

        return "";
    }
}

std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
    const std::string& title,
    const std::vector<FileFilter>& filters,
    const std::string& initialDir,
    UltraCanvasWindowBase*  parent) {

    FileDialogOptions options;
    options.title = title;
    options.filters = filters;
    options.initialDirectory = initialDir;
    options.parentWindow = parent;
    return OpenMultipleFiles(options);
}

std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(const FileDialogOptions& options) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];

        // Configure panel
        [panel setTitle:ToNSString(options.title.empty() ? "Open Files" : options.title)];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:YES];
        [panel setShowsHiddenFiles:options.showHiddenFiles ? YES : NO];

        // Set panel level to stay on top
        [panel setLevel:NSModalPanelWindowLevel];

        // Set initial directory
        if (!options.initialDirectory.empty()) {
            NSURL* dirURL = [NSURL fileURLWithPath:ToNSString(options.initialDirectory) isDirectory:YES];
            [panel setDirectoryURL:dirURL];
        }

        // Set file type filters
        if (!options.filters.empty()) {
            NSMutableArray<NSString*>* allowedTypes = [NSMutableArray array];

            for (const auto& filter : options.filters) {
                NSArray<NSString*>* extensions = ParseExtensions(filter);
                [allowedTypes addObjectsFromArray:extensions];
            }

            if ([allowedTypes count] > 0) {
                [panel setAllowedFileTypes:allowedTypes];
            }
        }

        // Show panel
        NSModalResponse response = [panel runModal];

        std::vector<std::string> results;

        if (response == NSModalResponseOK) {
            NSArray<NSURL*>* urls = [panel URLs];
            for (NSURL* url in urls) {
                results.push_back(FromNSString([url path]));
            }
        }

        return results;
    }
}

std::string UltraCanvasNativeDialogs::SaveFile(
    const std::string& title,
    const std::vector<FileFilter>& filters,
    const std::string& initialDir,
    const std::string& defaultFileName,
    UltraCanvasWindowBase*  parent) {

    FileDialogOptions options;
    options.title = title;
    options.filters = filters;
    options.initialDirectory = initialDir;
    options.defaultFileName = defaultFileName;
    options.parentWindow = parent;
    return SaveFile(options);
}

std::string UltraCanvasNativeDialogs::SaveFile(const FileDialogOptions& options) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];

        // Configure panel
        [panel setTitle:ToNSString(options.title.empty() ? "Save File" : options.title)];
        [panel setShowsHiddenFiles:options.showHiddenFiles ? YES : NO];
        [panel setCanCreateDirectories:YES];

        // Set panel level to stay on top
        [panel setLevel:NSModalPanelWindowLevel];

        // Set initial directory
        if (!options.initialDirectory.empty()) {
            NSURL* dirURL = [NSURL fileURLWithPath:ToNSString(options.initialDirectory) isDirectory:YES];
            [panel setDirectoryURL:dirURL];
        }

        // Set default filename
        if (!options.defaultFileName.empty()) {
            [panel setNameFieldStringValue:ToNSString(options.defaultFileName)];
        }

        // Set file type filters
        if (!options.filters.empty()) {
            NSMutableArray<NSString*>* allowedTypes = [NSMutableArray array];

            for (const auto& filter : options.filters) {
                NSArray<NSString*>* extensions = ParseExtensions(filter);
                [allowedTypes addObjectsFromArray:extensions];
            }

            if ([allowedTypes count] > 0) {
                [panel setAllowedFileTypes:allowedTypes];
            }
        }

        // Show panel
        NSModalResponse response = [panel runModal];

        if (response == NSModalResponseOK) {
            NSURL* url = [panel URL];
            if (url) {
                return FromNSString([url path]);
            }
        }

        return "";
    }
}

std::string UltraCanvasNativeDialogs::SelectFolder(
    const std::string& title,
    const std::string& initialDir,
    UltraCanvasWindowBase*  parent) {

    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];

        // Configure for folder selection
        [panel setTitle:ToNSString(title.empty() ? "Select Folder" : title)];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanCreateDirectories:YES];

        // Set panel level to stay on top
        [panel setLevel:NSModalPanelWindowLevel];

        // Set initial directory
        if (!initialDir.empty()) {
            NSURL* dirURL = [NSURL fileURLWithPath:ToNSString(initialDir) isDirectory:YES];
            [panel setDirectoryURL:dirURL];
        }

        // Show panel
        NSModalResponse response = [panel runModal];

        if (response == NSModalResponseOK) {
            NSURL* url = [[panel URLs] firstObject];
            if (url) {
                return FromNSString([url path]);
            }
        }

        return "";
    }
}

// ===== INPUT DIALOGS =====

NativeInputResult UltraCanvasNativeDialogs::InputText(
    const std::string& prompt,
    const std::string& title,
    const std::string& defaultValue,
    UltraCanvasWindowBase*  parent) {

    NativeInputDialogOptions options;
    options.prompt = prompt;
    options.title = title;
    options.defaultValue = defaultValue;
    options.parentWindow = parent;
    return InputText(options);
}

NativeInputResult UltraCanvasNativeDialogs::InputText(const NativeInputDialogOptions& options) {
    @autoreleasepool {
        NativeInputResult result;
        result.result = DialogResult::Cancel;

        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:ToNSString(options.title)];
        [alert setInformativeText:ToNSString(options.prompt)];
        [alert setAlertStyle:NSAlertStyleInformational];
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];

        // Set window level to stay on top
        [[alert window] setLevel:NSModalPanelWindowLevel];

        // Create text field
        NSTextField* input;
        if (options.password) {
            input = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
        } else {
            input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
        }

        [input setStringValue:ToNSString(options.defaultValue)];
        [alert setAccessoryView:input];

        // Make text field first responder when alert appears
        [[alert window] setInitialFirstResponder:input];

        NSModalResponse response = [alert runModal];

        if (response == NSAlertFirstButtonReturn) {
            result.result = DialogResult::OK;
            result.value = FromNSString([input stringValue]);
        }

        return result;
    }
}

NativeInputResult UltraCanvasNativeDialogs::InputPassword(
    const std::string& prompt,
    const std::string& title,
    UltraCanvasWindowBase*  parent) {

    NativeInputDialogOptions options;
    options.prompt = prompt;
    options.title = title;
    options.password = true;
    options.parentWindow = parent;
    return InputText(options);
}

// ===== CONVENIENCE FUNCTIONS =====

std::string UltraCanvasNativeDialogs::GetInput(
    const std::string& prompt,
    const std::string& title,
    const std::string& defaultValue,
    UltraCanvasWindowBase*  parent) {

    NativeInputResult result = InputText(prompt, title, defaultValue, parent);
    return result.IsOK() ? result.value : "";
}

std::string UltraCanvasNativeDialogs::GetPassword(
    const std::string& prompt,
    const std::string& title,
    UltraCanvasWindowBase*  parent) {

    NativeInputResult result = InputPassword(prompt, title, parent);
    return result.IsOK() ? result.value : "";
}

namespace {

// ===== NSPrintInfo → IOPrintOptions =====

// AppKit measures paper in points, 1/72 inch; the printer vocabulary uses
// hundredths of a millimetre. 25.4/72 mm per point, so 2540/72 hundredths.
int PointsToHundredthsMM(CGFloat points) {
    return static_cast<int>((points * 2540.0) / 72.0 + 0.5);
}

int IntFromPrintInfo(NSPrintInfo* printInfo, NSString* key, int fallback) {
    NSNumber* value = [[printInfo dictionary] objectForKey:key];
    return value ? [value intValue] : fallback;
}

bool BoolFromPrintInfo(NSPrintInfo* printInfo, NSString* key, bool fallback) {
    NSNumber* value = [[printInfo dictionary] objectForKey:key];
    return value ? [value boolValue] : fallback;
}

void ReadPrintInfo(NSPrintInfo* printInfo, NativePrintResult& chosen) {
    if (!printInfo) return;

    if (NSPrinter* printer = [printInfo printer]) {
        // The NSPrinter name is the CUPS destination name on macOS, which is
        // what the CUPS backend puts in IODeviceInfo::connectionPath.
        chosen.printerName = FromNSString([printer name]);
    }

    chosen.options.copies = std::max(1, IntFromPrintInfo(printInfo, NSPrintCopies, 1));
    chosen.options.collate = BoolFromPrintInfo(printInfo, NSPrintMustCollate, true);

    chosen.options.page.orientation =
        ([printInfo orientation] == NSPaperOrientationLandscape)
            ? IOPrintOrientation::Landscape
            : IOPrintOrientation::Portrait;

    // paperSize follows the orientation, so a landscape sheet comes back
    // already turned. The recogniser wants portrait measurements and the
    // orientation is carried separately above, so it is turned back rather
    // than applied twice.
    NSSize paper = [printInfo paperSize];
    CGFloat widthPoints = paper.width;
    CGFloat heightPoints = paper.height;
    if ([printInfo orientation] == NSPaperOrientationLandscape) {
        std::swap(widthPoints, heightPoints);
    }

    const IOPaperDimensions dimensions{PointsToHundredthsMM(widthPoints),
                                       PointsToHundredthsMM(heightPoints)};
    if (dimensions.IsValid()) {
        chosen.options.page.paperSize =
            IOPaperSizeFromDimensions(dimensions.widthHundredthsMM,
                                      dimensions.heightHundredthsMM);
        if (chosen.options.page.paperSize == IOPaperSize::Unknown) {
            chosen.options.page.paperSize = IOPaperSize::Custom;
            chosen.options.page.customSize = dimensions;
        }
    }

    chosen.options.page.margins.leftHundredthsMM =
        PointsToHundredthsMM([printInfo leftMargin]);
    chosen.options.page.margins.rightHundredthsMM =
        PointsToHundredthsMM([printInfo rightMargin]);
    chosen.options.page.margins.topHundredthsMM =
        PointsToHundredthsMM([printInfo topMargin]);
    chosen.options.page.margins.bottomHundredthsMM =
        PointsToHundredthsMM([printInfo bottomMargin]);

    // Duplex is not on NSPrintInfo at all - it lives in the PMPrintSettings
    // underneath - so it is left at its default rather than guessed. The
    // panel still applies the user's choice to the job it would run itself;
    // what is missing is a way to read it back out, and claiming a value here
    // would be inventing one.

    if (!BoolFromPrintInfo(printInfo, NSPrintAllPages, true)) {
        const int first = std::max(1, IntFromPrintInfo(printInfo, NSPrintFirstPage, 1));
        const int last = IntFromPrintInfo(printInfo, NSPrintLastPage, first);

        // The panel does not know the document's length, so the upper bound
        // is whatever the user typed. Capped so a slip of the keyboard cannot
        // turn into an enormous vector; pages past the end are dropped when
        // the range meets the paginated document anyway.
        constexpr int kMaxPages = 100000;
        for (int page = first;
             page <= last && static_cast<int>(chosen.pageRange.size()) < kMaxPages;
             ++page) {
            chosen.pageRange.push_back(page);
        }
    }

    if ([[printInfo jobDisposition] isEqualToString:NSPrintSaveJob]) {
        chosen.printToFile = true;
        if (NSURL* url = [[printInfo dictionary] objectForKey:NSPrintJobSavingURL]) {
            chosen.outputFilePath = FromNSString([url path]);
        }
    }
}

}  // namespace

    NativePrintResult UltraCanvasNativeDialogs::RequestPrintSettings(
            const std::string& documentName,
            UltraCanvasWindowBase* parent) {
        (void)parent;         // run application-modal, as the other panels here do
        (void)documentName;   // the panel shows no document name; the job
                              // carries its own through IOPrintJob::jobName

        NativePrintResult chosen;

        @autoreleasepool {
            // A copy, not the shared instance: the panel writes the user's
            // choices into whatever it is given, and editing the process-wide
            // NSPrintInfo would change the defaults for every later job. The
            // copy is owned by ARC, which this directory's .mm files are
            // compiled with.
            NSPrintInfo* printInfo = [[NSPrintInfo sharedPrintInfo] copy];

            NSPrintPanel* panel = [NSPrintPanel printPanel];

            // Cast back to the option type: NS_OPTIONS is an enum with a fixed
            // underlying type, so in Objective-C++ `a | b` yields NSUInteger,
            // and C++ will not convert that back to the enum implicitly the
            // way C would.
            [panel setOptions:static_cast<NSPrintPanelOptions>(
                                  [panel options] | NSPrintPanelShowsCopies |
                                  NSPrintPanelShowsPageRange |
                                  NSPrintPanelShowsPaperSize |
                                  NSPrintPanelShowsOrientation)];

            if ([panel runModalWithPrintInfo:printInfo] == NSModalResponseOK) {
                ReadPrintInfo(printInfo, chosen);

                // A confirmed panel that named no printer is not a choice we
                // can act on, so it is reported as a cancellation rather than
                // as an OK with an empty name that fails one layer later.
                chosen.accepted = !chosen.printerName.empty();
            }
        }

        return chosen;
    }

    bool UltraCanvasNativeDialogs::ShowPrintDialog(
            const std::string& documentName,
            const std::string& textContent,
            UltraCanvasWindowBase* parent) {

        // Prints through PrinterDevice and the CUPS transport, with the
        // settings the user chose in the panel.
        //
        // The version this replaces showed no print panel: it wrote the text
        // to a fixed path in the temp directory - the same path on every
        // call, so two documents printed in quick succession raced for it -
        // asked NSWorkspace to open it, and returned true without waiting,
        // whether or not anything was ever printed.
        return PrintTextWithDialog(documentName, textContent, parent).success;
    }

} // namespace UltraCanvas

#endif // TARGET_OS_MAC && !TARGET_OS_IPHONE
#endif // __APPLE__ && __MACH__