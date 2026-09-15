// OS/Linux/UltraCanvasLinuxIODeviceScanner.cpp
// SANE scanner backend: enumeration, option walk, and page scanning.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#if defined(__linux__) && defined(ULTRACANVAS_HAS_SANE)

#include "../../include/IODeviceManager/UltraCanvasIODeviceScanner.h"
#include "../../include/IODeviceManager/UltraCanvasIODeviceManager.h"

#include <sane/sane.h>
#include <sane/saneopts.h>

#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace {

// ============================================================================
// LIBRARY LIFETIME
// ============================================================================

// sane_init/sane_exit are process-global and not reference-counted by the
// library, so the count is kept here: a second scanner opening must not
// re-init, and the first one closing must not tear the library out from under
// the others.
class SaneLibrary {
public:
    static bool Acquire() {
        std::lock_guard<std::mutex> lock(mutex);
        if (refCount == 0) {
            SANE_Int version = 0;
            if (sane_init(&version, nullptr) != SANE_STATUS_GOOD) {
                return false;
            }
        }
        ++refCount;
        return true;
    }

    static void Release() {
        std::lock_guard<std::mutex> lock(mutex);
        if (refCount > 0 && --refCount == 0) {
            sane_exit();
        }
    }

private:
    static std::mutex mutex;
    static int refCount;
};

std::mutex SaneLibrary::mutex;
int SaneLibrary::refCount = 0;

// ============================================================================
// UNIT CONVERSION
// ============================================================================

// SANE expresses scan geometry in millimetres as a fixed-point value; this
// module works in hundredths of a millimetre throughout, so the two meet
// here rather than at every call site.
int FixedToHundredthsMM(SANE_Word value) {
    return static_cast<int>(SANE_UNFIX(value) * 100.0);
}

SANE_Word HundredthsMMToFixed(int value) {
    return SANE_FIX(static_cast<double>(value) / 100.0);
}

ScanColorMode ModeFromSaneName(const std::string& name) {
    if (name == SANE_VALUE_SCAN_MODE_COLOR)    return ScanColorMode::Color;
    if (name == SANE_VALUE_SCAN_MODE_GRAY)     return ScanColorMode::Grayscale;
    if (name == SANE_VALUE_SCAN_MODE_LINEART)  return ScanColorMode::Lineart;
    if (name == SANE_VALUE_SCAN_MODE_HALFTONE) return ScanColorMode::Halftone;
    return ScanColorMode::Unknown;
}

const char* ModeToSaneName(ScanColorMode mode) {
    switch (mode) {
        case ScanColorMode::Color:     return SANE_VALUE_SCAN_MODE_COLOR;
        case ScanColorMode::Grayscale: return SANE_VALUE_SCAN_MODE_GRAY;
        case ScanColorMode::Lineart:   return SANE_VALUE_SCAN_MODE_LINEART;
        case ScanColorMode::Halftone:  return SANE_VALUE_SCAN_MODE_HALFTONE;
        case ScanColorMode::Unknown:   break;
    }
    return SANE_VALUE_SCAN_MODE_COLOR;
}

// Backends name their sources freely, so the mapping is by substring rather
// than by exact match: "ADF Duplex", "Automatic Document Feeder(left
// aligned)" and "Duplex ADF" all mean the same thing.
ScanSource SourceFromSaneName(const std::string& name) {
    std::string lowered;
    lowered.reserve(name.size());
    for (char c : name) {
        lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    const bool duplex = lowered.find("duplex") != std::string::npos;
    if (lowered.find("adf") != std::string::npos ||
        lowered.find("feeder") != std::string::npos) {
        return duplex ? ScanSource::ADFDuplex : ScanSource::ADF;
    }
    if (lowered.find("transparency") != std::string::npos ||
        lowered.find("tpu") != std::string::npos) {
        return ScanSource::TransparencyUnit;
    }
    if (lowered.find("flatbed") != std::string::npos) {
        return ScanSource::Flatbed;
    }
    return ScanSource::Auto;
}

IODeviceResultCode CodeFromSaneStatus(SANE_Status status) {
    switch (status) {
        case SANE_STATUS_GOOD:          return IODeviceResultCode::Success;
        case SANE_STATUS_UNSUPPORTED:   return IODeviceResultCode::NotSupported;
        case SANE_STATUS_CANCELLED:     return IODeviceResultCode::Cancelled;
        case SANE_STATUS_DEVICE_BUSY:   return IODeviceResultCode::DeviceBusy;
        case SANE_STATUS_INVAL:         return IODeviceResultCode::InvalidArgument;
        case SANE_STATUS_JAMMED:        return IODeviceResultCode::HardwareError;
        // An empty feeder is how a multi-page run ends, so it is reported as
        // DeviceNotFound, which ScanPages() treats as "no more pages".
        case SANE_STATUS_NO_DOCS:       return IODeviceResultCode::DeviceNotFound;
        case SANE_STATUS_COVER_OPEN:    return IODeviceResultCode::HardwareError;
        case SANE_STATUS_IO_ERROR:      return IODeviceResultCode::IOError;
        case SANE_STATUS_NO_MEM:        return IODeviceResultCode::OutOfMemory;
        case SANE_STATUS_ACCESS_DENIED: return IODeviceResultCode::AccessDenied;
        default:                        return IODeviceResultCode::BackendError;
    }
}

// ============================================================================
// DEVICE
// ============================================================================

class SaneScannerDevice : public ScannerDevice {
public:
    explicit SaneScannerDevice(const IODeviceInfo& info) : ScannerDevice(info) {}

    ~SaneScannerDevice() override { CloseHandle(); }

protected:
    IODeviceResult DoConnect() override {
        if (!SaneLibrary::Acquire()) {
            return IODeviceResult::Error(IODeviceResultCode::BackendUnavailable,
                                         "SANE could not be initialised");
        }
        libraryHeld = true;

        const std::string name = GetDeviceInfo().connectionPath;
        const SANE_Status status = sane_open(name.c_str(), &handle);
        if (status != SANE_STATUS_GOOD) {
            handle = nullptr;
            SaneLibrary::Release();
            libraryHeld = false;
            return IODeviceResult::BackendError(
                CodeFromSaneStatus(status),
                "Could not open scanner '" + name + "': " + sane_strstatus(status),
                static_cast<int>(status));
        }
        return IODeviceResult::Ok();
    }

    void DoDisconnect() override { CloseHandle(); }

    IODeviceResult DoGetCapabilities(ScanCapabilities& caps) override {
        if (!handle) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                         "Scanner capabilities need an open device");
        }

        // Option 0 holds the option count; the rest are walked by name
        // because their indices differ per backend.
        for (SANE_Int index = 1;; ++index) {
            const SANE_Option_Descriptor* option =
                sane_get_option_descriptor(handle, index);
            if (!option) {
                break;
            }
            if (!option->name) {
                continue;
            }
            const std::string name = option->name;

            if (name == SANE_NAME_SCAN_RESOLUTION) {
                ReadResolutions(*option, caps);
            } else if (name == SANE_NAME_SCAN_MODE) {
                ReadStringList(*option, [&](const std::string& value) {
                    const ScanColorMode mode = ModeFromSaneName(value);
                    if (mode != ScanColorMode::Unknown) {
                        caps.colorModes.push_back(mode);
                    }
                });
            } else if (name == SANE_NAME_SCAN_SOURCE) {
                ReadStringList(*option, [&](const std::string& value) {
                    caps.sources.push_back(SourceFromSaneName(value));
                });
            } else if (name == SANE_NAME_SCAN_BR_X &&
                       option->constraint_type == SANE_CONSTRAINT_RANGE) {
                caps.maxArea.rightHundredthsMM =
                    FixedToHundredthsMM(option->constraint.range->max);
            } else if (name == SANE_NAME_SCAN_BR_Y &&
                       option->constraint_type == SANE_CONSTRAINT_RANGE) {
                caps.maxArea.bottomHundredthsMM =
                    FixedToHundredthsMM(option->constraint.range->max);
            } else if (name == SANE_NAME_BIT_DEPTH) {
                ReadWordList(*option, caps.bitDepths);
            } else if (name == SANE_NAME_PREVIEW) {
                caps.supportsPreview = IOSupport::Yes;
            }
        }

        return IODeviceResult::Ok(GetDeviceId());
    }

    IODeviceResult DoApplyConfiguration(const ScanConfiguration& config) override {
        if (!handle) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                         "Configuring the scanner needs an open device");
        }

        // Source first: it changes which geometry the scanner accepts, so
        // setting an area before it can be silently clamped to the old bed.
        if (config.source != ScanSource::Auto) {
            SetStringOptionMatching(SANE_NAME_SCAN_SOURCE, [&](const std::string& value) {
                return SourceFromSaneName(value) == config.source;
            });
        }

        SetStringOption(SANE_NAME_SCAN_MODE, ModeToSaneName(config.colorMode));

        if (config.resolutionDpi > 0) {
            SetIntOption(SANE_NAME_SCAN_RESOLUTION, config.resolutionDpi);
        }
        if (config.bitDepth > 0) {
            SetIntOption(SANE_NAME_BIT_DEPTH, config.bitDepth);
        }

        if (config.area.IsValid()) {
            SetFixedOption(SANE_NAME_SCAN_TL_X, config.area.leftHundredthsMM);
            SetFixedOption(SANE_NAME_SCAN_TL_Y, config.area.topHundredthsMM);
            SetFixedOption(SANE_NAME_SCAN_BR_X, config.area.rightHundredthsMM);
            SetFixedOption(SANE_NAME_SCAN_BR_Y, config.area.bottomHundredthsMM);
        }

        return IODeviceResult::Ok(GetDeviceId());
    }

    IODeviceResult DoScanPage(ScannedImage& image) override {
        if (!handle) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                         "Scanning needs an open device", GetDeviceId());
        }

        cancelled = false;

        SANE_Status status = sane_start(handle);
        if (status != SANE_STATUS_GOOD) {
            return IODeviceResult::BackendError(
                CodeFromSaneStatus(status),
                std::string("The scan would not start: ") + sane_strstatus(status),
                static_cast<int>(status), GetDeviceId());
        }

        SANE_Parameters parameters = {};
        status = sane_get_parameters(handle, &parameters);
        if (status != SANE_STATUS_GOOD) {
            sane_cancel(handle);
            return IODeviceResult::BackendError(
                CodeFromSaneStatus(status),
                std::string("The scanner would not describe the page: ") +
                    sane_strstatus(status),
                static_cast<int>(status), GetDeviceId());
        }

        image.width = parameters.pixels_per_line;
        image.bytesPerLine = parameters.bytes_per_line;
        image.bitsPerSample = parameters.depth;
        image.channels = parameters.format == SANE_FRAME_RGB ? 3 : 1;
        image.colorMode = parameters.format == SANE_FRAME_RGB
                              ? ScanColorMode::Color
                              : configuration.colorMode;

        // A scanner that does not know the page length in advance reports -1,
        // which is normal for a feeder: the height is counted as lines arrive.
        const bool heightKnown = parameters.lines >= 0;
        if (heightKnown) {
            image.data.reserve(static_cast<size_t>(parameters.bytes_per_line) *
                               static_cast<size_t>(parameters.lines));
        }

        IODeviceResult result = ReadImage(image, parameters, heightKnown);

        sane_cancel(handle);    // ends the frame, as SANE requires after reading

        // The height is left for ScannerDevice to derive from the data: a
        // feeder scan reports lines as -1 until the sheet has gone through,
        // and that arithmetic belongs in one place rather than in every
        // backend.
        return result;
    }

    void DoCancelScan() override {
        cancelled = true;
        if (handle) {
            // Safe to call from another thread while sane_read() blocks; that
            // is what SANE documents it for.
            sane_cancel(handle);
        }
    }

private:
    IODeviceResult ReadImage(ScannedImage& image, const SANE_Parameters& parameters,
                             bool heightKnown) {
        std::vector<uint8_t> buffer(64 * 1024);

        while (ShouldContinueScanning() && !cancelled) {
            SANE_Int read = 0;
            const SANE_Status status =
                sane_read(handle, buffer.data(), static_cast<SANE_Int>(buffer.size()),
                          &read);

            if (status == SANE_STATUS_EOF) {
                break;
            }
            if (status != SANE_STATUS_GOOD) {
                if (status == SANE_STATUS_CANCELLED) {
                    return IODeviceResult::Error(IODeviceResultCode::Cancelled,
                                                 "The scan was cancelled", GetDeviceId());
                }
                return IODeviceResult::BackendError(
                    CodeFromSaneStatus(status),
                    std::string("Reading the page failed: ") + sane_strstatus(status),
                    static_cast<int>(status), GetDeviceId());
            }

            if (read > 0) {
                image.data.insert(image.data.end(), buffer.begin(),
                                  buffer.begin() + read);

                if (heightKnown && parameters.bytes_per_line > 0) {
                    const size_t expected =
                        static_cast<size_t>(parameters.bytes_per_line) *
                        static_cast<size_t>(parameters.lines);
                    if (expected > 0) {
                        ReportProgress(static_cast<float>(image.data.size()) /
                                       static_cast<float>(expected));
                    }
                }
            }
        }

        if (cancelled || !ShouldContinueScanning()) {
            return IODeviceResult::Error(IODeviceResultCode::Cancelled,
                                         "The scan was cancelled", GetDeviceId());
        }
        return IODeviceResult::Ok(GetDeviceId());
    }

    // ===== OPTION HELPERS =====

    // Backends order their options differently, so every lookup is by name.
    const SANE_Option_Descriptor* FindOption(const char* name, SANE_Int& outIndex) {
        for (SANE_Int index = 1;; ++index) {
            const SANE_Option_Descriptor* option =
                sane_get_option_descriptor(handle, index);
            if (!option) {
                break;
            }
            if (option->name && std::strcmp(option->name, name) == 0) {
                outIndex = index;
                return option;
            }
        }
        return nullptr;
    }

    void ReadResolutions(const SANE_Option_Descriptor& option, ScanCapabilities& caps) {
        if (option.constraint_type == SANE_CONSTRAINT_WORD_LIST &&
            option.constraint.word_list) {
            const SANE_Word* list = option.constraint.word_list;
            const SANE_Word count = list[0];
            for (SANE_Word i = 1; i <= count; ++i) {
                const int dpi = option.type == SANE_TYPE_FIXED
                                    ? static_cast<int>(SANE_UNFIX(list[i]))
                                    : static_cast<int>(list[i]);
                caps.resolutions.push_back(dpi);
            }
        } else if (option.constraint_type == SANE_CONSTRAINT_RANGE &&
                   option.constraint.range) {
            // A continuous range is not a list; the bounds are recorded and
            // the list left empty, which reads as "did not enumerate".
            const SANE_Range* range = option.constraint.range;
            caps.minResolutionDpi = option.type == SANE_TYPE_FIXED
                                        ? static_cast<int>(SANE_UNFIX(range->min))
                                        : static_cast<int>(range->min);
            caps.maxResolutionDpi = option.type == SANE_TYPE_FIXED
                                        ? static_cast<int>(SANE_UNFIX(range->max))
                                        : static_cast<int>(range->max);
        }
    }

    template <typename Callback>
    void ReadStringList(const SANE_Option_Descriptor& option, Callback callback) {
        if (option.constraint_type != SANE_CONSTRAINT_STRING_LIST ||
            !option.constraint.string_list) {
            return;
        }
        for (const SANE_String_Const* entry = option.constraint.string_list;
             *entry != nullptr; ++entry) {
            callback(std::string(*entry));
        }
    }

    void ReadWordList(const SANE_Option_Descriptor& option, std::vector<int>& values) {
        if (option.constraint_type != SANE_CONSTRAINT_WORD_LIST ||
            !option.constraint.word_list) {
            return;
        }
        const SANE_Word* list = option.constraint.word_list;
        const SANE_Word count = list[0];
        for (SANE_Word i = 1; i <= count; ++i) {
            values.push_back(static_cast<int>(list[i]));
        }
    }

    void SetIntOption(const char* name, int value) {
        SANE_Int index = 0;
        const SANE_Option_Descriptor* option = FindOption(name, index);
        if (!option) {
            return;
        }
        SANE_Word word = option->type == SANE_TYPE_FIXED
                             ? SANE_FIX(static_cast<double>(value))
                             : static_cast<SANE_Word>(value);
        SANE_Int info = 0;
        sane_control_option(handle, index, SANE_ACTION_SET_VALUE, &word, &info);
    }

    void SetFixedOption(const char* name, int hundredthsMM) {
        SANE_Int index = 0;
        const SANE_Option_Descriptor* option = FindOption(name, index);
        if (!option) {
            return;
        }
        SANE_Word word = option->type == SANE_TYPE_FIXED
                             ? HundredthsMMToFixed(hundredthsMM)
                             : static_cast<SANE_Word>(hundredthsMM / 100);
        SANE_Int info = 0;
        sane_control_option(handle, index, SANE_ACTION_SET_VALUE, &word, &info);
    }

    void SetStringOption(const char* name, const char* value) {
        SANE_Int index = 0;
        const SANE_Option_Descriptor* option = FindOption(name, index);
        if (!option || option->type != SANE_TYPE_STRING) {
            return;
        }
        // SANE writes back into this buffer, so it is sized to the option's
        // declared length rather than to the string being set.
        std::vector<char> buffer(static_cast<size_t>(option->size) + 1, 0);
        std::strncpy(buffer.data(), value, buffer.size() - 1);
        SANE_Int info = 0;
        sane_control_option(handle, index, SANE_ACTION_SET_VALUE, buffer.data(), &info);
    }

    template <typename Predicate>
    void SetStringOptionMatching(const char* name, Predicate matches) {
        SANE_Int index = 0;
        const SANE_Option_Descriptor* option = FindOption(name, index);
        if (!option || option->constraint_type != SANE_CONSTRAINT_STRING_LIST) {
            return;
        }
        for (const SANE_String_Const* entry = option->constraint.string_list;
             *entry != nullptr; ++entry) {
            if (matches(std::string(*entry))) {
                SetStringOption(name, *entry);
                return;
            }
        }
    }

    void CloseHandle() {
        if (handle) {
            sane_close(handle);
            handle = nullptr;
        }
        if (libraryHeld) {
            SaneLibrary::Release();
            libraryHeld = false;
        }
    }

    SANE_Handle handle = nullptr;
    bool libraryHeld = false;
    std::atomic<bool> cancelled{false};
};

// ============================================================================
// ENUMERATION
// ============================================================================

std::vector<IODevicePtr> EnumerateSaneScanners() {
    std::vector<IODevicePtr> scanners;

    if (!SaneLibrary::Acquire()) {
        return scanners;
    }

    const SANE_Device** devices = nullptr;
    // local_only false so networked backends (net, escl, airscan) are
    // included; a driverless network scanner is the common case now.
    if (sane_get_devices(&devices, SANE_FALSE) == SANE_STATUS_GOOD && devices) {
        for (int i = 0; devices[i] != nullptr; ++i) {
            const SANE_Device& device = *devices[i];
            if (!device.name) {
                continue;
            }

            IODeviceInfo info;
            info.deviceId = std::string("sane:") + device.name;
            info.connectionPath = device.name;
            info.category = IODeviceCategory::Scanner;
            info.backend = "SANE";
            info.manufacturer = device.vendor ? device.vendor : "";
            info.model = device.model ? device.model : "";
            info.state = IODeviceState::Disconnected;

            info.name = info.manufacturer.empty()
                            ? info.model
                            : info.manufacturer + " " + info.model;
            if (info.name.empty()) {
                info.name = device.name;
            }
            if (device.type) {
                info.description = device.type;
            }

            // SANE device names carry their backend and connection, e.g.
            // "escl:https://…" or "genesys:libusb:001:004".
            const std::string name = device.name;
            if (name.rfind("net:", 0) == 0 || name.rfind("escl:", 0) == 0 ||
                name.rfind("airscan:", 0) == 0) {
                info.transport = IODeviceTransport::Network;
            } else if (name.find("libusb:") != std::string::npos ||
                       name.find("usb") != std::string::npos) {
                info.transport = IODeviceTransport::USB;
            }

            scanners.push_back(std::make_shared<SaneScannerDevice>(info));
        }
    }

    SaneLibrary::Release();
    return scanners;
}

}  // namespace

namespace Internal {

void RegisterSaneScannerBackend(IODeviceManager& manager) {
    manager.RegisterEnumerator(IODeviceCategory::Scanner, "SANE", EnumerateSaneScanners);
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // __linux__ && ULTRACANVAS_HAS_SANE
