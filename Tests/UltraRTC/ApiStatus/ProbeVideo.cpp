// Tests/UltraRTC/ApiStatus/ProbeVideo.cpp
// UltraRtcApiStatus: video - the VP8 track, pictures in and decoded
// pictures out, key-frame requests, and the camera.
// Version: 0.1.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraRtcApiStatus.h"

#include <atomic>
#include <memory>

using namespace ultrartc_apistatus;

namespace {

bool Near(int a, int b) {
    return a - b < 24 && b - a < 24;    // VP8 is lossy
}

std::string Colour(const uint8_t* rgb) {
    return std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," + std::to_string(rgb[2]);
}

} // namespace

ULTRARTC_PROBE("Video", UltraRtc_AddVideoTrack) {
    REQUIRE_VIDEO();
    Pair& pair = VideoPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(pair.a.track != UltraRtcInvalidHandle && pair.b.track != UltraRtcInvalidHandle);
    UltraRtcSdpType type = UltraRtcSdpType::Unspecified;
    std::string sdp;
    PROBE_EXPECT(UltraRtc_GetLocalDescription(pair.a.peer, type, sdp));
    PROBE_EXPECT_MSG(sdp.find("VP8/90000") != std::string::npos, "the offer does not name VP8");
    UltraRtcVideoConfig tiny;
    tiny.width = 4;
    UltraRtcHandle refused = 7;
    const UltraRtcResult r = UltraRtc_AddVideoTrack(pair.a.peer, "tiny", UltraRtcDirection::SendRecv, tiny, refused);
    PROBE_EXPECT_MSG(!r && refused == UltraRtcInvalidHandle, "a 4 pixel wide track was accepted");
    PROBE_EXPECT(UltraRtc_AddVideoTrack(UltraRtcInvalidHandle, "video", UltraRtcDirection::SendRecv,
                                        UltraRtcVideoConfig(), refused).code == UltraRtcResultCode::InvalidHandle);
    return Working("a track added on both sides with one mid negotiated as VP8 and opened; a size no encoder "
                   "takes is refused");
}

ULTRARTC_PROBE("Video", UltraRtc_SendVideoFrame) {
    REQUIRE_VIDEO();
    Pair& pair = VideoPair();
    REQUIRE_PAIR(pair);
    const PictureResult sent = SendPictures(20, 240, 140, 20);      // orange
    PROBE_EXPECT_MSG(sent.count >= 10, "20 pictures arrived as " + std::to_string(sent.count));
    UltraRtcVideoFrame cut;
    cut.width = 10;
    cut.height = 10;
    cut.rgba.resize(10);        // far fewer bytes than 10 x 10 pixels
    PROBE_EXPECT_MSG(!UltraRtc_SendVideoFrame(pair.a.track, cut), "a picture with too few bytes was accepted");
    PROBE_EXPECT(UltraRtc_SendVideoFrame(UltraRtcInvalidHandle, cut).code == UltraRtcResultCode::InvalidHandle);
    return Working(std::to_string(sent.count) + " of 20 pictures sent at 25 a second reached the other side; a "
                   "picture with too few bytes is refused");
}

ULTRARTC_PROBE("Video", UltraRtc_SetVideoFrameCallback) {
    REQUIRE_VIDEO();
    Pair& pair = VideoPair();
    REQUIRE_PAIR(pair);
    const PictureResult blue = SendPictures(15, 30, 90, 220);
    PROBE_EXPECT_MSG(blue.count >= 8, "the callback got only " + std::to_string(blue.count) + " pictures");
    PROBE_EXPECT_MSG(blue.width == 320 && blue.height == 240,
                     "pictures arrived at " + std::to_string(blue.width) + " x " + std::to_string(blue.height) +
                         ", not the track's 320 x 240");
    PROBE_EXPECT_MSG(Near(blue.centre[0], 30) && Near(blue.centre[1], 90) && Near(blue.centre[2], 220),
                     "pictures of 30,90,220 arrived as " + Colour(blue.centre));
    PROBE_EXPECT(UltraRtc_SetVideoFrameCallback(UltraRtcInvalidHandle, nullptr).code ==
                 UltraRtcResultCode::InvalidHandle);
    return Working("decoded RGBA pictures at the track's 320 x 240: 30,90,220 sent at 400 x 300 arrived as " +
                   Colour(blue.centre));
}

ULTRARTC_PROBE("Video", UltraRtc_RequestKeyFrame) {
    REQUIRE_VIDEO();
    Pair& pair = VideoPair();
    REQUIRE_PAIR(pair);
    const UltraRtcResult asked = UltraRtc_RequestKeyFrame(pair.b.track);
    PROBE_EXPECT_MSG(asked, asked.message);
    PROBE_EXPECT(UltraRtc_RequestKeyFrame(UltraRtcInvalidHandle).code == UltraRtcResultCode::InvalidHandle);
    // The stream must survive the request: pictures go on arriving.
    const PictureResult after = SendPictures(10, 20, 200, 60);
    PROBE_EXPECT_MSG(after.count >= 5, "pictures stopped arriving after a key frame was asked for");
    return Implemented("the request is accepted and pictures go on arriving; that the sender answered with a key "
                       "frame cannot be seen through the API");
}

ULTRARTC_PROBE("Video", UltraRtc_StartCamera) {
    REQUIRE_VIDEO();
    Pair& pair = VideoPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_StartCamera(UltraRtcInvalidHandle).code == UltraRtcResultCode::InvalidHandle);
    auto previews = std::make_shared<std::atomic<int>>(0);
    const UltraRtcResult started =
        UltraRtc_StartCamera(pair.a.track, "", [previews](const UltraRtcVideoFrame&) { ++*previews; });
    if (!started) return Implemented("no camera could be opened on this machine: " + started.message);
    const bool seen = WaitUntil([&]() { return previews->load() >= 3; }, 4000);
    UltraRtc_StopCamera(pair.a.track);
    if (!seen) return Implemented("a camera opened but delivered no picture in 4 s");
    return Working("the default camera delivered pictures to the track and to the self view");
}

ULTRARTC_PROBE("Video", UltraRtc_StopCamera) {
    REQUIRE_VIDEO();
    Pair& pair = VideoPair();
    REQUIRE_PAIR(pair);
    PROBE_EXPECT(UltraRtc_StopCamera(UltraRtcInvalidHandle).code == UltraRtcResultCode::InvalidHandle);
    PROBE_EXPECT_MSG(UltraRtc_StopCamera(pair.a.track), "stopping a camera that was never started failed");
    auto previews = std::make_shared<std::atomic<int>>(0);
    const UltraRtcResult started =
        UltraRtc_StartCamera(pair.a.track, "", [previews](const UltraRtcVideoFrame&) { ++*previews; });
    if (!started) return Implemented("no camera on this machine to stop; with none started it answers Ok");
    const bool seen = WaitUntil([&]() { return previews->load() >= 3; }, 4000);
    const UltraRtcResult stopped = UltraRtc_StopCamera(pair.a.track);
    PROBE_EXPECT_MSG(stopped, stopped.message);
    if (!seen) return Implemented("a camera opened and closed, but it delivered no picture to stop");
    const int atStop = previews->load();
    const bool more = WaitUntil([&]() { return previews->load() > atStop; }, 800);
    PROBE_EXPECT_MSG(!more, "the self view still got pictures after the camera was stopped");
    return Working("the camera's pictures stopped, for the track and for the self view");
}
