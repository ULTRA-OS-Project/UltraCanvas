// OS/Linux/UltraCanvasNativeDialogsLinux.cpp
// Linux implementation of native OS dialogs using GTK+
// Uses unified DialogType, DialogButtons, DialogResult from UltraCanvasModalDialog.h
// Version: 2.1.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "UltraCanvasNativeDialogs.h"

#ifdef __linux__

#include <gtk-3.0/gtk/gtk.h>
#include <gtk/gtkunixprint.h>
#include <gdk/gdkx.h>   // gdk_x11_window_foreign_new_for_display — parent dialogs to X11 windows
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
                    // gtk_init_check() reports failure (e.g. no DISPLAY) instead of
                    // aborting the whole process like gtk_init() would.
                    if (gtk_init_check(&argc, &argv)) {
                        initialized = true;
                    }
                }
            }

        private:
            GtkInitializer() : initialized(false) {}
            bool initialized;
        };

        // Returns true only if GTK is usable; callers should bail out gracefully
        // (empty/Cancel result) when this returns false.
        bool EnsureGtkInitialized() {
            GtkInitializer::GetInstance().EnsureInitialized();
            return GtkInitializer::GetInstance().IsInitialized();
        }

        // Make a freshly-created GTK dialog transient-for (and modal to) its
        // UltraCanvas parent. UltraCanvas windows are raw X11 windows, not
        // GtkWindows, so gtk_window_set_transient_for() can't be used directly:
        // we wrap the parent's XID in a foreign GdkWindow and set the transient
        // hint at the GDK level. With no usable parent we fall back to the old
        // keep-above behaviour so the dialog still surfaces above the app.
        void ParentDialogToWindow(GtkWidget* dialog, UltraCanvasWindowBase* parent) {
            if (!dialog) return;
            NativeWindowHandle xid = parent ? parent->GetNativeHandle() : 0;
            if (xid) {
                gtk_widget_realize(dialog);
                GdkWindow* dlgGdk = gtk_widget_get_window(dialog);
                if (dlgGdk) {
                    GdkDisplay* disp = gdk_window_get_display(dlgGdk);
                    GdkWindow* parentGdk = gdk_x11_window_foreign_new_for_display(disp, xid);
                    if (parentGdk) {
                        gdk_window_set_transient_for(dlgGdk, parentGdk);
                        g_object_unref(parentGdk);
                    }
                }
                gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
            } else {
                gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
            }
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

        // Key under which each GtkFileFilter stores its primary extension so the
        // Save dialog can rewrite the filename when the file type is switched.
        const char* const kPrimaryExtKey = "uc-primary-ext";

        // GTK's Save file chooser does NOT rewrite the extension in the name
        // entry when the user picks a different "Dateityp"/file-type filter, so
        // the filename keeps whatever extension it started with. We fix that
        // ourselves: on every filter change, swap the current name's extension
        // for the one attached to the newly selected filter.
        void OnSaveFilterChanged(GObject* chooserObj, GParamSpec*, gpointer) {
            GtkFileChooser* chooser = GTK_FILE_CHOOSER(chooserObj);
            GtkFileFilter* filter = gtk_file_chooser_get_filter(chooser);
            if (!filter) return;

            const char* ext = static_cast<const char*>(
                    g_object_get_data(G_OBJECT(filter), kPrimaryExtKey));
            // "All files" (*) and pattern-less filters leave the name untouched.
            if (!ext || !*ext || std::strcmp(ext, "*") == 0) return;

            gchar* current = gtk_file_chooser_get_current_name(chooser);
            if (!current) return;
            std::string name = current;
            g_free(current);
            if (name.empty()) return;

            // Replace an existing trailing extension with the filter's extension;
            // append if the name has none. A leading dot (dot-file) is not an
            // extension separator, so it is preserved.
            size_t dot = name.find_last_of('.');
            size_t sep = name.find_last_of("/\\");
            if (dot != std::string::npos &&
                (sep == std::string::npos || dot > sep) && dot != 0) {
                name.erase(dot);
            }
            name += ".";
            name += ext;

            gtk_file_chooser_set_current_name(chooser, name.c_str());
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

        if (!EnsureGtkInitialized()) return DialogResult::Cancel;

        GtkMessageType gtkType = ToGtkMessageType(type);
        GtkButtonsType gtkButtons = ToGtkButtonsType(buttons);

        // GTK-level parent stays null; the real X11 parent is applied below.
        GtkWindow* parentWindow = nullptr;

        GtkWidget* dialog = gtk_message_dialog_new(
                parentWindow,
                static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                gtkType,
                gtkButtons,
                "%s",
                message.c_str()
        );

        gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());

        // Parent + make modal to the owning window (keep-above fallback if none).
        ParentDialogToWindow(dialog, parent);

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
        if (!EnsureGtkInitialized()) return {};

        // GTK-level parent stays null; the real X11 parent is applied below.
        GtkWindow* parentWindow = nullptr;

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
                options.title.empty() ? "Open File" : options.title.c_str(),
                parentWindow,
                GTK_FILE_CHOOSER_ACTION_OPEN,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Open", GTK_RESPONSE_ACCEPT,
                nullptr
        );

        // Parent + make modal to the owning window (keep-above fallback if none).
        ParentDialogToWindow(dialog, options.parentWindow);

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
            UltraCanvasWindowBase*  parent) {

        FileDialogOptions options;
        options.title = title;
        options.filters = filters;
        options.initialDirectory = initialDir;
        options.parentWindow = parent;
        return OpenMultipleFiles(options);
    }

    std::vector<std::string> UltraCanvasNativeDialogs::OpenMultipleFiles(const FileDialogOptions& options) {
        if (!EnsureGtkInitialized()) return {};

        // GTK-level parent stays null; the real X11 parent is applied below.
        GtkWindow* parentWindow = nullptr;

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
                options.title.empty() ? "Open Files" : options.title.c_str(),
                parentWindow,
                GTK_FILE_CHOOSER_ACTION_OPEN,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Open", GTK_RESPONSE_ACCEPT,
                nullptr
        );

        // Parent + make modal to the owning window (keep-above fallback if none).
        ParentDialogToWindow(dialog, options.parentWindow);

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
        if (!EnsureGtkInitialized()) return {};

        // GTK-level parent stays null; the real X11 parent is applied below.
        GtkWindow* parentWindow = nullptr;

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
                options.title.empty() ? "Save File" : options.title.c_str(),
                parentWindow,
                GTK_FILE_CHOOSER_ACTION_SAVE,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Save", GTK_RESPONSE_ACCEPT,
                nullptr
        );

        // Parent + make modal to the owning window (keep-above fallback if none).
        ParentDialogToWindow(dialog, options.parentWindow);

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
            // Remember the primary extension so OnSaveFilterChanged can rewrite
            // the filename when the user switches file type.
            if (!filter.extensions.empty()) {
                g_object_set_data_full(G_OBJECT(gtkFilter), kPrimaryExtKey,
                                       g_strdup(filter.extensions.front().c_str()), g_free);
            }
            gtk_file_chooser_add_filter(chooser, gtkFilter);
        }

        // Add "All Files" filter if no filters specified
        if (options.filters.empty()) {
            GtkFileFilter* allFilter = gtk_file_filter_new();
            gtk_file_filter_set_name(allFilter, "All Files");
            gtk_file_filter_add_pattern(allFilter, "*");
            gtk_file_chooser_add_filter(chooser, allFilter);
        }

        // Keep the filename's extension in sync with the selected file type.
        // Connected after the filters are added so priming the default filter
        // doesn't rewrite the caller-supplied default filename.
        g_signal_connect(chooser, "notify::filter",
                         G_CALLBACK(OnSaveFilterChanged), nullptr);

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
            UltraCanvasWindowBase*  parent) {

        if (!EnsureGtkInitialized()) return {};

        // GTK-level parent stays null; the real X11 parent is applied below.
        GtkWindow* parentWindow = nullptr;

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
                title.empty() ? "Select Folder" : title.c_str(),
                parentWindow,
                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Select", GTK_RESPONSE_ACCEPT,
                nullptr
        );

        // Parent + make modal to the owning window (keep-above fallback if none).
        ParentDialogToWindow(dialog, parent);

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
            UltraCanvasWindowBase*  parent) {

        NativeInputDialogOptions options;
        options.prompt = prompt;
        options.title = title;
        options.defaultValue = defaultValue;
        options.parentWindow = parent;
        return InputText(options);
    }

    NativeInputResult UltraCanvasNativeDialogs::InputText(const NativeInputDialogOptions& options) {
        NativeInputResult result;
        result.result = DialogResult::Cancel;

        if (!EnsureGtkInitialized()) return result;

        // GTK-level parent stays null; the real X11 parent is applied below.
        GtkWindow* parentWindow = nullptr;

        // Create dialog
        GtkWidget* dialog = gtk_dialog_new_with_buttons(
                options.title.c_str(),
                parentWindow,
                static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_OK", GTK_RESPONSE_OK,
                nullptr
        );

        // Parent + make modal to the owning window (keep-above fallback if none).
        ParentDialogToWindow(dialog, options.parentWindow);

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

    bool UltraCanvasNativeDialogs::ShowPrintDialog(
            const std::string& documentName,
            const std::string& textContent,
            UltraCanvasWindowBase* parent) {

        if (!EnsureGtkInitialized()) return false;

        // GTK-level parent stays null; the real X11 parent is applied below.
        GtkWindow* parentWindow = nullptr;

        // Build a GtkPrintUnixDialog — the standard GTK print dialog
        GtkWidget* dialog = gtk_print_unix_dialog_new(
                documentName.empty() ? "Print" : documentName.c_str(),
                parentWindow
        );

        // Parent + make modal to the owning window (keep-above fallback if none).
        ParentDialogToWindow(dialog, parent);

        // Configure: show all pages tab, hide page range (plain text only)
        GtkPrintCapabilities capabilities = (GtkPrintCapabilities) (GTK_PRINT_CAPABILITY_COPIES |
                                            GTK_PRINT_CAPABILITY_COLLATE |
                                            GTK_PRINT_CAPABILITY_REVERSE);

        gtk_print_unix_dialog_set_manual_capabilities(GTK_PRINT_UNIX_DIALOG(dialog), capabilities);

        bool printed = false;

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            GtkPrinter*     printer  = gtk_print_unix_dialog_get_selected_printer(GTK_PRINT_UNIX_DIALOG(dialog));
            GtkPrintSettings* settings = gtk_print_unix_dialog_get_settings(GTK_PRINT_UNIX_DIALOG(dialog));
            GtkPageSetup*   pageSetup = gtk_print_unix_dialog_get_page_setup(GTK_PRINT_UNIX_DIALOG(dialog));

            if (printer && settings) {
                // Write text content to a temp file and hand it to lpr
                char tmpPath[] = "/tmp/ultratexter_print_XXXXXX";
                int fd = mkstemp(tmpPath);
                if (fd >= 0) {
                    write(fd, textContent.c_str(), textContent.size());
                    close(fd);

                    // Retrieve chosen printer name for lpr -P
                    const gchar* printerName = gtk_printer_get_name(printer);
                    std::string cmd = std::string("lpr -P \"") + printerName + "\" \"" + tmpPath + "\"";
                    int ret = system(cmd.c_str());
                    printed = (ret == 0);

                    unlink(tmpPath);  // Remove temp file after submission
                }
            }

            if (settings)  g_object_unref(settings);
        }

        gtk_widget_destroy(dialog);
        ProcessGtkEvents();

        return printed;
    }
} // namespace UltraCanvas

#endif // __linux__