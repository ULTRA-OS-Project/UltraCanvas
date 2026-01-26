// OS/Linux/UltraCanvasNativeDialogsLinux.cpp
// Linux implementation of native OS dialogs using GTK+
// Uses unified DialogType, DialogButtons, DialogResult from UltraCanvasModalDialog.h
// Version: 2.0.0
// Last Modified: 2026-01-25
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"

#ifdef __linux__

#include <gtk-3.0/gtk/gtk.h>
#include <iostream>
#include <cstring>

namespace UltraCanvas {

    namespace {

// ===== GTK INITIALIZATION HELPER =====
        class GtkInitializer {
        public:
            static GtkInitializer& GetInstance() {
                static GtkInitializer instance;
                return instance;
            }

            bool IsInitialized() const { return initialized; }

            void EnsureInitialized() {
                if (!initialized) {
                    int argc = 0;
                    char** argv = nullptr;
                    gtk_init(&argc, &argv);
                    initialized = true;
                }
            }

        private:
            GtkInitializer() : initialized(false) {}
            bool initialized;
        };

        void EnsureGtkInitialized() {
            GtkInitializer::GetInstance().EnsureInitialized();
        }

// Process pending GTK events
        void ProcessGtkEvents() {
            while (gtk_events_pending()) {
                gtk_main_iteration();
            }
        }

// Convert DialogType to GTK message type
        GtkMessageType ToGtkMessageType(DialogType type) {
            switch (type) {
                case DialogType::Information: return GTK_MESSAGE_INFO;
                case DialogType::Warning:     return GTK_MESSAGE_WARNING;
                case DialogType::Error:       return GTK_MESSAGE_ERROR;
                case DialogType::Question:    return GTK_MESSAGE_QUESTION;
                case DialogType::Custom:
                default:                      return GTK_MESSAGE_INFO;
            }
        }

// Convert DialogButtons to GTK buttons type
        GtkButtonsType ToGtkButtonsType(DialogButtons buttons) {
            switch (buttons) {
                case DialogButtons::OK:              return GTK_BUTTONS_OK;
                case DialogButtons::OKCancel:        return GTK_BUTTONS_OK_CANCEL;
                case DialogButtons::YesNo:           return GTK_BUTTONS_YES_NO;
                case DialogButtons::YesNoCancel:     return GTK_BUTTONS_NONE; // Custom handling
                case DialogButtons::RetryCancel:     return GTK_BUTTONS_NONE; // Custom handling
                case DialogButtons::AbortRetryIgnore: return GTK_BUTTONS_NONE; // Custom handling
                default:                             return GTK_BUTTONS_OK;
            }
        }

// Convert GTK response to DialogResult
        DialogResult FromGtkResponse(gint response) {
            switch (response) {
                case GTK_RESPONSE_OK:
                case GTK_RESPONSE_ACCEPT:
                    return DialogResult::OK;
                case GTK_RESPONSE_CANCEL:
                case GTK_RESPONSE_DELETE_EVENT:
                    return DialogResult::Cancel;
                case GTK_RESPONSE_YES:
                    return DialogResult::Yes;
                case GTK_RESPONSE_NO:
                    return DialogResult::No;
                case GTK_RESPONSE_CLOSE:
                    return DialogResult::Close;
                default:
                    return DialogResult::Cancel;
            }
        }

// Convert FileFilter extensions to GTK filter patterns
        void AddFilterPatterns(GtkFileFilter* filter, const FileFilter& fileFilter) {
            for (const auto& ext : fileFilter.extensions) {
                if (ext == "*") {
                    gtk_file_filter_add_pattern(filter, "*");
                } else {
                    std::string pattern = "*." + ext;
                    gtk_file_filter_add_pattern(filter, pattern.c_str());
                }
            }
        }

    } // anonymous namespace

// ===== MESSAGE DIALOGS =====

    DialogResult UltraCanvasNativeDialogs::ShowInfo(
            const std::string& message,
            const std::string& title,
            NativeWindowHandle parent) {
        return ShowMessage(message, title, DialogType::Information, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowWarning(
            const std::string& message,
            const std::string& title,
            NativeWindowHandle parent) {
        return ShowMessage(message, title, DialogType::Warning, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowError(
            const std::string& message,
            const std::string& title,
            NativeWindowHandle parent) {
        return ShowMessage(message, title, DialogType::Error, DialogButtons::OK, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowQuestion(
            const std::string& message,
            const std::string& title,
            DialogButtons buttons,
            NativeWindowHandle parent) {
        return ShowMessage(message, title, DialogType::Question, buttons, parent);
    }

    DialogResult UltraCanvasNativeDialogs::ShowMessage(
            const std::string& message,
            const std::string& title,
            DialogType type,
            DialogButtons buttons,
            NativeWindowHandle parent) {

        EnsureGtkInitialized();

        GtkMessageType gtkType = ToGtkMessageType(type);
        GtkButtonsType gtkButtons = ToGtkButtonsType(buttons);

        // Get parent GtkWindow if provided
        GtkWindow* parentWindow = nullptr;
        if (parent != nullptr) {
            // The parent handle should be a GtkWindow* when using GTK
            parentWindow = GTK_WINDOW(parent);
        }

        GtkWidget* dialog = gtk_message_dialog_new(
                parentWindow,
                static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                gtkType,
                gtkButtons,
                "%s",
                message.c_str()
        );

        gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());

        // Keep dialog on top
        gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

        // Add custom buttons for special cases
        if (buttons == DialogButtons::YesNoCancel) {
            gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                                   "Yes", GTK_RESPONSE_YES,
                                   "No", GTK_RESPONSE_NO,
                                   "Cancel", GTK_RESPONSE_CANCEL,
                                   nullptr);
        } else if (buttons == DialogButtons::RetryCancel) {
            gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                                   "Retry", GTK_RESPONSE_OK,
                                   "Cancel", GTK_RESPONSE_CANCEL,
                                   nullptr);
        } else if (buttons == DialogButtons::AbortRetryIgnore) {
            gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                                   "Abort", GTK_RESPONSE_REJECT,
                                   "Retry", GTK_RESPONSE_OK,
                                   "Ignore", GTK_RESPONSE_ACCEPT,
                                   nullptr);
        }

        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        ProcessGtkEvents();

        // Handle special button mappings
        if (buttons == DialogButtons::AbortRetryIgnore) {
            switch (response) {
                case GTK_RESPONSE_REJECT: return DialogResult::Abort;
                case GTK_RESPONSE_OK:     return DialogResult::Retry;
                case GTK_RESPONSE_ACCEPT: return DialogResult::Ignore;
                default:                  return DialogResult::Cancel;
            }
        }
        if (buttons == DialogButtons::RetryCancel) {
            if (response == GTK_RESPONSE_OK) return DialogResult::Retry;
        }

        return FromGtkResponse(response);
    }

// ===== CONFIRMATION DIALOGS =====

    bool UltraCanvasNativeDialogs::Confirm(
            const std::string& message,
            const std::string& title,
            NativeWindowHandle parent) {
        DialogResult result = ShowMessage(message, title,
                                          DialogType::Question, DialogButtons::OKCancel, parent);
        return result == DialogResult::OK;
    }

    bool UltraCanvasNativeDialogs::ConfirmYesNo(
            const std::string& message,
            const std::string& title,
            NativeWindowHandle parent) {
        DialogResult result = ShowMessage(message, title,
                                          DialogType::Question, DialogButtons::YesNo, parent);
        return result == DialogResult::Yes;
    }

// ===== FILE DIALOGS =====

    std::string UltraCanvasNativeDialogs::OpenFile(
            const std::string& title,
            const std::vector<FileFilter>& filters,
            const std::string& initialDir,
            NativeWindowHandle parent) {

        NativeFileDialogOptions options;
        options.title = title;
        options.filters = filters;
        options.initialDirectory = initialDir;
        options.parentWindow = parent;
        return OpenFile(options);
    }

    std::string UltraCanvasNativeDialogs::OpenFile(const NativeFileDialogOptions& options) {
        EnsureGtkInitialized();

        // Get parent GtkWindow if provided
        GtkWindow* parentWindow = nullptr;
        if (options.parentWindow != nullptr) {
            parentWindow = GTK_WINDOW(options.parentWindow);
        }

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
                options.title.empty() ? "Open File" : options.title.c_str(),
                parentWindow,
                GTK_FILE_CHOOSER_ACTION_OPEN,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Open", GTK_RESPONSE_ACCEPT,
                nullptr
        );

        // Keep dialog on top
        gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

        GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

        // Set initial directory
        if (!options.initialDirectory.empty()) {
            gtk_file_chooser_set_current_folder(chooser, options.initialDirectory.c_str());
        }

        // Set show hidden files
        gtk_file_chooser_set_show_hidden(chooser, options.showHiddenFiles);

        // Add file filters
        for (const auto& filter : options.filters) {
            GtkFileFilter* gtkFilter = gtk_file_filter_new();
            gtk_file_filter_set_name(gtkFilter, filter.ToDisplayString().c_str());
            AddFilterPatterns(gtkFilter, filter);
            gtk_file_chooser_add_filter(chooser, gtkFilter);
        }

        // Add "All Files" filter if no filters specified
        if (options.filters.empty()) {
            GtkFileFilter* allFilter = gtk_file_filter_new();
            gtk_file_filter_set_name(allFilter, "All Files");
            gtk_file_filter_add_pattern(allFilter, "*");
            gtk_file_chooser_add_filter(chooser, allFilter);
        }

        std::string result;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* filename = gtk_file_chooser_get_filename(chooser);
            if (filename) {
                result = filename;
                g_free(filename);
            }
        }

        gtk_widget_destroy(dialog);
        ProcessGtkEvents();

        return result;
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(
            const std::string& title,
            const std::vector<FileFilter>& filters,
            const std::string& initialDir,
            NativeWindowHandle parent) {

        NativeFileDialogOptions options;
        options.title = title;
        options.filters = filters;
        options.initialDirectory = initialDir;
        options.allowMultiSelect = true;
        options.parentWindow = parent;
        return OpenMultipleFiles(options);
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(const NativeFileDialogOptions& options) {
        EnsureGtkInitialized();

        // Get parent GtkWindow if provided
        GtkWindow* parentWindow = nullptr;
        if (options.parentWindow != nullptr) {
            parentWindow = GTK_WINDOW(options.parentWindow);
        }

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
                options.title.empty() ? "Open Files" : options.title.c_str(),
                parentWindow,
                GTK_FILE_CHOOSER_ACTION_OPEN,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Open", GTK_RESPONSE_ACCEPT,
                nullptr
        );

        // Keep dialog on top
        gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

        GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

        // Enable multiple selection
        gtk_file_chooser_set_select_multiple(chooser, TRUE);

        // Set initial directory
        if (!options.initialDirectory.empty()) {
            gtk_file_chooser_set_current_folder(chooser, options.initialDirectory.c_str());
        }

        // Set show hidden files
        gtk_file_chooser_set_show_hidden(chooser, options.showHiddenFiles);

        // Add file filters
        for (const auto& filter : options.filters) {
            GtkFileFilter* gtkFilter = gtk_file_filter_new();
            gtk_file_filter_set_name(gtkFilter, filter.ToDisplayString().c_str());
            AddFilterPatterns(gtkFilter, filter);
            gtk_file_chooser_add_filter(chooser, gtkFilter);
        }

        // Add "All Files" filter if no filters specified
        if (options.filters.empty()) {
            GtkFileFilter* allFilter = gtk_file_filter_new();
            gtk_file_filter_set_name(allFilter, "All Files");
            gtk_file_filter_add_pattern(allFilter, "*");
            gtk_file_chooser_add_filter(chooser, allFilter);
        }

        std::vector<std::string> results;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            GSList* filenames = gtk_file_chooser_get_filenames(chooser);
            GSList* iter = filenames;
            while (iter) {
                char* filename = static_cast<char*>(iter->data);
                if (filename) {
                    results.push_back(filename);
                    g_free(filename);
                }
                iter = iter->next;
            }
            g_slist_free(filenames);
        }

        gtk_widget_destroy(dialog);
        ProcessGtkEvents();

        return results;
    }

    std::string UltraCanvasNativeDialogs::SaveFile(
            const std::string& title,
            const std::vector<FileFilter>& filters,
            const std::string& initialDir,
            const std::string& defaultFileName,
            NativeWindowHandle parent) {

        NativeFileDialogOptions options;
        options.title = title;
        options.filters = filters;
        options.initialDirectory = initialDir;
        options.defaultFileName = defaultFileName;
        options.parentWindow = parent;
        return SaveFile(options);
    }

    std::string UltraCanvasNativeDialogs::SaveFile(const NativeFileDialogOptions& options) {
        EnsureGtkInitialized();

        // Get parent GtkWindow if provided
        GtkWindow* parentWindow = nullptr;
        if (options.parentWindow != nullptr) {
            parentWindow = GTK_WINDOW(options.parentWindow);
        }

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
                options.title.empty() ? "Save File" : options.title.c_str(),
                parentWindow,
                GTK_FILE_CHOOSER_ACTION_SAVE,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Save", GTK_RESPONSE_ACCEPT,
                nullptr
        );

        // Keep dialog on top
        gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

        GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

        // Enable overwrite confirmation
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

        // Set initial directory
        if (!options.initialDirectory.empty()) {
            gtk_file_chooser_set_current_folder(chooser, options.initialDirectory.c_str());
        }

        // Set default filename
        if (!options.defaultFileName.empty()) {
            gtk_file_chooser_set_current_name(chooser, options.defaultFileName.c_str());
        }

        // Set show hidden files
        gtk_file_chooser_set_show_hidden(chooser, options.showHiddenFiles);

        // Add file filters
        for (const auto& filter : options.filters) {
            GtkFileFilter* gtkFilter = gtk_file_filter_new();
            gtk_file_filter_set_name(gtkFilter, filter.ToDisplayString().c_str());
            AddFilterPatterns(gtkFilter, filter);
            gtk_file_chooser_add_filter(chooser, gtkFilter);
        }

        // Add "All Files" filter if no filters specified
        if (options.filters.empty()) {
            GtkFileFilter* allFilter = gtk_file_filter_new();
            gtk_file_filter_set_name(allFilter, "All Files");
            gtk_file_filter_add_pattern(allFilter, "*");
            gtk_file_chooser_add_filter(chooser, allFilter);
        }

        std::string result;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* filename = gtk_file_chooser_get_filename(chooser);
            if (filename) {
                result = filename;
                g_free(filename);
            }
        }

        gtk_widget_destroy(dialog);
        ProcessGtkEvents();

        return result;
    }

    std::string UltraCanvasNativeDialogs::SelectFolder(
            const std::string& title,
            const std::string& initialDir,
            NativeWindowHandle parent) {

        EnsureGtkInitialized();

        // Get parent GtkWindow if provided
        GtkWindow* parentWindow = nullptr;
        if (parent != nullptr) {
            parentWindow = GTK_WINDOW(parent);
        }

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
                title.empty() ? "Select Folder" : title.c_str(),
                parentWindow,
                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Select", GTK_RESPONSE_ACCEPT,
                nullptr
        );

        // Keep dialog on top
        gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

        GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

        // Set initial directory
        if (!initialDir.empty()) {
            gtk_file_chooser_set_current_folder(chooser, initialDir.c_str());
        }

        std::string result;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char* folder = gtk_file_chooser_get_filename(chooser);
            if (folder) {
                result = folder;
                g_free(folder);
            }
        }

        gtk_widget_destroy(dialog);
        ProcessGtkEvents();

        return result;
    }

// ===== INPUT DIALOGS =====

    NativeInputResult UltraCanvasNativeDialogs::InputText(
            const std::string& prompt,
            const std::string& title,
            const std::string& defaultValue,
            NativeWindowHandle parent) {

        NativeInputDialogOptions options;
        options.prompt = prompt;
        options.title = title;
        options.defaultValue = defaultValue;
        options.parentWindow = parent;
        return InputText(options);
    }

    NativeInputResult UltraCanvasNativeDialogs::InputText(const NativeInputDialogOptions& options) {
        EnsureGtkInitialized();

        NativeInputResult result;
        result.result = DialogResult::Cancel;

        // Get parent GtkWindow if provided
        GtkWindow* parentWindow = nullptr;
        if (options.parentWindow != nullptr) {
            parentWindow = GTK_WINDOW(options.parentWindow);
        }

        // Create dialog
        GtkWidget* dialog = gtk_dialog_new_with_buttons(
                options.title.c_str(),
                parentWindow,
                static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_OK", GTK_RESPONSE_OK,
                nullptr
        );

        // Keep dialog on top
        gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

        // Get content area
        GtkWidget* contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(contentArea), 10);

        // Create vertical box for content
        GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_container_add(GTK_CONTAINER(contentArea), vbox);

        // Add prompt label
        if (!options.prompt.empty()) {
            GtkWidget* label = gtk_label_new(options.prompt.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
        }

        // Add text entry
        GtkWidget* entry = gtk_entry_new();
        if (!options.defaultValue.empty()) {
            gtk_entry_set_text(GTK_ENTRY(entry), options.defaultValue.c_str());
        }
        if (options.password) {
            gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
            gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_PASSWORD);
        }
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

        // Set minimum dialog size
        gtk_window_set_default_size(GTK_WINDOW(dialog), 300, -1);

        gtk_widget_show_all(dialog);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            result.result = DialogResult::OK;
            result.value = gtk_entry_get_text(GTK_ENTRY(entry));
        }

        gtk_widget_destroy(dialog);
        ProcessGtkEvents();

        return result;
    }

    NativeInputResult UltraCanvasNativeDialogs::InputPassword(
            const std::string& prompt,
            const std::string& title,
            NativeWindowHandle parent) {

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
            NativeWindowHandle parent) {

        NativeInputResult result = InputText(prompt, title, defaultValue, parent);
        return result.IsOK() ? result.value : "";
    }

    std::string UltraCanvasNativeDialogs::GetPassword(
            const std::string& prompt,
            const std::string& title,
            NativeWindowHandle parent) {

        NativeInputResult result = InputPassword(prompt, title, parent);
        return result.IsOK() ? result.value : "";
    }

} // namespace UltraCanvas

#endif // __linux__