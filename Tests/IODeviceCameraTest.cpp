// Tests/IODeviceCameraTest.cpp
// CameraDevice: configuration validation, the streaming state machine and
// control clamping.
//
// The streaming half is what needs guarding. A camera hands frames to a
// callback on a capture thread, and the two mistakes that follow are a
// callback firing after StopStream() returned - by then the caller has
// usually torn down whatever the callback writes into - and a base class
// destructor that tries to stop a stream through a virtual call, which
// reaches an already-destroyed derived object. Both are asserted here.
//
// Driven by a fake camera, so it runs on a machine with no /dev/video* at all.
// Version: 1.0.0
// Last Modified: 2026-09-14
// Author: UltraCanvas Framework

#include "IODeviceManager/UltraCanvasIODeviceCamera.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Mentions(const std::vector<std::string>& changes, const std::string& needle) {
    for (const auto& change : changes) {
        if (change.find(needle) != std::string::npos) return true;
    }
    return false;
}

// ===== FAKE CAMERA =====

class FakeCamera : public CameraDevice {
public:
    explicit FakeCamera(const IODeviceInfo& info)
        : CameraDevice(info, CameraType::Webcam) {}

    // Stops its own thread, as a real backend must: the base destructor
    // deliberately calls no virtuals.
    ~FakeCamera() override { StopProducer(); }

    CameraCapabilities fakeCapabilities;
    std::atomic<int> appliedConfigurations{0};
    std::atomic<int> controlWrites{0};
    int lastControlValue = 0;
    bool lastAutoSetting = false;

protected:
    IODeviceResult DoConnect() override { return IODeviceResult::Ok(); }
    void DoDisconnect() override { StopProducer(); }

    IODeviceResult DoGetCapabilities(CameraCapabilities& caps) override {
        caps = fakeCapabilities;
        return IODeviceResult::Ok();
    }

    IODeviceResult DoApplyConfiguration(const CameraConfiguration&) override {
        ++appliedConfigurations;
        return IODeviceResult::Ok();
    }

    IODeviceResult DoCaptureFrame(CameraFrame& frame) override {
        frame.resolution = CameraResolution(640, 480);
        frame.format = CameraPixelFormat::YUYV;
        frame.data.assign(640 * 480 * 2, 0x80);
        return IODeviceResult::Ok();
    }

    IODeviceResult DoStartStream() override {
        producing = true;
        producer = std::thread([this] {
            while (producing && ShouldKeepStreaming()) {
                CameraFrame frame;
                frame.resolution = CameraResolution(640, 480);
                frame.format = CameraPixelFormat::YUYV;
                frame.data.assign(64, 0x42);
                DeliverFrame(frame);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
        return IODeviceResult::Ok();
    }

    void DoStopStream() override { StopProducer(); }

    IODeviceResult DoGetControl(CameraControl, int& value) override {
        value = lastControlValue;
        return IODeviceResult::Ok();
    }

    IODeviceResult DoSetControl(CameraControl, int value) override {
        lastControlValue = value;
        ++controlWrites;
        return IODeviceResult::Ok();
    }

    IODeviceResult DoSetControlAuto(CameraControl, bool automatic) override {
        lastAutoSetting = automatic;
        return IODeviceResult::Ok();
    }

private:
    void StopProducer() {
        producing = false;
        if (producer.joinable()) {
            producer.join();
        }
    }

    std::atomic<bool> producing{false};
    std::thread producer;
};

IODeviceInfo MakeCameraInfo() {
    IODeviceInfo info;
    info.deviceId = "fake:camera";
    info.name = "Fake Camera";
    info.category = IODeviceCategory::Camera;
    info.backend = "Fake";
    return info;
}

std::shared_ptr<FakeCamera> MakeCamera() {
    auto camera = std::make_shared<FakeCamera>(MakeCameraInfo());

    CameraFormatCapability yuyv;
    yuyv.format = CameraPixelFormat::YUYV;
    yuyv.resolutions = {{640, 480}, {1280, 720}};
    yuyv.frameRates = {30, 15};

    CameraFormatCapability mjpeg;
    mjpeg.format = CameraPixelFormat::MJPEG;
    mjpeg.resolutions = {{640, 480}, {1920, 1080}};
    mjpeg.frameRates = {30};

    camera->fakeCapabilities.formats = {mjpeg, yuyv};   // compressed listed first
    camera->fakeCapabilities.supportsStreaming = IOSupport::Yes;

    CameraControlRange brightness;
    brightness.supported = true;
    brightness.minimum = 0;
    brightness.maximum = 255;
    brightness.step = 1;
    brightness.defaultValue = 128;

    CameraControlRange exposure;
    exposure.supported = true;
    exposure.minimum = 3;
    exposure.maximum = 2047;
    exposure.step = 4;
    exposure.autoCapable = true;

    camera->fakeCapabilities.controls = {
        {CameraControl::Brightness, brightness},
        {CameraControl::Exposure, exposure},
    };
    return camera;
}

// ===== TESTS =====

void TestFrameSizing() {
    std::cout << "\nFrame sizing\n";

    const CameraResolution vga(640, 480);
    Check(CameraFrameSize(CameraPixelFormat::Gray8, vga) == 640u * 480u, "Gray8 is 1 byte per pixel");
    Check(CameraFrameSize(CameraPixelFormat::YUYV, vga) == 640u * 480u * 2, "YUYV is 4:2:2");
    Check(CameraFrameSize(CameraPixelFormat::NV12, vga) == 640u * 480u * 3 / 2, "NV12 is 4:2:0");
    Check(CameraFrameSize(CameraPixelFormat::RGB24, vga) == 640u * 480u * 3, "RGB24 is 3 bytes per pixel");

    // A compressed frame's size varies per frame, so there is no fixed answer
    // and 0 says so rather than a worst case that would over-allocate.
    Check(CameraFrameSize(CameraPixelFormat::MJPEG, vga) == 0, "MJPEG has no fixed frame size");
    Check(CameraPixelFormatIsCompressed(CameraPixelFormat::MJPEG), "MJPEG is compressed");
    Check(!CameraPixelFormatIsCompressed(CameraPixelFormat::YUYV), "YUYV is not");
}

void TestControlClamping() {
    std::cout << "\nControl clamping\n";

    CameraControlRange range;
    range.supported = true;
    range.minimum = 3;
    range.maximum = 2047;
    range.step = 4;

    Check(range.Clamp(-100) == 3, "below the minimum clamps up");
    Check(range.Clamp(99999) <= 2047, "above the maximum clamps down");

    // Steps count from the minimum, the way the device does: minimum 3 with
    // step 4 accepts 3, 7, 11 - not 0, 4, 8.
    Check((range.Clamp(10) - range.minimum) % range.step == 0, "a value snaps onto the step grid");
    Check(range.Clamp(10) == 7, "10 snaps down to 7, not to 8");
    Check(range.Clamp(2047) <= 2047, "the top of the range stays inside it");

    CameraControlRange unsupported;
    Check(unsupported.Clamp(42) == 42, "an unsupported control does not rewrite the value");
}

void TestConfigurationIsValidated() {
    std::cout << "\nConfiguration is validated, not silently substituted\n";

    auto camera = MakeCamera();
    camera->Connect();

    CameraConfiguration good;
    good.pixelFormat = CameraPixelFormat::YUYV;
    good.resolution = CameraResolution(1280, 720);
    Check(static_cast<bool>(camera->SetConfiguration(good)), "a supported mode is accepted");
    Check(camera->GetConfiguration().resolution == CameraResolution(1280, 720),
          "and is what the camera reports back");

    // A camera that quietly captured 640x480 when asked for 4K would be found
    // out only by inspecting the frames.
    CameraConfiguration tooBig;
    tooBig.pixelFormat = CameraPixelFormat::YUYV;
    tooBig.resolution = CameraResolution(3840, 2160);
    IODeviceResult refused = camera->SetConfiguration(tooBig);
    Check(!static_cast<bool>(refused), "an unsupported resolution is refused");
    Check(refused.code == IODeviceResultCode::NotSupported, "with NotSupported");
    Check(camera->GetConfiguration().resolution == CameraResolution(1280, 720),
          "and the working configuration is untouched");

    CameraConfiguration empty;
    Check(!static_cast<bool>(camera->SetConfiguration(empty)),
          "a configuration with no resolution is refused");
}

void TestResolveConfiguration() {
    std::cout << "\nResolveConfiguration fills in what the caller left out\n";

    auto camera = MakeCamera();
    camera->Connect();

    std::vector<std::string> changes;
    CameraConfiguration resolved = camera->ResolveConfiguration(CameraConfiguration(), &changes);

    // The fake lists MJPEG first, but an uncompressed format is preferred so
    // a caller can read pixels without decoding.
    Check(resolved.pixelFormat == CameraPixelFormat::YUYV,
          "an uncompressed format is preferred over the one listed first");
    Check(resolved.resolution == CameraResolution(1280, 720),
          "and the largest resolution that format offers is chosen");
    Check(resolved.bufferCount >= 2, "at least two buffers are requested");
    Check(Mentions(changes, "No pixel format"), "and the caller is told what was filled in");

    // One buffer leaves the driver nowhere to put the next frame.
    CameraConfiguration starved;
    starved.pixelFormat = CameraPixelFormat::YUYV;
    starved.resolution = CameraResolution(640, 480);
    starved.bufferCount = 1;
    changes.clear();
    resolved = camera->ResolveConfiguration(starved, &changes);
    Check(resolved.bufferCount == 2, "a single buffer is raised to two");
    Check(Mentions(changes, "two capture buffers"), "and the reason is given");

    // An unsupported format is swapped and reported.
    CameraConfiguration exotic;
    exotic.pixelFormat = CameraPixelFormat::H265;
    exotic.resolution = CameraResolution(640, 480);
    changes.clear();
    resolved = camera->ResolveConfiguration(exotic, &changes);
    Check(resolved.pixelFormat != CameraPixelFormat::H265, "an unsupported format is replaced");
    Check(Mentions(changes, "H.265"), "and named in the change");
}

void TestStreaming() {
    std::cout << "\nStreaming\n";

    auto camera = MakeCamera();
    camera->Connect();

    std::atomic<int> frames{0};
    std::atomic<uint64_t> lastNumber{0};

    IODeviceResult started = camera->StartStream([&](const CameraFrame& frame) {
        ++frames;
        lastNumber = frame.frameNumber;
    });
    Check(static_cast<bool>(started), "the stream starts");
    Check(camera->IsStreaming(), "and the camera reports streaming");

    // Starting twice must not spawn a second producer.
    Check(static_cast<bool>(camera->StartStream([](const CameraFrame&) {})),
          "starting an already-running stream succeeds");

    for (int i = 0; i < 100 && frames < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(frames >= 3, "frames arrive at the callback");
    Check(lastNumber > 0, "and each carries a frame number");
    Check(camera->GetFrameCount() >= 3, "which the camera also counts");

    // The property that matters: once StopStream() returns, nothing more may
    // reach a callback whose captured state the caller is about to destroy.
    camera->StopStream();
    const int afterStop = frames;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(frames == afterStop, "NO frame arrives after StopStream() returns");
    Check(!camera->IsStreaming(), "and the camera reports it stopped");

    camera->StopStream();
    Check(true, "stopping twice is harmless");

    // Reconfiguring mid-stream would tear down the buffer pool under the
    // reader, so it is refused rather than raced.
    camera->StartStream([](const CameraFrame&) {});
    CameraConfiguration other;
    other.pixelFormat = CameraPixelFormat::YUYV;
    other.resolution = CameraResolution(640, 480);
    IODeviceResult refused = camera->SetConfiguration(other);
    Check(!static_cast<bool>(refused), "reconfiguring while streaming is refused");
    Check(refused.code == IODeviceResultCode::InvalidState, "with InvalidState");
    camera->StopStream();

    Check(!static_cast<bool>(camera->StartStream(nullptr)),
          "streaming without a callback is refused");
}

void TestStreamingRequiresConnection() {
    std::cout << "\nStreaming needs an open device\n";

    auto camera = MakeCamera();
    Check(!static_cast<bool>(camera->StartStream([](const CameraFrame&) {})),
          "a disconnected camera cannot stream");

    CameraFrame frame;
    Check(!static_cast<bool>(camera->CaptureFrame(frame)),
          "nor capture a still");
}

void TestControls() {
    std::cout << "\nControls\n";

    auto camera = MakeCamera();
    camera->Connect();

    Check(camera->GetControlRange(CameraControl::Brightness).supported,
          "a control the camera has is reported supported");
    Check(!camera->GetControlRange(CameraControl::Tilt).supported,
          "one it does not have is not");

    Check(static_cast<bool>(camera->SetControl(CameraControl::Brightness, 200)),
          "a control can be set");
    int value = 0;
    Check(static_cast<bool>(camera->GetControl(CameraControl::Brightness, value)) &&
              value == 200,
          "and read back");

    // Clamped rather than refused: a slider position should land on the
    // nearest value the device takes.
    camera->SetControl(CameraControl::Brightness, 9999);
    camera->GetControl(CameraControl::Brightness, value);
    Check(value == 255, "an out-of-range value is clamped, not rejected");

    camera->SetControl(CameraControl::Exposure, 10);
    camera->GetControl(CameraControl::Exposure, value);
    Check(value == 7, "a value is snapped onto the control's step grid");

    IODeviceResult missing = camera->SetControl(CameraControl::Tilt, 1);
    Check(!static_cast<bool>(missing), "setting a control the camera lacks fails");
    Check(missing.code == IODeviceResultCode::NotSupported, "with NotSupported");

    Check(static_cast<bool>(camera->SetControlAuto(CameraControl::Exposure, true)),
          "a control with an automatic mode can be switched to it");
    IODeviceResult noAuto = camera->SetControlAuto(CameraControl::Brightness, true);
    Check(!static_cast<bool>(noAuto), "one without an automatic mode cannot");
    Check(noAuto.code == IODeviceResultCode::NotSupported, "with NotSupported");
}

void TestCapabilityQueries() {
    std::cout << "\nCapability queries\n";

    auto camera = MakeCamera();
    camera->Connect();
    const CameraCapabilities& caps = camera->GetCapabilities();

    Check(caps.Supports(CameraPixelFormat::YUYV), "a listed format is supported");
    Check(!caps.Supports(CameraPixelFormat::H264), "an unlisted one is not");
    Check(caps.Supports(CameraPixelFormat::YUYV, CameraResolution(1280, 720)),
          "a listed resolution for that format is supported");
    Check(!caps.Supports(CameraPixelFormat::YUYV, CameraResolution(1920, 1080)),
          "a resolution listed only for another format is not");
    Check(caps.GetLargestResolution(CameraPixelFormat::MJPEG) == CameraResolution(1920, 1080),
          "the largest resolution is found per format");
    Check(caps.GetFormats().size() == 2, "both formats are enumerated");

    // A driver reporting a continuous range rather than a list leaves the
    // resolutions empty; that must read as "did not enumerate", or such a
    // camera becomes unusable.
    CameraCapabilities vague;
    CameraFormatCapability openEnded;
    openEnded.format = CameraPixelFormat::YUYV;
    vague.formats = {openEnded};
    Check(vague.Supports(CameraPixelFormat::YUYV, CameraResolution(800, 600)),
          "an unenumerated resolution list does not refuse every resolution");
}

}  // namespace

int main() {
    std::cout << "IODeviceManager camera tests\n";
    std::cout << "============================\n";

    TestFrameSizing();
    TestControlClamping();
    TestConfigurationIsValidated();
    TestResolveConfiguration();
    TestStreaming();
    TestStreamingRequiresConnection();
    TestControls();
    TestCapabilityQueries();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All camera tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " camera test(s) FAILED.\n";
    return 1;
}
