// OS/MSWindows/UltraCanvasWindowsRemovableDrives.cpp
// The Windows backend of UltraCanvasRemovableDrives - what the notification
// area's "Safely Remove Hardware" does.
//
//   - List: the disks SetupAPI lists (GUID_DEVINTERFACE_DISK) whose bus is
//     USB, SD, MMC or FireWire, or whose media is removable, and the drive
//     letters on each - a letter belongs to a disk when
//     IOCTL_STORAGE_GET_DEVICE_NUMBER answers the same number for both.
//     Only removable and fixed letters are opened: optical drives and
//     network mappings are never touched, so nothing spins up or waits out
//     a timeout. The disk holding the Windows directory is never listed.
//   - SafelyRemove: each of the disk's volumes is flushed, then
//     CM_Request_Device_Eject on the disk's parent - the USB mass-storage
//     device or the card reader, the node Windows ejects. Windows refuses
//     ("vetoes") while a program has a file open there, and the error says
//     which program where Windows names it.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif

#include "UltraCanvasRemovableDrivesBackend.h"

#include <map>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>

namespace UltraCanvas {
namespace RemovableDrivesBackend {

namespace {

    // GUID_DEVINTERFACE_DISK, spelled out: <ntddstor.h> is not in every SDK
    // the build meets.
    const GUID kDiskInterface = {0x53f56307, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};

    std::string WideToUtf8(const wchar_t* text) {
        if (!text || !*text) return std::string();
        const int length = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1) return std::string();
        std::string out(static_cast<size_t>(length - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr, nullptr);
        return out;
    }

    std::wstring Utf8ToWide(const std::string& text) {
        if (text.empty()) return std::wstring();
        const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(length > 0 ? length : 0), L'\0');
        if (length > 0) {
            ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
        }
        return out;
    }

    std::string Trim(std::string text) {
        while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) text.pop_back();
        size_t start = 0;
        while (start < text.size() && text[start] == ' ') ++start;
        return text.substr(start);
    }

    // The storage device number of an open disk or volume; -1 when it has none.
    long DeviceNumberOf(HANDLE handle) {
        STORAGE_DEVICE_NUMBER number{};
        DWORD returned = 0;
        if (!::DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number),
                               &returned, nullptr)) {
            return -1;
        }
        return number.DeviceType == FILE_DEVICE_DISK ? static_cast<long>(number.DeviceNumber) : -1;
    }

    struct Disk {
        DEVINST devInst = 0;
        std::string instanceId;
        long number = -1;
        STORAGE_BUS_TYPE bus = BusTypeUnknown;
        bool removableMedia = false;
        std::string name;
    };

    void ReadDescriptor(HANDLE handle, Disk& disk) {
        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        std::vector<BYTE> buffer(1024);
        DWORD returned = 0;
        if (!::DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                               static_cast<DWORD>(buffer.size()), &returned, nullptr) ||
            returned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            return;
        }
        const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        disk.bus = descriptor->BusType;
        disk.removableMedia = descriptor->RemovableMedia != FALSE;
        auto field = [&](DWORD offset) {
            if (offset == 0 || offset >= returned) return std::string();
            return Trim(reinterpret_cast<const char*>(buffer.data() + offset));
        };
        const std::string vendor = field(descriptor->VendorIdOffset);
        const std::string product = field(descriptor->ProductIdOffset);
        disk.name = vendor.empty() ? product : product.empty() ? vendor : vendor + " " + product;
    }

    std::vector<Disk> ListDisks() {
        std::vector<Disk> disks;
        HDEVINFO set = ::SetupDiGetClassDevsW(&kDiskInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return disks;
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        for (DWORD index = 0; ::SetupDiEnumDeviceInterfaces(set, nullptr, &kDiskInterface, index, &interfaceData);
             ++index) {
            DWORD needed = 0;
            ::SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &needed, nullptr);
            if (needed == 0) continue;
            std::vector<BYTE> detailBuffer(needed);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            SP_DEVINFO_DATA deviceData{};
            deviceData.cbSize = sizeof(deviceData);
            if (!::SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, needed, nullptr, &deviceData)) {
                continue;
            }
            Disk disk;
            disk.devInst = deviceData.DevInst;
            wchar_t instanceId[MAX_DEVICE_ID_LEN] = {};
            if (::CM_Get_Device_IDW(deviceData.DevInst, instanceId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
                disk.instanceId = WideToUtf8(instanceId);
            }
            // No access asked: the ioctls used here need none, and a disk
            // another program holds open still answers.
            HANDLE handle = ::CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, 0, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                disk.number = DeviceNumberOf(handle);
                ReadDescriptor(handle, disk);
                ::CloseHandle(handle);
            }
            if (!disk.instanceId.empty()) disks.push_back(disk);
        }
        ::SetupDiDestroyDeviceInfoList(set);
        return disks;
    }

    struct Letter {
        wchar_t letter = L'C';
        long number = -1;
        std::string label;
        uint64_t size = 0;
    };

    std::vector<Letter> ListLetters() {
        std::vector<Letter> letters;
        const DWORD mask = ::GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (!(mask & (1u << i))) continue;
            Letter entry;
            entry.letter = static_cast<wchar_t>(L'A' + i);
            const std::wstring root = std::wstring(1, entry.letter) + L":\\";
            const UINT type = ::GetDriveTypeW(root.c_str());
            if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED) continue;
            const std::wstring device = L"\\\\.\\" + std::wstring(1, entry.letter) + L":";
            HANDLE handle = ::CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, 0, nullptr);
            if (handle == INVALID_HANDLE_VALUE) continue;
            entry.number = DeviceNumberOf(handle);
            ::CloseHandle(handle);
            wchar_t label[MAX_PATH + 1] = {};
            if (::GetVolumeInformationW(root.c_str(), label, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0)) {
                entry.label = WideToUtf8(label);
            }
            ULARGE_INTEGER total{};
            if (::GetDiskFreeSpaceExW(root.c_str(), nullptr, &total, nullptr)) entry.size = total.QuadPart;
            letters.push_back(entry);
        }
        return letters;
    }

    long SystemDiskNumber() {
        wchar_t windows[MAX_PATH] = {};
        if (::GetWindowsDirectoryW(windows, MAX_PATH) == 0 || windows[0] == 0) return -1;
        const std::wstring device = L"\\\\.\\" + std::wstring(1, windows[0]) + L":";
        HANDLE handle = ::CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return -1;
        const long number = DeviceNumberOf(handle);
        ::CloseHandle(handle);
        return number;
    }

    std::string BusName(STORAGE_BUS_TYPE bus) {
        switch (bus) {
            case BusTypeUsb:  return "USB";
            case BusTypeSd:   return "SD card";
            case BusTypeMmc:  return "SD card";
            case BusType1394: return "FireWire";
            default:          return "";
        }
    }

    bool Removable(const Disk& disk) {
        return disk.removableMedia || !BusName(disk.bus).empty();
    }

    // Write out what Windows still holds for a volume; best effort - the
    // eject request flushes too, but a volume flushed first is ready sooner.
    void FlushVolume(wchar_t letter) {
        const std::wstring device = L"\\\\.\\" + std::wstring(1, letter) + L":";
        HANDLE handle = ::CreateFileW(device.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return;
        ::FlushFileBuffers(handle);
        ::CloseHandle(handle);
    }

} // namespace

std::string BackendName() { return "win32"; }

std::vector<RemovableDrive> List(std::string& error) {
    (void)error;
    std::vector<RemovableDrive> out;
    const long systemDisk = SystemDiskNumber();
    const std::vector<Letter> letters = ListLetters();
    for (const Disk& disk : ListDisks()) {
        if (!Removable(disk) || (disk.number >= 0 && disk.number == systemDisk)) continue;
        RemovableDrive drive;
        drive.id = disk.instanceId;
        drive.name = disk.name;
        drive.bus = BusName(disk.bus);
        for (const Letter& letter : letters) {
            if (disk.number < 0 || letter.number != disk.number) continue;
            drive.mountPoints.push_back(WideToUtf8((std::wstring(1, letter.letter) + L":\\").c_str()));
            drive.labels.push_back(letter.label);
            drive.sizeBytes += letter.size;
        }
        out.push_back(drive);
    }
    return out;
}

bool SafelyRemove(const std::string& id, std::string& error) {
    std::wstring instance = Utf8ToWide(id);
    DEVINST disk = 0;
    if (::CM_Locate_DevNodeW(&disk, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        error = "The drive is no longer plugged in.";
        return false;
    }
    // Flush its volumes first.
    long number = -1;
    for (const Disk& entry : ListDisks()) {
        if (entry.instanceId == id) number = entry.number;
    }
    if (number >= 0) {
        for (const Letter& letter : ListLetters()) {
            if (letter.number == number) FlushVolume(letter.letter);
        }
    }
    // The parent is what Windows ejects (the USB mass-storage device, the
    // card reader); the disk itself where it has no removable parent.
    DEVINST parent = 0;
    const DEVINST targets[2] = {::CM_Get_Parent(&parent, disk, 0) == CR_SUCCESS ? parent : disk, disk};
    PNP_VETO_TYPE veto = PNP_VetoTypeUnknown;
    wchar_t vetoName[MAX_PATH] = {};
    for (DEVINST target : targets) {
        // Windows sometimes refuses the first request while it finishes
        // writing; it is asked a few times.
        for (int attempt = 0; attempt < 3; ++attempt) {
            veto = PNP_VetoTypeUnknown;
            vetoName[0] = 0;
            const CONFIGRET result = ::CM_Request_Device_EjectW(target, &veto, vetoName, MAX_PATH, 0);
            if (result == CR_SUCCESS && veto == PNP_VetoTypeUnknown) return true;
            ::Sleep(300);
        }
    }
    const std::string who = WideToUtf8(vetoName);
    if (veto == PNP_VetoOutstandingOpen || veto == PNP_VetoDevice || veto == PNP_VetoLegacyDevice) {
        error = "A program still has a file open on the drive" +
                (who.empty() || who.find('\\') != std::string::npos ? std::string(".") : " (" + who + ").") +
                " Close it and try again.";
    } else {
        error = "Windows did not let the drive be removed" + (who.empty() ? std::string(".") : " (" + who + ").");
    }
    return false;
}

} // namespace RemovableDrivesBackend
} // namespace UltraCanvas
