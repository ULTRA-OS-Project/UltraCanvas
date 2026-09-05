// OS/MacOS/UltraCanvasMacOSAlias.mm
// Resolving a Finder alias, which is the one part of UltraCanvasMacBundle.h
// that cannot be done by reading the file. An alias holds bookmark data: a
// set of hints — volume, file id, path, creation date — that lets the system
// find the target again after it has been moved or renamed, which is exactly
// what makes an alias different from a symlink. Only macOS can follow them,
// so this is the only implementation and everywhere else reports false.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#import <Foundation/Foundation.h>

#include "UltraCanvasMacBundle.h"

namespace UltraCanvas {

    bool ResolveFinderAlias(const std::string& path, std::string& outTarget) {
        if (path.empty()) return false;
        @autoreleasepool {
            NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
            if (!nsPath) return false;
            NSURL* url = [NSURL fileURLWithPath:nsPath];
            if (!url) return false;

            NSError* error = nil;
            NSData* bookmark = [NSURL bookmarkDataWithContentsOfURL:url
                                                              error:&error];
            if (!bookmark) return false;

            BOOL stale = NO;
            // Resolving without mounting and without user interface: a file
            // display is drawing a folder listing, and must not put a volume
            // password dialog on screen to do it.
            NSURL* resolved = [NSURL
                    URLByResolvingBookmarkData:bookmark
                                       options:NSURLBookmarkResolutionWithoutUI |
                                               NSURLBookmarkResolutionWithoutMounting
                                 relativeToURL:nil
                           bookmarkDataIsStale:&stale
                                         error:&error];
            if (!resolved || ![resolved isFileURL]) return false;
            const char* resolvedPath = [[resolved path] UTF8String];
            if (!resolvedPath || !*resolvedPath) return false;
            outTarget = resolvedPath;
            return true;
        }
    }

} // namespace UltraCanvas
