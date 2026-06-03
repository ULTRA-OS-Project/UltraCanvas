// OS/MacOS/UltraCanvasMacOSFileLoader.mm
// macOS implementation of UltraCanvasFileLoader::NotifyRecentFile.
// Registers the file with NSDocumentController so it appears in
// Apple menu > Recent Items > Documents and the app's Dock menu.
// Version: 1.0.0
// Last Modified: 2026-05-12
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

namespace UltraCanvas {

    void UltraCanvasFileLoader::NotifyRecentFile(const std::string& filePath) {
        if (filePath.empty()) return;

        @autoreleasepool {
            NSString* nsPath = [NSString stringWithUTF8String:filePath.c_str()];
            if (!nsPath) return;

            NSURL* url = [NSURL fileURLWithPath:nsPath];
            if (!url) return;

            [[NSDocumentController sharedDocumentController]
                noteNewRecentDocumentURL:url];
        }
    }

} // namespace UltraCanvas

#endif // __APPLE__
