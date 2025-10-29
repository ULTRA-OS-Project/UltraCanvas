// OS/WASM/UltraCanvasWASMSupport.cpp
// WebAssembly platform support utilities implementation
// Version: 1.0.0
// Last Modified: 2025-01-27
// Author: UltraCanvas Framework

#include "UltraCanvasWASMSupport.h"
#include <emscripten.h>
#include <emscripten/fetch.h>
#include <iostream>
#include <sstream>

namespace UltraCanvas {

// ===== FILE SYSTEM SUPPORT =====

bool WASMFileSystem::MountFileSystem(const std::string& mountPoint) {
    return EM_ASM_INT({
        try {
            const path = UTF8ToString($0);
            
            // Create mount point
            if (!FS.analyzePath(path).exists) {
                FS.mkdir(path);
            }
            
            // Mount IDBFS
            FS.mount(IDBFS, {}, path);
            
            // Sync from IndexedDB
            FS.syncfs(true, function(err) {
                if (err) {
                    console.error('IDBFS mount error:', err);
                } else {
                    console.log('IDBFS mounted at:', path);
                }
            });
            
            return 1;
        } catch (e) {
            console.error('Failed to mount IDBFS:', e);
            return 0;
        }
    }, mountPoint.c_str());
}

bool WASMFileSystem::SyncFromBrowser() {
    return EM_ASM_INT({
        return new Promise((resolve) => {
            FS.syncfs(true, function(err) {
                resolve(err ? 0 : 1);
            });
        });
    });
}

bool WASMFileSystem::SyncToBrowser() {
    return EM_ASM_INT({
        return new Promise((resolve) => {
            FS.syncfs(false, function(err) {
                resolve(err ? 0 : 1);
            });
        });
    });
}

bool WASMFileSystem::FileExists(const std::string& path) {
    return EM_ASM_INT({
        try {
            const analysis = FS.analyzePath(UTF8ToString($0));
            return analysis.exists && !analysis.object.isFolder ? 1 : 0;
        } catch (e) {
            return 0;
        }
    }, path.c_str());
}

std::vector<uint8_t> WASMFileSystem::ReadFile(const std::string& path) {
    std::vector<uint8_t> data;
    
    // This is a simplified version - in real implementation,
    // you'd need to properly transfer data from JS to C++
    std::cerr << "[WASM FileSystem] ReadFile not fully implemented yet" << std::endl;
    
    return data;
}

bool WASMFileSystem::WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    // Simplified version
    std::cerr << "[WASM FileSystem] WriteFile not fully implemented yet" << std::endl;
    return false;
}

bool WASMFileSystem::DeleteFile(const std::string& path) {
    return EM_ASM_INT({
        try {
            FS.unlink(UTF8ToString($0));
            return 1;
        } catch (e) {
            console.error('Failed to delete file:', e);
            return 0;
        }
    }, path.c_str());
}

bool WASMFileSystem::CreateDirectory(const std::string& path) {
    return EM_ASM_INT({
        try {
            FS.mkdir(UTF8ToString($0));
            return 1;
        } catch (e) {
            console.error('Failed to create directory:', e);
            return 0;
        }
    }, path.c_str());
}

std::vector<std::string> WASMFileSystem::ListDirectory(const std::string& path) {
    std::vector<std::string> files;
    
    // Simplified version
    std::cerr << "[WASM FileSystem] ListDirectory not fully implemented yet" << std::endl;
    
    return files;
}

// ===== NETWORK SUPPORT =====

bool WASMNetwork::FetchURL(const std::string& url, std::vector<uint8_t>& data) {
    // Synchronous fetch (blocks)
    std::cerr << "[WASM Network] Synchronous fetch not recommended in WASM" << std::endl;
    return false;
}

bool WASMNetwork::FetchText(const std::string& url, std::string& text) {
    // Synchronous fetch (blocks)
    std::cerr << "[WASM Network] Synchronous fetch not recommended in WASM" << std::endl;
    return false;
}

void WASMNetwork::FetchAsync(const std::string& url, FetchCallback callback) {
    // Use Emscripten fetch API
    std::cerr << "[WASM Network] FetchAsync not fully implemented yet" << std::endl;
}

// ===== BROWSER INTEGRATION =====

void WASMBrowser::DownloadFile(const std::string& filename, 
                              const std::vector<uint8_t>& data, 
                              const std::string& mimeType) {
    // Create blob and trigger download
    EM_ASM({
        const filename = UTF8ToString($0);
        const mimeType = UTF8ToString($1);
        
        // In real implementation, would transfer data from C++ to JS
        const blob = new Blob([], { type: mimeType });
        const url = URL.createObjectURL(blob);
        
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }, filename.c_str(), mimeType.c_str());
}

void WASMBrowser::Alert(const std::string& message) {
    EM_ASM({
        alert(UTF8ToString($0));
    }, message.c_str());
}

bool WASMBrowser::Confirm(const std::string& message) {
    return EM_ASM_INT({
        return confirm(UTF8ToString($0)) ? 1 : 0;
    }, message.c_str());
}

std::string WASMBrowser::Prompt(const std::string& message, const std::string& defaultValue) {
    char* result = (char*)EM_ASM_PTR({
        const result = prompt(UTF8ToString($0), UTF8ToString($1));
        if (result === null) return 0;
        const len = lengthBytesUTF8(result) + 1;
        const ptr = _malloc(len);
        stringToUTF8(result, ptr, len);
        return ptr;
    }, message.c_str(), defaultValue.c_str());
    
    if (result) {
        std::string str(result);
        free(result);
        return str;
    }
    return "";
}

void WASMBrowser::ConsoleLog(const std::string& message) {
    EM_ASM({
        console.log(UTF8ToString($0));
    }, message.c_str());
}

void WASMBrowser::ConsoleWarn(const std::string& message) {
    EM_ASM({
        console.warn(UTF8ToString($0));
    }, message.c_str());
}

void WASMBrowser::ConsoleError(const std::string& message) {
    EM_ASM({
        console.error(UTF8ToString($0));
    }, message.c_str());
}

std::string WASMBrowser::GetUserAgent() {
    char* ua = (char*)EM_ASM_PTR({
        const ua = navigator.userAgent;
        const len = lengthBytesUTF8(ua) + 1;
        const ptr = _malloc(len);
        stringToUTF8(ua, ptr, len);
        return ptr;
    });
    
    if (ua) {
        std::string result(ua);
        free(ua);
        return result;
    }
    return "";
}

std::string WASMBrowser::GetPlatform() {
    char* platform = (char*)EM_ASM_PTR({
        const p = navigator.platform;
        const len = lengthBytesUTF8(p) + 1;
        const ptr = _malloc(len);
        stringToUTF8(p, ptr, len);
        return ptr;
    });
    
    if (platform) {
        std::string result(platform);
        free(platform);
        return result;
    }
    return "";
}

void WASMBrowser::GetScreenSize(int& width, int& height) {
    width = EM_ASM_INT({
        return screen.width;
    });
    
    height = EM_ASM_INT({
        return screen.height;
    });
}

void WASMBrowser::SetLocalStorage(const std::string& key, const std::string& value) {
    EM_ASM({
        try {
            localStorage.setItem(UTF8ToString($0), UTF8ToString($1));
        } catch (e) {
            console.error('LocalStorage error:', e);
        }
    }, key.c_str(), value.c_str());
}

std::string WASMBrowser::GetLocalStorage(const std::string& key) {
    char* value = (char*)EM_ASM_PTR({
        try {
            const v = localStorage.getItem(UTF8ToString($0));
            if (v === null) return 0;
            const len = lengthBytesUTF8(v) + 1;
            const ptr = _malloc(len);
            stringToUTF8(v, ptr, len);
            return ptr;
        } catch (e) {
            console.error('LocalStorage error:', e);
            return 0;
        }
    }, key.c_str());
    
    if (value) {
        std::string result(value);
        free(value);
        return result;
    }
    return "";
}

void WASMBrowser::RemoveLocalStorage(const std::string& key) {
    EM_ASM({
        try {
            localStorage.removeItem(UTF8ToString($0));
        } catch (e) {
            console.error('LocalStorage error:', e);
        }
    }, key.c_str());
}

void WASMBrowser::ClearLocalStorage() {
    EM_ASM({
        try {
            localStorage.clear();
        } catch (e) {
            console.error('LocalStorage error:', e);
        }
    });
}

// ===== RESOURCE LOADING =====

void WASMResourceLoader::LoadImage(const std::string& url, ImageLoadCallback callback) {
    std::cerr << "[WASM ResourceLoader] LoadImage not fully implemented yet" << std::endl;
}

void WASMResourceLoader::LoadFont(const std::string& fontFamily, 
                                 const std::string& url, 
                                 FontLoadCallback callback) {
    EM_ASM({
        const family = UTF8ToString($0);
        const fontUrl = UTF8ToString($1);
        
        const font = new FontFace(family, `url(${fontUrl})`);
        
        font.load().then(function(loadedFont) {
            document.fonts.add(loadedFont);
            console.log('Font loaded:', family);
            // Callback would be invoked here
        }).catch(function(error) {
            console.error('Font load error:', error);
        });
    }, fontFamily.c_str(), url.c_str());
}

void WASMResourceLoader::PreloadAsset(const std::string& url) {
    EM_ASM({
        const link = document.createElement('link');
        link.rel = 'preload';
        link.href = UTF8ToString($0);
        link.as = 'fetch';
        document.head.appendChild(link);
    }, url.c_str());
}

// ===== TIME & PERFORMANCE =====

double WASMTime::GetTime() {
    return emscripten_get_now() / 1000.0;
}

void WASMTime::PerformanceMark(const std::string& name) {
    EM_ASM({
        if (performance && performance.mark) {
            performance.mark(UTF8ToString($0));
        }
    }, name.c_str());
}

double WASMTime::PerformanceMeasure(const std::string& name, 
                                   const std::string& startMark, 
                                   const std::string& endMark) {
    return EM_ASM_DOUBLE({
        if (performance && performance.measure) {
            try {
                performance.measure(UTF8ToString($0), UTF8ToString($1), UTF8ToString($2));
                const entries = performance.getEntriesByName(UTF8ToString($0));
                if (entries.length > 0) {
                    return entries[entries.length - 1].duration;
                }
            } catch (e) {
                console.error('Performance measure error:', e);
            }
        }
        return 0;
    }, name.c_str(), startMark.c_str(), endMark.c_str());
}

// ===== URL & QUERY PARAMETERS =====

std::string WASMURL::GetCurrentURL() {
    char* url = (char*)EM_ASM_PTR({
        const url = window.location.href;
        const len = lengthBytesUTF8(url) + 1;
        const ptr = _malloc(len);
        stringToUTF8(url, ptr, len);
        return ptr;
    });
    
    if (url) {
        std::string result(url);
        free(url);
        return result;
    }
    return "";
}

std::string WASMURL::GetQueryParameter(const std::string& name) {
    char* value = (char*)EM_ASM_PTR({
        const params = new URLSearchParams(window.location.search);
        const value = params.get(UTF8ToString($0));
        if (value === null) return 0;
        const len = lengthBytesUTF8(value) + 1;
        const ptr = _malloc(len);
        stringToUTF8(value, ptr, len);
        return ptr;
    }, name.c_str());
    
    if (value) {
        std::string result(value);
        free(value);
        return result;
    }
    return "";
}

std::vector<std::pair<std::string, std::string>> WASMURL::GetAllQueryParameters() {
    std::vector<std::pair<std::string, std::string>> params;
    
    // Simplified version
    std::cerr << "[WASM URL] GetAllQueryParameters not fully implemented yet" << std::endl;
    
    return params;
}

std::string WASMURL::GetHash() {
    char* hash = (char*)EM_ASM_PTR({
        const h = window.location.hash;
        const len = lengthBytesUTF8(h) + 1;
        const ptr = _malloc(len);
        stringToUTF8(h, ptr, len);
        return ptr;
    });
    
    if (hash) {
        std::string result(hash);
        free(hash);
        return result;
    }
    return "";
}

void WASMURL::Navigate(const std::string& url) {
    EM_ASM({
        window.location.href = UTF8ToString($0);
    }, url.c_str());
}

void WASMURL::Reload() {
    EM_ASM({
        window.location.reload();
    });
}

} // namespace UltraCanvas
