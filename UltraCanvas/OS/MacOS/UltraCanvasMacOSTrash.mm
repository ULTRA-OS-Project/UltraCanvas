// OS/MacOS/UltraCanvasMacOSTrash.mm
// MoveToTrash on macOS: -[NSFileManager trashItemAtURL:resultingItemURL:error:],
// which picks the right Trash for the volume (~/.Trash, or the volume's own
// .Trashes/<uid>) and records the origin, so Finder's "Put Back" works - a
// plain move into ~/.Trash would lose both.
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include "UltraCanvasTrash.h"

#import <Foundation/Foundation.h>

#include <string>

namespace UltraCanvas {

    bool NativeMoveToTrash(const std::string& path, std::string& error) {
        @autoreleasepool {
            NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
            if (!nsPath) {
                error = "the file name is not valid UTF-8";
                return false;
            }
            NSURL* url = [NSURL fileURLWithPath:nsPath];
            NSError* nsError = nil;
            if (![[NSFileManager defaultManager] trashItemAtURL:url
                                               resultingItemURL:nil
                                                          error:&nsError]) {
                const char* text = nsError
                        ? [[nsError localizedDescription] UTF8String] : nullptr;
                error = text ? text : "the Finder refused";
                return false;
            }
            return true;
        }
    }

} // namespace UltraCanvas
