// OS/MacOS/UltraCanvasMacOSGeolocation.mm
// UltraCanvasGeolocation on macOS: CoreLocation.
//
// A CLLocationManager is made on the main thread (CoreLocation delivers to
// the run loop of the thread that made it, which here is the UI's), asks for
// "when in use" permission while the person has not decided, and
// requestLocation answers once through the delegate: the first location,
// or the error. The program's Info.plist must carry
// NSLocationWhenInUseUsageDescription, or macOS refuses without asking
// (package-macos.sh writes it for the apps that ask for a location).
// Version: 1.0.0
// Last Modified: 2026-11-23
// Author: UltraCanvas Framework

#include "UltraCanvasGeolocation.h"

#if defined(__APPLE__) && defined(ULTRACANVAS_GEOLOCATION_NATIVE)

#if __has_include(<CoreLocation/CoreLocation.h>)
#import <CoreLocation/CoreLocation.h>
#import <Foundation/Foundation.h>
#define UC_HAVE_CORELOCATION 1
#endif

#include <functional>
#include <string>

#ifdef UC_HAVE_CORELOCATION

// One request: the manager, the answer, and itself kept alive until it has
// answered (the set below holds it)
@interface UltraCanvasLocationRequest : NSObject <CLLocationManagerDelegate>
@property (nonatomic, strong) CLLocationManager* manager;
- (instancetype)initWithAnswer:(std::function<void(const UltraCanvas::GeoPositionResult&)>)answer;
- (void)start;
@end

static NSMutableSet* g_requests = nil;

@implementation UltraCanvasLocationRequest {
    std::function<void(const UltraCanvas::GeoPositionResult&)> _answer;
    BOOL _done;
    BOOL _asked;
}

- (instancetype)initWithAnswer:(std::function<void(const UltraCanvas::GeoPositionResult&)>)answer {
    if ((self = [super init])) {
        _answer = std::move(answer);
        _done = NO;
        _asked = NO;
    }
    return self;
}

- (void)finish:(const UltraCanvas::GeoPositionResult&)result {
    if (_done) return;
    _done = YES;
    self.manager.delegate = nil;
    if (_answer) _answer(result);
    _answer = nullptr;
    [g_requests removeObject:self];
}

- (void)fail:(NSString*)why {
    UltraCanvas::GeoPositionResult r;
    r.error = why ? std::string([why UTF8String]) : std::string("No location.");
    [self finish:r];
}

- (void)start {
    if (![CLLocationManager locationServicesEnabled]) {
        [self fail:@"Location Services are turned off (System Settings > Privacy & Security)."];
        return;
    }
    self.manager = [[CLLocationManager alloc] init];
    self.manager.delegate = self;
    self.manager.desiredAccuracy = kCLLocationAccuracyBest;
    [self proceed:self.manager.authorizationStatus];
}

- (void)proceed:(CLAuthorizationStatus)status {
    if (_done) return;
    switch (status) {
        case kCLAuthorizationStatusNotDetermined:
            if (!_asked) {
                _asked = YES;
                [self.manager requestWhenInUseAuthorization];   // the answer comes to the delegate
            }
            return;
        case kCLAuthorizationStatusRestricted:
        case kCLAuthorizationStatusDenied:
            [self fail:@"This program is not allowed to use Location Services (System Settings > Privacy & "
                        @"Security > Location Services)."];
            return;
        default:
            [self.manager requestLocation];
            return;
    }
}

- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager {
    [self proceed:manager.authorizationStatus];
}

- (void)locationManager:(CLLocationManager*)manager didUpdateLocations:(NSArray<CLLocation*>*)locations {
    (void)manager;
    CLLocation* last = locations.lastObject;
    if (!last) return;
    UltraCanvas::GeoPositionResult r;
    r.ok = true;
    r.latitude = last.coordinate.latitude;
    r.longitude = last.coordinate.longitude;
    r.accuracyMeters = last.horizontalAccuracy >= 0 ? last.horizontalAccuracy : -1.0;
    [self finish:r];
}

- (void)locationManager:(CLLocationManager*)manager didFailWithError:(NSError*)error {
    (void)manager;
    if (error.code == kCLErrorLocationUnknown) return;   // still looking: CoreLocation tries again
    if (error.code == kCLErrorDenied) {
        [self fail:@"This program is not allowed to use Location Services."];
        return;
    }
    [self fail:error.localizedDescription];
}

@end

#endif // UC_HAVE_CORELOCATION

namespace UltraCanvas {
namespace GeolocationBackend {

bool Available() {
#ifdef UC_HAVE_CORELOCATION
    return true;
#else
    return false;
#endif
}

std::string Name() {
    return "CoreLocation";
}

void Request(int timeoutMs, std::function<void(const GeoPositionResult&)> answer) {
    (void)timeoutMs;   // the core keeps the time
#ifdef UC_HAVE_CORELOCATION
    // CoreLocation answers on the run loop of the thread that made the
    // manager: the main one
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!g_requests) g_requests = [[NSMutableSet alloc] init];
        UltraCanvasLocationRequest* request = [[UltraCanvasLocationRequest alloc] initWithAnswer:answer];
        [g_requests addObject:request];
        [request start];
    });
#else
    GeoPositionResult r;
    r.error = "This program was built without CoreLocation.";
    answer(r);
#endif
}

} // namespace GeolocationBackend
} // namespace UltraCanvas

#endif
