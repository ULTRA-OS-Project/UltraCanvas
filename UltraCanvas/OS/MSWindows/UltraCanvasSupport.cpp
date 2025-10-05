// OS/MSWindows/UltraCanvasSupport.cpp
// Windows platform implementation - Win32/Direct2D rendering
// Version: 1.0.0
// Last Modified: 2025-07-01
// Author: UltraCanvas Framework

#ifdef _WIN32

#include "UltraCanvasSupport.h"
#include <iostream>
#include <sstream>
#include <codecvt>
#include <locale>
#include <algorithm>

namespace UltraCanvas {

// ===== GLOBAL VARIABLES =====
WindowsRenderContext* g_windowsRenderContext = nullptr;
std::vector<WindowsWindowData*> g_windowsList;
HINSTANCE g_applicationInstance = nullptr;

// Static window class name
static const wchar_t* ULTRACANVAS_WINDOW_CLASS = L"UltraCanvasWindow";
static bool g_windowClassRegistered = false;

// ===== WINDOW PROCEDURE =====
LRESULT CALLBACK UltraCanvasWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WindowsWindowData* windowData = nullptr;
    
    // Find window data
    for (auto* data : g_windowsList) {
        if (data->hwnd == hwnd) {
            windowData = data;
            break;
        }
    }
    
    switch (message) {
        case WM_CREATE:
            break;
            
        case WM_SIZE:
            if (windowData && windowData->renderContext) {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                windowData->width = width;
                windowData->height = height;
                ResizeRenderTarget(hwnd, width, height);
            }
            break;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (windowData && windowData->renderContext && windowData->renderContext->renderTarget) {
                BeginDrawingWindows();
                // Application-specific painting would be called here
                EndDrawingWindows();
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_DESTROY:
            if (windowData) {
                windowData->isActive = false;
                PostQuitMessage(0);
            }
            return 0;
            
        default:
            // Convert to UCEvent and post to application
            UCEvent event = ConvertWindowsMessage(hwnd, message, wParam, lParam);
            if (event.type != UCEventType::Unknown) {
                PostUltraCanvasEvent(event);
            }
            break;
    }
    
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ===== PLATFORM INITIALIZATION =====
HRESULT InitUltraCanvasPlatform() {
    HRESULT hr = S_OK;
    
    try {
        // Get application instance
        g_applicationInstance = GetModuleHandle(nullptr);
        
        // Register window class if not already done
        if (!g_windowClassRegistered) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEXW);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = UltraCanvasWindowProc;
            wcex.hInstance = g_applicationInstance;
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wcex.lpszClassName = ULTRACANVAS_WINDOW_CLASS;
            
            if (!RegisterClassExW(&wcex)) {
                hr = HRESULT_FROM_WIN32(GetLastError());
                LogWindowsError("RegisterClassExW");
                return hr;
            }
            g_windowClassRegistered = true;
        }
        
        // Initialize COM
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(hr)) {
            LogWindowsError("CoInitializeEx");
            return hr;
        }
        
        // Create global render context
        g_windowsRenderContext = new WindowsRenderContext();
        
        // Create Direct2D factory
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_windowsRenderContext->d2dFactory);
        if (FAILED(hr)) {
            LogWindowsError("D2D1CreateFactory");
            return hr;
        }
        
        // Create DirectWrite factory
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), 
                                reinterpret_cast<IUnknown**>(&g_windowsRenderContext->writeFactory));
        if (FAILED(hr)) {
            LogWindowsError("DWriteCreateFactory");
            return hr;
        }
        
        // Create WIC factory
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&g_windowsRenderContext->wicFactory));
        if (FAILED(hr)) {
            LogWindowsError("CoCreateInstance WICImagingFactory");
            return hr;
        }
        
        std::cout << "UltraCanvas Windows platform initialized successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in InitUltraCanvasPlatform: " << e.what() << std::endl;
        hr = E_FAIL;
    }
    
    return hr;
}

void ShutdownUltraCanvasPlatform() {
    try {
        // Cleanup all windows
        for (auto* windowData : g_windowsList) {
            if (windowData) {
                if (windowData->renderContext) {
                    ReleaseRenderTarget(windowData->renderContext);
                    delete windowData->renderContext;
                }
                if (windowData->hwnd) {
                    DestroyWindow(windowData->hwnd);
                }
                delete windowData;
            }
        }
        g_windowsList.clear();
        
        // Cleanup global render context
        if (g_windowsRenderContext) {
            if (g_windowsRenderContext->currentBrush) {
                g_windowsRenderContext->currentBrush->Release();
            }
            if (g_windowsRenderContext->currentTextFormat) {
                g_windowsRenderContext->currentTextFormat->Release();
            }
            if (g_windowsRenderContext->wicFactory) {
                g_windowsRenderContext->wicFactory->Release();
            }
            if (g_windowsRenderContext->writeFactory) {
                g_windowsRenderContext->writeFactory->Release();
            }
            if (g_windowsRenderContext->d2dFactory) {
                g_windowsRenderContext->d2dFactory->Release();
            }
            delete g_windowsRenderContext;
            g_windowsRenderContext = nullptr;
        }
        
        // Uninitialize COM
        CoUninitialize();
        
        std::cout << "UltraCanvas Windows platform shut down successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in ShutdownUltraCanvasPlatform: " << e.what() << std::endl;
    }
}

bool InitUltraCanvasWindows(void* context) {
    HRESULT hr = InitUltraCanvasPlatform();
    return SUCCEEDED(hr);
}

// ===== WINDOW MANAGEMENT =====
HWND CreateUltraCanvasWindow(const char* title, int x, int y, int width, int height) {
    if (!g_windowsRenderContext) {
        std::cerr << "UltraCanvas platform not initialized" << std::endl;
        return nullptr;
    }
    
    std::wstring wTitle = StringToWString(title);
    
    HWND hwnd = CreateWindowExW(
        0,                              // Extended window style
        ULTRACANVAS_WINDOW_CLASS,       // Window class name
        wTitle.c_str(),                 // Window title
        WS_OVERLAPPEDWINDOW,            // Window style
        x, y, width, height,            // Position and size
        nullptr,                        // Parent window
        nullptr,                        // Menu
        g_applicationInstance,          // Instance handle
        nullptr                         // Additional application data
    );
    
    if (!hwnd) {
        LogWindowsError("CreateWindowExW");
        return nullptr;
    }
    
    // Create window data
    WindowsWindowData* windowData = new WindowsWindowData();
    windowData->hwnd = hwnd;
    windowData->width = width;
    windowData->height = height;
    windowData->title = title;
    windowData->renderContext = new WindowsRenderContext(*g_windowsRenderContext);
    
    // Create render target for this window
    HRESULT hr = CreateRenderTarget(hwnd, windowData->renderContext);
    if (FAILED(hr)) {
        delete windowData->renderContext;
        delete windowData;
        DestroyWindow(hwnd);
        return nullptr;
    }
    
    g_windowsList.push_back(windowData);
    
    return hwnd;
}

void DestroyUltraCanvasWindow(HWND hwnd) {
    auto it = std::find_if(g_windowsList.begin(), g_windowsList.end(),
                          [hwnd](WindowsWindowData* data) { return data->hwnd == hwnd; });
    
    if (it != g_windowsList.end()) {
        WindowsWindowData* windowData = *it;
        
        if (windowData->renderContext) {
            ReleaseRenderTarget(windowData->renderContext);
            delete windowData->renderContext;
        }
        
        delete windowData;
        g_windowsList.erase(it);
    }
    
    DestroyWindow(hwnd);
}

void ShowUltraCanvasWindow(HWND hwnd, bool show) {
    ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
    
    auto it = std::find_if(g_windowsList.begin(), g_windowsList.end(),
                          [hwnd](WindowsWindowData* data) { return data->hwnd == hwnd; });
    
    if (it != g_windowsList.end()) {
        (*it)->isVisible = show;
    }
}

void UpdateUltraCanvasWindow(HWND hwnd) {
    UpdateWindow(hwnd);
}

void SetWindowTitle(HWND hwnd, const char* title) {
    std::wstring wTitle = StringToWString(title);
    SetWindowTextW(hwnd, wTitle.c_str());
    
    auto it = std::find_if(g_windowsList.begin(), g_windowsList.end(),
                          [hwnd](WindowsWindowData* data) { return data->hwnd == hwnd; });
    
    if (it != g_windowsList.end()) {
        (*it)->title = title;
    }
}

// ===== RENDER TARGET MANAGEMENT =====
HRESULT CreateRenderTarget(HWND hwnd, WindowsRenderContext* context) {
    if (!context || !context->d2dFactory) {
        return E_INVALIDARG;
    }
    
    RECT rect;
    GetClientRect(hwnd, &rect);
    
    D2D1_SIZE_U size = D2D1::SizeU(rect.right - rect.left, rect.bottom - rect.top);
    
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndRtProps = D2D1::HwndRenderTargetProperties(hwnd, size);
    
    HRESULT hr = context->d2dFactory->CreateHwndRenderTarget(
        rtProps, hwndRtProps, &context->renderTarget);
    
    if (SUCCEEDED(hr)) {
        // Create initial brush
        hr = context->renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black), &context->currentBrush);
    }
    
    return hr;
}

void ReleaseRenderTarget(WindowsRenderContext* context) {
    if (context) {
        if (context->currentBrush) {
            context->currentBrush->Release();
            context->currentBrush = nullptr;
        }
        if (context->currentTextFormat) {
            context->currentTextFormat->Release();
            context->currentTextFormat = nullptr;
        }
        if (context->renderTarget) {
            context->renderTarget->Release();
            context->renderTarget = nullptr;
        }
    }
}

HRESULT ResizeRenderTarget(HWND hwnd, int width, int height) {
    auto it = std::find_if(g_windowsList.begin(), g_windowsList.end(),
                          [hwnd](WindowsWindowData* data) { return data->hwnd == hwnd; });
    
    if (it != g_windowsList.end()) {
        WindowsWindowData* windowData = *it;
        if (windowData->renderContext && windowData->renderContext->renderTarget) {
            D2D1_SIZE_U size = D2D1::SizeU(width, height);
            return windowData->renderContext->renderTarget->Resize(size);
        }
    }
    
    return E_INVALIDARG;
}

// ===== BASIC DRAWING FUNCTIONS =====
void DrawRectWindows(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->renderTarget) return;
    
    D2D1_RECT_F rect = RectToD2D1(x, y, w, h);
    D2D1_COLOR_F color = ColorToD2D1(r, g, b, a);
    
    if (g_windowsRenderContext->currentBrush) {
        g_windowsRenderContext->currentBrush->PaintWidthColorcolor);
        g_windowsRenderContext->renderTarget->DrawRectangle(rect, g_windowsRenderContext->currentBrush,
                                                           g_windowsRenderContext->currentStrokeWidth);
    }
}

void FillRectWindows(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->renderTarget) return;
    
    D2D1_RECT_F rect = RectToD2D1(x, y, w, h);
    D2D1_COLOR_F color = ColorToD2D1(r, g, b, a);
    
    if (g_windowsRenderContext->currentBrush) {
        g_windowsRenderContext->currentBrush->PaintWidthColorcolor);
        g_windowsRenderContext->renderTarget->FillRectangle(rect, g_windowsRenderContext->currentBrush);
    }
}

void DrawLineWindows(float x1, float y1, float x2, float y2, float r, float g, float b, float a) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->renderTarget) return;
    
    D2D1_POINT_2F point1 = PointToD2D1(x1, y1);
    D2D1_POINT_2F point2 = PointToD2D1(x2, y2);
    D2D1_COLOR_F color = ColorToD2D1(r, g, b, a);
    
    if (g_windowsRenderContext->currentBrush) {
        g_windowsRenderContext->currentBrush->PaintWidthColorcolor);
        g_windowsRenderContext->renderTarget->DrawLine(point1, point2, g_windowsRenderContext->currentBrush,
                                                      g_windowsRenderContext->currentStrokeWidth);
    }
}

void DrawCircleWindows(float cx, float cy, float radius, float r, float g, float b, float a) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->renderTarget) return;
    
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(PointToD2D1(cx, cy), radius, radius);
    D2D1_COLOR_F color = ColorToD2D1(r, g, b, a);
    
    if (g_windowsRenderContext->currentBrush) {
        g_windowsRenderContext->currentBrush->PaintWidthColorcolor);
        g_windowsRenderContext->renderTarget->DrawEllipse(ellipse, g_windowsRenderContext->currentBrush,
                                                         g_windowsRenderContext->currentStrokeWidth);
    }
}

void FillCircleWindows(float cx, float cy, float radius, float r, float g, float b, float a) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->renderTarget) return;
    
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(PointToD2D1(cx, cy), radius, radius);
    D2D1_COLOR_F color = ColorToD2D1(r, g, b, a);
    
    if (g_windowsRenderContext->currentBrush) {
        g_windowsRenderContext->currentBrush->PaintWidthColorcolor);
        g_windowsRenderContext->renderTarget->FillEllipse(ellipse, g_windowsRenderContext->currentBrush);
    }
}

// ===== TEXT RENDERING =====
void DrawTextWindows(const char* text, float x, float y, float r, float g, float b, float a) {
    DrawTextWithFontWindows(text, "Arial", 12.0f, x, y, r, g, b, a);
}

void DrawTextWithFontWindows(const char* text, const char* fontFamily, float fontSize, 
                            float x, float y, float r, float g, float b, float a) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->renderTarget || !text) return;
    
    std::wstring wText = StringToWString(text);
    D2D1_COLOR_F color = ColorToD2D1(r, g, b, a);
    
    // Create or update text format
    if (!g_windowsRenderContext->currentTextFormat) {
        g_windowsRenderContext->currentTextFormat = CreateTextFormat(fontFamily, fontSize);
    }
    
    if (g_windowsRenderContext->currentTextFormat && g_windowsRenderContext->currentBrush) {
        g_windowsRenderContext->currentBrush->PaintWidthColorcolor);
        
        D2D1_RECT_F layoutRect = D2D1::RectF(x, y, x + 1000, y + 100); // Large enough for single line
        g_windowsRenderContext->renderTarget->DrawTextW(wText.c_str(), static_cast<UINT32>(wText.length()),
                                                       g_windowsRenderContext->currentTextFormat,
                                                       layoutRect, g_windowsRenderContext->currentBrush);
    }
}

Point2D GetTextExtentsWindows(const char* text, const char* fontFamily, float fontSize) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->writeFactory || !text) {
        return Point2D(0, 0);
    }
    
    std::wstring wText = StringToWString(text);
    IDWriteTextFormat* textFormat = CreateTextFormat(fontFamily, fontSize);
    
    if (!textFormat) {
        return Point2D(0, 0);
    }
    
    IDWriteTextLayout* textLayout = nullptr;
    HRESULT hr = g_windowsRenderContext->writeFactory->CreateTextLayout(
        wText.c_str(), static_cast<UINT32>(wText.length()), textFormat,
        1000.0f, 100.0f, &textLayout);
    
    Point2D result(0, 0);
    if (SUCCEEDED(hr) && textLayout) {
        DWRITE_TEXT_METRICS metrics;
        hr = textLayout->GetMetrics(&metrics);
        if (SUCCEEDED(hr)) {
            result.x = metrics.width;
            result.y = metrics.height;
        }
        textLayout->Release();
    }
    
    ReleaseTextFormat(textFormat);
    return result;
}

// ===== IMAGE RENDERING =====
void DrawImageWindows(const char* filename, float x, float y, float w, float h) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->renderTarget || !filename) return;
    
    IWICBitmapSource* wicBitmap = LoadImageFromFile(filename);
    if (!wicBitmap) return;
    
    ID2D1Bitmap* d2dBitmap = CreateBitmapFromWIC(wicBitmap);
    if (d2dBitmap) {
        D2D1_SIZE_F bitmapSize = d2dBitmap->GetSize();
        
        // Use original size if width/height not specified
        if (w == 0) w = bitmapSize.width;
        if (h == 0) h = bitmapSize.height;
        
        D2D1_RECT_F destRect = RectToD2D1(x, y, w, h);
        g_windowsRenderContext->renderTarget->DrawBitmap(d2dBitmap, destRect, 1.0f,
                                                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        
        ReleaseBitmap(d2dBitmap);
    }
    
    wicBitmap->Release();
}

// ===== RENDERING STATE MANAGEMENT =====
void BeginDrawingWindows() {
    if (g_windowsRenderContext && g_windowsRenderContext->renderTarget) {
        g_windowsRenderContext->renderTarget->BeginDraw();
    }
}

void EndDrawingWindows() {
    if (g_windowsRenderContext && g_windowsRenderContext->renderTarget) {
        HRESULT hr = g_windowsRenderContext->renderTarget->EndDraw();
        if (FAILED(hr)) {
            LogWindowsError("EndDraw", hr);
        }
    }
}

void ClearWindows(float r, float g, float b, float a) {
    if (g_windowsRenderContext && g_windowsRenderContext->renderTarget) {
        D2D1_COLOR_F color = ColorToD2D1(r, g, b, a);
        g_windowsRenderContext->renderTarget->Clear(color);
    }
}

void SetClipRectWindows(float x, float y, float w, float h) {
    if (g_windowsRenderContext && g_windowsRenderContext->renderTarget) {
        D2D1_RECT_F clipRect = RectToD2D1(x, y, w, h);
        g_windowsRenderContext->renderTarget->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        g_windowsRenderContext->clipStack.push_back(clipRect);
    }
}

void ClearClipRectWindows() {
    if (g_windowsRenderContext && g_windowsRenderContext->renderTarget) {
        while (!g_windowsRenderContext->clipStack.empty()) {
            g_windowsRenderContext->renderTarget->PopAxisAlignedClip();
            g_windowsRenderContext->clipStack.pop_back();
        }
    }
}

// ===== EVENT CONVERSION =====
UCEvent ConvertWindowsMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    UCEvent event;
    event.type = UCEventType::Unknown;
    event.timestamp = GetTickCount64();
    event.windowId = reinterpret_cast<long>(hwnd);
    
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            event.type = UCEventType::MouseDown;
            event.button = ConvertMouseButton(message, wParam);
            event.x = GET_X_LPARAM(lParam);
            event.y = GET_Y_LPARAM(lParam);
            break;
            
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            event.type = UCEventType::MouseUp;
            event.button = ConvertMouseButton(message, wParam);
            event.x = GET_X_LPARAM(lParam);
            event.y = GET_Y_LPARAM(lParam);
            break;
            
        case WM_MOUSEMOVE:
            event.type = UCEventType::MouseMove;
            event.x = GET_X_LPARAM(lParam);
            event.y = GET_Y_LPARAM(lParam);
            break;
            
        case WM_KEYDOWN:
            event.type = UCEventType::KeyDown;
            event.key = ConvertVirtualKeyCode(wParam);
            break;
            
        case WM_KEYUP:
            event.type = UCEventType::KeyUp;
            event.key = ConvertVirtualKeyCode(wParam);
            break;
            
        case WM_CHAR:
            event.type = UCEventType::TextInput;
            event.character = static_cast<wchar_t>(wParam);
            break;
    }
    
    return event;
}

UCMouseButton ConvertMouseButton(UINT message, WPARAM wParam) {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            return UCMouseButton::Left;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            return UCMouseButton::Right;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            return UCMouseButton::Middle;
        default:
            return UCMouseButton::None;
    }
}

// ===== UTILITY FUNCTIONS =====
D2D1_COLOR_F ColorToD2D1(float r, float g, float b, float a) {
    return D2D1::ColorF(r, g, b, a);
}

D2D1_COLOR_F ColorToD2D1(const Color& color) {
    return D2D1::ColorF(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

D2D1_RECT_F RectToD2D1(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x + w, y + h);
}

D2D1_RECT_F RectToD2D1(const Rect2D& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
}

D2D1_POINT_2F PointToD2D1(float x, float y) {
    return D2D1::Point2F(x, y);
}

D2D1_POINT_2F PointToD2D1(const Point2D& point) {
    return D2D1::Point2F(point.x, point.y);
}

std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), nullptr, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &strTo[0], size_needed, nullptr, nullptr);
    return strTo;
}

void LogWindowsError(const char* function, DWORD error) {
    if (error == 0) error = GetLastError();
    
    LPVOID lpMsgBuf;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&lpMsgBuf, 0, nullptr);
    
    std::cerr << "Windows Error in " << function << ": " << static_cast<char*>(lpMsgBuf) << std::endl;
    LocalFree(lpMsgBuf);
}

// ===== FONT MANAGEMENT =====
IDWriteTextFormat* CreateTextFormat(const char* fontFamily, float fontSize, bool bold, bool italic) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->writeFactory) return nullptr;
    
    std::wstring wFontFamily = StringToWString(fontFamily);
    
    DWRITE_FONT_WEIGHT weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    
    IDWriteTextFormat* textFormat = nullptr;
    HRESULT hr = g_windowsRenderContext->writeFactory->CreateTextFormat(
        wFontFamily.c_str(), nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &textFormat);
    
    if (FAILED(hr)) {
        LogWindowsError("CreateTextFormat", hr);
        return nullptr;
    }
    
    return textFormat;
}

void ReleaseTextFormat(IDWriteTextFormat* textFormat) {
    if (textFormat) {
        textFormat->Release();
    }
}

// ===== RESOURCE MANAGEMENT =====
IWICBitmapSource* LoadImageFromFile(const char* filename) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->wicFactory || !filename) return nullptr;
    
    std::wstring wFilename = StringToWString(filename);
    
    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = g_windowsRenderContext->wicFactory->CreateDecoderFromFilename(
        wFilename.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    
    if (FAILED(hr)) return nullptr;
    
    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    
    IWICBitmapSource* bitmapSource = nullptr;
    if (SUCCEEDED(hr)) {
        bitmapSource = frame;
        bitmapSource->AddRef();
        frame->Release();
    }
    
    decoder->Release();
    return bitmapSource;
}

ID2D1Bitmap* CreateBitmapFromWIC(IWICBitmapSource* wicBitmap) {
    if (!g_windowsRenderContext || !g_windowsRenderContext->renderTarget || !wicBitmap) return nullptr;
    
    ID2D1Bitmap* bitmap = nullptr;
    HRESULT hr = g_windowsRenderContext->renderTarget->CreateBitmapFromWicBitmap(wicBitmap, &bitmap);
    
    if (FAILED(hr)) {
        LogWindowsError("CreateBitmapFromWicBitmap", hr);
        return nullptr;
    }
    
    return bitmap;
}

void ReleaseBitmap(ID2D1Bitmap* bitmap) {
    if (bitmap) {
        bitmap->Release();
    }
}

// ===== MESSAGE LOOP =====
bool ProcessWindowsMessages() {
    MSG msg;
    bool hasMessages = false;
    
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        hasMessages = true;
        
        if (msg.message == WM_QUIT) {
            return false;
        }
        
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    return true;
}

void PostUltraCanvasEvent(const UCEvent& event) {
    // This would typically post the event to the UltraCanvas event system
    // For now, we'll just output debug information
    #ifdef _DEBUG
    std::cout << "UCEvent: Type=" << static_cast<int>(event.type) 
              << " X=" << event.x << " Y=" << event.y << std::endl;
    #endif
}

} // namespace UltraCanvas

#endif // _WIN32
