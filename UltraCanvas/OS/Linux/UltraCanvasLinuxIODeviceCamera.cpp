// OS/Linux/UltraCanvasLinuxIODeviceCamera.cpp
// V4L2 camera backend: enumeration, capability walk, mmap streaming capture
// and UVC controls.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#ifdef __linux__

#include "../../include/IODeviceManager/UltraCanvasIODeviceCamera.h"
#include "../../include/IODeviceManager/UltraCanvasIODeviceManager.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {
namespace {

// ============================================================================
// IOCTL HELPER
// ============================================================================

// V4L2 ioctls are interrupted by any signal the process happens to take, and
// a plain call would surface that as a device error. Retrying on EINTR is
// required, not defensive.
int RetryIoctl(int fd, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ::ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

std::string ErrnoText() { return std::strerror(errno); }

// ============================================================================
// FORMAT MAPPING
// ============================================================================

struct FormatMapping {
    uint32_t fourcc;
    CameraPixelFormat format;
};

const FormatMapping kFormatMappings[] = {
    {V4L2_PIX_FMT_YUYV,   CameraPixelFormat::YUYV},
    {V4L2_PIX_FMT_UYVY,   CameraPixelFormat::UYVY},
    {V4L2_PIX_FMT_NV12,   CameraPixelFormat::NV12},
    {V4L2_PIX_FMT_YUV420, CameraPixelFormat::YU12},
    {V4L2_PIX_FMT_MJPEG,  CameraPixelFormat::MJPEG},
    {V4L2_PIX_FMT_JPEG,   CameraPixelFormat::JPEG},
    {V4L2_PIX_FMT_H264,   CameraPixelFormat::H264},
    {V4L2_PIX_FMT_RGB24,  CameraPixelFormat::RGB24},
    {V4L2_PIX_FMT_BGR24,  CameraPixelFormat::BGR24},
    {V4L2_PIX_FMT_GREY,   CameraPixelFormat::Gray8},
};

CameraPixelFormat FormatFromFourcc(uint32_t fourcc) {
    for (const auto& mapping : kFormatMappings) {
        if (mapping.fourcc == fourcc) {
            return mapping.format;
        }
    }
    return CameraPixelFormat::Unknown;
}

uint32_t FourccFromFormat(CameraPixelFormat format) {
    for (const auto& mapping : kFormatMappings) {
        if (mapping.format == format) {
            return mapping.fourcc;
        }
    }
    return 0;
}

// ============================================================================
// CONTROL MAPPING
// ============================================================================

struct ControlMapping {
    CameraControl control;
    uint32_t id;
    uint32_t autoId;    // 0 when the control has no automatic companion
};

const ControlMapping kControlMappings[] = {
    {CameraControl::Brightness,              V4L2_CID_BRIGHTNESS,             0},
    {CameraControl::Contrast,                V4L2_CID_CONTRAST,               0},
    {CameraControl::Saturation,              V4L2_CID_SATURATION,             0},
    {CameraControl::Hue,                     V4L2_CID_HUE,                    V4L2_CID_HUE_AUTO},
    {CameraControl::Sharpness,               V4L2_CID_SHARPNESS,              0},
    {CameraControl::Gamma,                   V4L2_CID_GAMMA,                  0},
    {CameraControl::Gain,                    V4L2_CID_GAIN,                   V4L2_CID_AUTOGAIN},
    {CameraControl::Exposure,                V4L2_CID_EXPOSURE_ABSOLUTE,      V4L2_CID_EXPOSURE_AUTO},
    {CameraControl::Focus,                   V4L2_CID_FOCUS_ABSOLUTE,         V4L2_CID_FOCUS_AUTO},
    {CameraControl::WhiteBalanceTemperature, V4L2_CID_WHITE_BALANCE_TEMPERATURE,
                                             V4L2_CID_AUTO_WHITE_BALANCE},
    {CameraControl::Zoom,                    V4L2_CID_ZOOM_ABSOLUTE,          0},
    {CameraControl::Pan,                     V4L2_CID_PAN_ABSOLUTE,           0},
    {CameraControl::Tilt,                    V4L2_CID_TILT_ABSOLUTE,          0},
    {CameraControl::BacklightCompensation,   V4L2_CID_BACKLIGHT_COMPENSATION, 0},
};

const ControlMapping* FindControl(CameraControl control) {
    for (const auto& mapping : kControlMappings) {
        if (mapping.control == control) {
            return &mapping;
        }
    }
    return nullptr;
}

// ============================================================================
// DEVICE
// ============================================================================

class V4L2CameraDevice : public CameraDevice {
public:
    explicit V4L2CameraDevice(const IODeviceInfo& info)
        : CameraDevice(info, CameraType::Webcam) {}

    // Stops and joins the capture thread here, not in ~CameraDevice: by the
    // time the base destructor runs this object is gone, and a thread still
    // calling DeliverFrame() would be reading freed memory.
    ~V4L2CameraDevice() override {
        StopCaptureThread();
        ReleaseBuffers();
        CloseDevice();
    }

protected:
    IODeviceResult DoConnect() override {
        const std::string node = GetDeviceInfo().connectionPath;

        fd = ::open(node.c_str(), O_RDWR | O_NONBLOCK, 0);
        if (fd < 0) {
            const int error = errno;
            return IODeviceResult::BackendError(
                error == EACCES ? IODeviceResultCode::AccessDenied
                                : IODeviceResultCode::ConnectionFailed,
                "Could not open " + node + ": " + ErrnoText() +
                    (error == EACCES ? " (a udev rule or the 'video' group "
                                       "usually grants this)"
                                     : ""),
                error);
        }

        v4l2_capability capability = {};
        if (RetryIoctl(fd, VIDIOC_QUERYCAP, &capability) < 0) {
            const int error = errno;
            CloseDevice();
            return IODeviceResult::BackendError(IODeviceResultCode::CommunicationError,
                                                "VIDIOC_QUERYCAP failed on " + node +
                                                    ": " + ErrnoText(),
                                                error);
        }

        if (!(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            CloseDevice();
            return IODeviceResult::Error(IODeviceResultCode::NotSupported,
                                         node + " is a V4L2 node but not a capture "
                                                "device");
        }
        usesStreaming = (capability.capabilities & V4L2_CAP_STREAMING) != 0;

        return IODeviceResult::Ok();
    }

    void DoDisconnect() override {
        StopCaptureThread();
        ReleaseBuffers();
        CloseDevice();
    }

    IODeviceResult DoGetCapabilities(CameraCapabilities& caps) override {
        if (fd < 0) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                         "Camera capabilities need an open device");
        }

        // Formats, then the frame sizes each one offers, then the rates each
        // size offers - the order V4L2 nests them in.
        for (uint32_t index = 0;; ++index) {
            v4l2_fmtdesc description = {};
            description.index = index;
            description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

            if (RetryIoctl(fd, VIDIOC_ENUM_FMT, &description) < 0) {
                break;
            }

            const CameraPixelFormat format = FormatFromFourcc(description.pixelformat);
            if (format == CameraPixelFormat::Unknown) {
                continue;
            }

            CameraFormatCapability entry;
            entry.format = format;
            EnumerateFrameSizes(description.pixelformat, entry);
            caps.formats.push_back(std::move(entry));
        }

        for (const auto& mapping : kControlMappings) {
            CameraControlRange range;
            if (QueryControl(mapping, range)) {
                caps.controls.emplace_back(mapping.control, range);
            }
        }

        caps.supportsStreaming = IOSupportFrom(usesStreaming);
        caps.supportsStillCapture = IOSupport::Yes;
        return IODeviceResult::Ok(GetDeviceId());
    }

    IODeviceResult DoApplyConfiguration(const CameraConfiguration& config) override {
        if (fd < 0) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                         "Configuring the camera needs an open device");
        }

        v4l2_format format = {};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = static_cast<uint32_t>(config.resolution.width);
        format.fmt.pix.height = static_cast<uint32_t>(config.resolution.height);
        format.fmt.pix.pixelformat = FourccFromFormat(config.pixelFormat);
        format.fmt.pix.field = V4L2_FIELD_ANY;

        if (RetryIoctl(fd, VIDIOC_S_FMT, &format) < 0) {
            const int error = errno;
            return IODeviceResult::BackendError(IODeviceResultCode::NotSupported,
                                                std::string("VIDIOC_S_FMT failed: ") +
                                                    ErrnoText(),
                                                error, GetDeviceId());
        }

        // V4L2 may return a different format from the one asked for; the
        // negotiated one is what the buffers will actually contain, so it is
        // what gets recorded rather than the request.
        negotiated.resolution = CameraResolution(static_cast<int>(format.fmt.pix.width),
                                                 static_cast<int>(format.fmt.pix.height));
        negotiated.pixelFormat = FormatFromFourcc(format.fmt.pix.pixelformat);
        negotiated.bufferCount = config.bufferCount;
        negotiatedSizeImage = format.fmt.pix.sizeimage;

        if (config.frameRate > 0) {
            v4l2_streamparm parameters = {};
            parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            parameters.parm.capture.timeperframe.numerator = 1;
            parameters.parm.capture.timeperframe.denominator =
                static_cast<uint32_t>(config.frameRate);
            // A driver that will not set the rate still captures at its own,
            // so this is not fatal.
            RetryIoctl(fd, VIDIOC_S_PARM, &parameters);
        }

        return IODeviceResult::Ok(GetDeviceId());
    }

    IODeviceResult DoCaptureFrame(CameraFrame& frame) override {
        // A running stream already owns the buffer pool; take the next frame
        // off it rather than tearing the pipeline down and rebuilding it.
        if (ShouldKeepStreaming()) {
            return IODeviceResult::Error(
                IODeviceResultCode::DeviceBusy,
                "This camera is streaming; take frames from the stream callback",
                GetDeviceId());
        }

        IODeviceResult prepared = PrepareBuffers();
        if (!prepared.success) {
            return prepared;
        }

        IODeviceResult started = StartCapture();
        if (!started.success) {
            ReleaseBuffers();
            return started;
        }

        IODeviceResult result = DequeueFrame(frame, /*timeoutMillis=*/5000);

        StopCapture();
        ReleaseBuffers();
        return result;
    }

    IODeviceResult DoStartStream() override {
        IODeviceResult prepared = PrepareBuffers();
        if (!prepared.success) {
            return prepared;
        }

        IODeviceResult started = StartCapture();
        if (!started.success) {
            ReleaseBuffers();
            return started;
        }

        captureThread = std::thread([this] { CaptureLoop(); });
        return IODeviceResult::Ok(GetDeviceId());
    }

    void DoStopStream() override {
        StopCaptureThread();
        StopCapture();
        ReleaseBuffers();
    }

    IODeviceResult DoGetControl(CameraControl control, int& value) override {
        const ControlMapping* mapping = FindControl(control);
        if (!mapping || fd < 0) {
            return IODeviceResult::Error(IODeviceResultCode::NotSupported,
                                         "Unsupported control", GetDeviceId());
        }

        v4l2_control query = {};
        query.id = mapping->id;
        if (RetryIoctl(fd, VIDIOC_G_CTRL, &query) < 0) {
            const int error = errno;
            return IODeviceResult::BackendError(IODeviceResultCode::CommunicationError,
                                                std::string("VIDIOC_G_CTRL failed: ") +
                                                    ErrnoText(),
                                                error, GetDeviceId());
        }
        value = query.value;
        return IODeviceResult::Ok(GetDeviceId());
    }

    IODeviceResult DoSetControl(CameraControl control, int value) override {
        const ControlMapping* mapping = FindControl(control);
        if (!mapping || fd < 0) {
            return IODeviceResult::Error(IODeviceResultCode::NotSupported,
                                         "Unsupported control", GetDeviceId());
        }

        // A control under automatic control rejects a manual write, so the
        // automatic mode is switched off first; a caller setting a value has
        // asked for manual by doing so.
        if (mapping->autoId != 0) {
            SetAutoControl(*mapping, false);
        }

        v4l2_control update = {};
        update.id = mapping->id;
        update.value = value;
        if (RetryIoctl(fd, VIDIOC_S_CTRL, &update) < 0) {
            const int error = errno;
            return IODeviceResult::BackendError(IODeviceResultCode::CommunicationError,
                                                std::string("VIDIOC_S_CTRL failed: ") +
                                                    ErrnoText(),
                                                error, GetDeviceId());
        }
        return IODeviceResult::Ok(GetDeviceId());
    }

    IODeviceResult DoSetControlAuto(CameraControl control, bool automatic) override {
        const ControlMapping* mapping = FindControl(control);
        if (!mapping || mapping->autoId == 0 || fd < 0) {
            return IODeviceResult::Error(IODeviceResultCode::NotSupported,
                                         "This control has no automatic mode",
                                         GetDeviceId());
        }
        if (!SetAutoControl(*mapping, automatic)) {
            const int error = errno;
            return IODeviceResult::BackendError(IODeviceResultCode::CommunicationError,
                                                std::string("Setting automatic mode "
                                                            "failed: ") +
                                                    ErrnoText(),
                                                error, GetDeviceId());
        }
        return IODeviceResult::Ok(GetDeviceId());
    }

private:
    struct MappedBuffer {
        void* start = nullptr;
        size_t length = 0;
    };

    // ===== CAPABILITY WALK =====

    void EnumerateFrameSizes(uint32_t fourcc, CameraFormatCapability& entry) {
        for (uint32_t index = 0;; ++index) {
            v4l2_frmsizeenum sizes = {};
            sizes.index = index;
            sizes.pixel_format = fourcc;

            if (RetryIoctl(fd, VIDIOC_ENUM_FRAMESIZES, &sizes) < 0) {
                break;
            }

            if (sizes.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                const CameraResolution resolution(
                    static_cast<int>(sizes.discrete.width),
                    static_cast<int>(sizes.discrete.height));
                entry.resolutions.push_back(resolution);
                if (entry.frameRates.empty()) {
                    EnumerateFrameRates(fourcc, resolution, entry.frameRates);
                }
            } else {
                // A stepwise or continuous range is not a list, so only its
                // largest is recorded. CameraCapabilities treats an empty
                // resolution list as "did not enumerate" rather than "none",
                // which is what keeps such a camera usable.
                entry.resolutions.emplace_back(
                    static_cast<int>(sizes.stepwise.max_width),
                    static_cast<int>(sizes.stepwise.max_height));
                break;
            }
        }
    }

    void EnumerateFrameRates(uint32_t fourcc, const CameraResolution& resolution,
                             std::vector<int>& rates) {
        for (uint32_t index = 0;; ++index) {
            v4l2_frmivalenum intervals = {};
            intervals.index = index;
            intervals.pixel_format = fourcc;
            intervals.width = static_cast<uint32_t>(resolution.width);
            intervals.height = static_cast<uint32_t>(resolution.height);

            if (RetryIoctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &intervals) < 0) {
                break;
            }
            if (intervals.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
                break;
            }
            // V4L2 reports the interval between frames; callers think in
            // frames per second, so it is inverted here.
            if (intervals.discrete.numerator > 0) {
                rates.push_back(static_cast<int>(intervals.discrete.denominator /
                                                 intervals.discrete.numerator));
            }
        }
    }

    bool QueryControl(const ControlMapping& mapping, CameraControlRange& range) {
        v4l2_queryctrl query = {};
        query.id = mapping.id;

        if (RetryIoctl(fd, VIDIOC_QUERYCTRL, &query) < 0) {
            return false;
        }
        if (query.flags & V4L2_CTRL_FLAG_DISABLED) {
            return false;
        }

        range.supported = true;
        range.minimum = query.minimum;
        range.maximum = query.maximum;
        range.step = query.step > 0 ? query.step : 1;
        range.defaultValue = query.default_value;

        if (mapping.autoId != 0) {
            v4l2_queryctrl autoQuery = {};
            autoQuery.id = mapping.autoId;
            range.autoCapable = RetryIoctl(fd, VIDIOC_QUERYCTRL, &autoQuery) == 0 &&
                                !(autoQuery.flags & V4L2_CTRL_FLAG_DISABLED);
        }
        return true;
    }

    bool SetAutoControl(const ControlMapping& mapping, bool automatic) {
        v4l2_control update = {};
        update.id = mapping.autoId;

        // V4L2_CID_EXPOSURE_AUTO is an enumeration rather than a flag, so it
        // takes a mode constant where the others take 0 or 1.
        if (mapping.autoId == V4L2_CID_EXPOSURE_AUTO) {
            update.value = automatic ? V4L2_EXPOSURE_APERTURE_PRIORITY
                                     : V4L2_EXPOSURE_MANUAL;
        } else {
            update.value = automatic ? 1 : 0;
        }
        return RetryIoctl(fd, VIDIOC_S_CTRL, &update) == 0;
    }

    // ===== BUFFERS =====

    IODeviceResult PrepareBuffers() {
        if (!buffers.empty()) {
            return IODeviceResult::Ok(GetDeviceId());
        }
        if (fd < 0) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                         "Capture needs an open device", GetDeviceId());
        }

        const int wanted = negotiated.bufferCount > 0 ? negotiated.bufferCount : 4;

        v4l2_requestbuffers request = {};
        request.count = static_cast<uint32_t>(wanted);
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;

        if (RetryIoctl(fd, VIDIOC_REQBUFS, &request) < 0) {
            const int error = errno;
            return IODeviceResult::BackendError(IODeviceResultCode::BackendError,
                                                std::string("VIDIOC_REQBUFS failed: ") +
                                                    ErrnoText(),
                                                error, GetDeviceId());
        }
        if (request.count < 2) {
            ReleaseBuffers();
            return IODeviceResult::Error(IODeviceResultCode::BackendError,
                                         "The driver would not give at least two "
                                         "capture buffers",
                                         GetDeviceId());
        }

        for (uint32_t index = 0; index < request.count; ++index) {
            v4l2_buffer description = {};
            description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            description.memory = V4L2_MEMORY_MMAP;
            description.index = index;

            if (RetryIoctl(fd, VIDIOC_QUERYBUF, &description) < 0) {
                const int error = errno;
                ReleaseBuffers();
                return IODeviceResult::BackendError(
                    IODeviceResultCode::BackendError,
                    std::string("VIDIOC_QUERYBUF failed: ") + ErrnoText(), error,
                    GetDeviceId());
            }

            MappedBuffer buffer;
            buffer.length = description.length;
            buffer.start = ::mmap(nullptr, description.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, description.m.offset);
            if (buffer.start == MAP_FAILED) {
                const int error = errno;
                ReleaseBuffers();
                return IODeviceResult::BackendError(
                    IODeviceResultCode::OutOfMemory,
                    std::string("Mapping a capture buffer failed: ") + ErrnoText(),
                    error, GetDeviceId());
            }
            buffers.push_back(buffer);

            if (RetryIoctl(fd, VIDIOC_QBUF, &description) < 0) {
                const int error = errno;
                ReleaseBuffers();
                return IODeviceResult::BackendError(
                    IODeviceResultCode::BackendError,
                    std::string("VIDIOC_QBUF failed: ") + ErrnoText(), error,
                    GetDeviceId());
            }
        }
        return IODeviceResult::Ok(GetDeviceId());
    }

    void ReleaseBuffers() {
        for (auto& buffer : buffers) {
            if (buffer.start && buffer.start != MAP_FAILED) {
                ::munmap(buffer.start, buffer.length);
            }
        }
        buffers.clear();

        if (fd >= 0) {
            // Count 0 tells the driver to free the pool it allocated.
            v4l2_requestbuffers release = {};
            release.count = 0;
            release.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            release.memory = V4L2_MEMORY_MMAP;
            RetryIoctl(fd, VIDIOC_REQBUFS, &release);
        }
    }

    // ===== CAPTURE =====

    IODeviceResult StartCapture() {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (RetryIoctl(fd, VIDIOC_STREAMON, &type) < 0) {
            const int error = errno;
            return IODeviceResult::BackendError(IODeviceResultCode::BackendError,
                                                std::string("VIDIOC_STREAMON failed: ") +
                                                    ErrnoText(),
                                                error, GetDeviceId());
        }
        return IODeviceResult::Ok(GetDeviceId());
    }

    void StopCapture() {
        if (fd < 0) {
            return;
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        RetryIoctl(fd, VIDIOC_STREAMOFF, &type);
    }

    // Waits for one frame, copies it out and requeues the buffer. The device
    // is opened non-blocking so a stalled camera cannot wedge the caller, and
    // poll() supplies the timeout.
    IODeviceResult DequeueFrame(CameraFrame& frame, int timeoutMillis) {
        pollfd waiter = {};
        waiter.fd = fd;
        waiter.events = POLLIN;

        int ready = 0;
        do {
            ready = ::poll(&waiter, 1, timeoutMillis);
        } while (ready == -1 && errno == EINTR);

        if (ready == 0) {
            return IODeviceResult::Error(IODeviceResultCode::Timeout,
                                         "The camera delivered no frame in time",
                                         GetDeviceId());
        }
        if (ready < 0) {
            const int error = errno;
            return IODeviceResult::BackendError(IODeviceResultCode::IOError,
                                                std::string("Waiting for a frame "
                                                            "failed: ") +
                                                    ErrnoText(),
                                                error, GetDeviceId());
        }

        v4l2_buffer buffer = {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;

        if (RetryIoctl(fd, VIDIOC_DQBUF, &buffer) < 0) {
            const int error = errno;
            return IODeviceResult::BackendError(IODeviceResultCode::IOError,
                                                std::string("VIDIOC_DQBUF failed: ") +
                                                    ErrnoText(),
                                                error, GetDeviceId());
        }

        if (buffer.index < buffers.size()) {
            const MappedBuffer& mapped = buffers[buffer.index];
            // bytesused, not the buffer length: for MJPEG the two differ by
            // however much the frame happened to compress by, and copying the
            // whole buffer would append garbage to every frame.
            const size_t used = buffer.bytesused > 0 ? buffer.bytesused : mapped.length;

            frame.data.assign(static_cast<const uint8_t*>(mapped.start),
                              static_cast<const uint8_t*>(mapped.start) + used);
            frame.resolution = negotiated.resolution;
            frame.format = negotiated.pixelFormat;

            // The driver's own timestamp, taken when the frame was captured
            // rather than when this thread got round to reading it.
            frame.timestampMicros =
                static_cast<uint64_t>(buffer.timestamp.tv_sec) * 1000000ull +
                static_cast<uint64_t>(buffer.timestamp.tv_usec);
        }

        // Requeued immediately: a buffer held by the caller is one the driver
        // cannot fill.
        RetryIoctl(fd, VIDIOC_QBUF, &buffer);

        return IODeviceResult::Ok(GetDeviceId());
    }

    void CaptureLoop() {
        while (ShouldKeepStreaming()) {
            CameraFrame frame;
            // A short timeout so the loop notices StopStream() promptly
            // instead of sitting in poll() for seconds.
            IODeviceResult result = DequeueFrame(frame, /*timeoutMillis=*/200);
            if (!result.success) {
                if (result.code == IODeviceResultCode::Timeout) {
                    continue;
                }
                break;
            }
            DeliverFrame(frame);
        }
    }

    void StopCaptureThread() {
        if (captureThread.joinable()) {
            captureThread.join();
        }
    }

    void CloseDevice() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    int fd = -1;
    bool usesStreaming = false;
    CameraConfiguration negotiated;
    uint32_t negotiatedSizeImage = 0;
    std::vector<MappedBuffer> buffers;
    std::thread captureThread;
};

// ============================================================================
// ENUMERATION
// ============================================================================

std::vector<IODevicePtr> EnumerateV4L2Cameras() {
    std::vector<IODevicePtr> cameras;

    DIR* directory = ::opendir("/dev");
    if (!directory) {
        return cameras;
    }

    std::vector<std::string> nodes;
    while (dirent* entry = ::readdir(directory)) {
        const std::string name = entry->d_name;
        if (name.rfind("video", 0) == 0) {
            nodes.push_back("/dev/" + name);
        }
    }
    ::closedir(directory);

    std::sort(nodes.begin(), nodes.end());

    for (const std::string& node : nodes) {
        const int fd = ::open(node.c_str(), O_RDWR | O_NONBLOCK, 0);
        if (fd < 0) {
            continue;
        }

        v4l2_capability capability = {};
        const bool queried = RetryIoctl(fd, VIDIOC_QUERYCAP, &capability) == 0;
        ::close(fd);

        // Modern kernels give one camera several /dev/video* nodes - metadata
        // and output nodes among them - so anything that is not a capture
        // device is skipped rather than offered as a camera that yields no
        // frames.
        if (!queried || !(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            continue;
        }

        IODeviceInfo info;
        info.deviceId = "v4l2:" + node;
        info.name = reinterpret_cast<const char*>(capability.card);
        info.model = info.name;
        info.category = IODeviceCategory::Camera;
        info.backend = "V4L2";
        info.connectionPath = node;
        info.state = IODeviceState::Disconnected;
        info.attributes["driver"] = reinterpret_cast<const char*>(capability.driver);

        const std::string bus = reinterpret_cast<const char*>(capability.bus_info);
        if (!bus.empty()) {
            info.attributes["bus"] = bus;
            if (bus.rfind("usb", 0) == 0) {
                info.transport = IODeviceTransport::USB;
            } else if (bus.rfind("PCI", 0) == 0 || bus.rfind("pci", 0) == 0) {
                info.transport = IODeviceTransport::PCI;
            }
        }

        cameras.push_back(std::make_shared<V4L2CameraDevice>(info));
    }

    return cameras;
}

}  // namespace

namespace Internal {

void RegisterV4L2CameraBackend(IODeviceManager& manager) {
    manager.RegisterEnumerator(IODeviceCategory::Camera, "V4L2", EnumerateV4L2Cameras);
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // __linux__
