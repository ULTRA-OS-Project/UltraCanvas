# UltraCanvas WASM - Quick Start Guide

## 🚀 Get Started in 5 Minutes

### Step 1: Install Emscripten (One-time setup)

```bash
# Clone Emscripten SDK
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install and activate
./emsdk install latest
./emsdk activate latest

# Add to your shell (run this in each terminal or add to ~/.bashrc)
source ./emsdk_env.sh
```

**Verify installation:**
```bash
emcc --version
# Should show: emcc (Emscripten gcc/clang-like replacement) 3.x.x
```

---

### Step 2: Copy WASM Platform to Your Project

```bash
# Copy all files from this directory to your UltraCanvas project:
cp -r OS/WASM/ /path/to/your/UltraCanvas/OS/
```

---

### Step 3: Update Header Files

**File: `include/UltraCanvasApplication.h`**

Add after the other platform includes:

```cpp
#elif defined(__WASM__)
    // WASM Platform
    #include "../OS/WASM/UltraCanvasWASMApplication.h"
    #define UltraCanvasNativeApplication UltraCanvasWASMApplication
```

**File: `include/UltraCanvasWindow.h`**

Add after the other platform includes:

```cpp
#elif defined(__WASM__)
    // WASM Platform
    #include "../OS/WASM/UltraCanvasWASMWindow.h"
    namespace UltraCanvas {
        using UltraCanvasNativeWindow = UltraCanvasWASMWindow;
    }
```

---

### Step 4: Update Main CMakeLists.txt

Add to your main `CMakeLists.txt`:

```cmake
# WASM Platform Support
if(EMSCRIPTEN)
    message(STATUS "Building for WebAssembly/WASM platform")
    add_subdirectory(OS/WASM)
    target_link_libraries(UltraCanvas PRIVATE UltraCanvasWASM)
    
    # Set output to current directory for easy testing
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
```

---

### Step 5: Create Your Application

**File: `examples/HelloWASM.cpp`**

```cpp
#include <UltraCanvas.h>

using namespace UltraCanvas;

int main() {
    // Create application
    auto app = UltraCanvasApplication::Create();
    
    if (!app->Initialize()) {
        std::cerr << "Failed to initialize application" << std::endl;
        return -1;
    }
    
    // Create window
    WindowConfig config;
    config.title = "Hello UltraCanvas WASM!";
    config.width = 800;
    config.height = 600;
    config.backgroundColor = Color(240, 240, 250);
    
    auto window = std::make_shared<UltraCanvasWindow>();
    if (!window->Create(config)) {
        std::cerr << "Failed to create window" << std::endl;
        return -1;
    }
    
    // Add a button
    auto button = CreateButton("Click Me!", 50, 50, 150, 40);
    button->OnClick([]() {
        WASMBrowser::Alert("Hello from WASM!");
    });
    window->AddChild(button);
    
    // Add a label
    auto label = CreateLabel("Running in your browser!", 50, 120, 300, 30);
    label->SetFontSize(16);
    label->SetTextColor(Color(50, 50, 150));
    window->AddChild(label);
    
    // Show and run
    app->RegisterWindow(window.get());
    window->Show();
    
    return app->Run();
}
```

---

### Step 6: Create HTML Shell

**File: `examples/shell.html`**

```html
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>UltraCanvas WASM Demo</title>
    <style>
        body { 
            margin: 0; 
            padding: 20px;
            font-family: Arial, sans-serif;
            background: #2c3e50;
            color: white;
        }
        .container {
            max-width: 850px;
            margin: 0 auto;
        }
        h1 {
            text-align: center;
            margin-bottom: 20px;
        }
        #canvas { 
            display: block; 
            margin: 0 auto;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
            border-radius: 4px;
        }
        .info {
            margin-top: 20px;
            padding: 15px;
            background: rgba(255,255,255,0.1);
            border-radius: 4px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 UltraCanvas WebAssembly Demo</h1>
        
        <canvas id="canvas"></canvas>
        
        <div class="info">
            <p><strong>Status:</strong> <span id="status">Loading...</span></p>
            <p><strong>Info:</strong> This is C++ compiled to WebAssembly running in your browser!</p>
        </div>
    </div>
    
    <script>
        // Configure Emscripten Module
        var Module = {
            canvas: document.getElementById('canvas'),
            
            print: function(text) { 
                console.log('[stdout]', text); 
            },
            
            printErr: function(text) { 
                console.error('[stderr]', text); 
            },
            
            onRuntimeInitialized: function() {
                document.getElementById('status').textContent = 'Running ✅';
                console.log('UltraCanvas WASM initialized successfully!');
            }
        };
    </script>
    
    <!-- Load the WASM module -->
    <script src="HelloWASM.js"></script>
</body>
</html>
```

---

### Step 7: Build

```bash
# Create build directory
mkdir build-wasm
cd build-wasm

# Configure
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
emmake make HelloWASM -j8

# If successful, you should see:
# - HelloWASM.js
# - HelloWASM.wasm
# - HelloWASM.html (if shell specified)
```

---

### Step 8: Test Locally

```bash
# Start a local web server (Python 3)
python3 -m http.server 8000

# Or use Node.js (if you have it)
npx http-server -p 8000

# Open in browser:
# http://localhost:8000/shell.html
```

**Expected Result:** You should see your UltraCanvas application running in the browser! 🎉

---

## 🎯 Common Build Options

### Minimal Build (Fastest compile)

```bash
emcmake cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O2"

emmake make -j8
```

### Debug Build (With source maps)

```bash
emcmake cmake .. \
    -DCMAKE_BUILD_TYPE=Debug

emmake make -j8
```

### Optimized Build (Smallest size)

```bash
emcmake cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -flto"

emmake make -j8
```

---

## 🛠️ Customizing the Build

### Change Canvas ID

```cpp
// In your application code:
WindowConfig config;
config.platformData = "myCanvasId";  // Custom canvas ID
```

```html
<!-- In your HTML: -->
<canvas id="myCanvasId"></canvas>
```

### Custom Memory Settings

Add to `CMakeLists.txt`:

```cmake
if(EMSCRIPTEN)
    target_link_options(YourApp PUBLIC
        -sINITIAL_MEMORY=128MB
        -sMAXIMUM_MEMORY=1GB
    )
endif()
```

### Embed Assets

```bash
emcc ... --preload-file assets@/assets
```

Then access in C++ as `/assets/image.png`

---

## 📦 Deployment

### Quick Deploy to GitHub Pages

```bash
# Build release version
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make -j8

# Copy to docs/ folder (for GitHub Pages)
mkdir -p docs
cp HelloWASM.js HelloWASM.wasm shell.html docs/
cp shell.html docs/index.html

# Commit and push
git add docs/
git commit -m "Deploy WASM app"
git push

# Enable GitHub Pages in repo settings → Pages → Source: docs/
```

Your app will be live at: `https://yourusername.github.io/yourrepo/`

---

## ⚠️ Troubleshooting

### "Module is not defined"

**Problem:** JavaScript Module object not configured  
**Solution:** Ensure Module is defined before loading .js file

```html
<script>
    var Module = { canvas: document.getElementById('canvas') };
</script>
<script src="YourApp.js"></script>
```

### "Canvas is null"

**Problem:** Canvas element doesn't exist or wrong ID  
**Solution:** Verify canvas ID matches

```html
<canvas id="canvas"></canvas>  <!-- Must exist before script loads -->
```

### "Out of memory"

**Problem:** Application needs more memory  
**Solution:** Increase memory limits

```cmake
target_link_options(YourApp PUBLIC -sINITIAL_MEMORY=128MB)
```

### CORS Errors

**Problem:** Loading from file:// protocol  
**Solution:** Use a local web server

```bash
python3 -m http.server 8000
# Then open http://localhost:8000/
```

---

## 🎓 Next Steps

1. **Read the full README.md** for advanced features
2. **Explore examples/** for more complex demos
3. **Try building your UI** with UltraCanvas components
4. **Deploy to production** using the deployment guide

---

## 💡 Tips

- Use Chrome DevTools → Console for debugging
- Enable "Preserve log" to see startup messages
- Use Debug build for readable stack traces
- Test on mobile devices for touch events
- Use Lighthouse for performance audits

---

## 📚 Resources

- **Emscripten Docs:** https://emscripten.org/docs/
- **WebAssembly.org:** https://webassembly.org/
- **MDN Canvas API:** https://developer.mozilla.org/en-US/docs/Web/API/Canvas_API
- **UltraCanvas Docs:** Check project documentation

---

**That's it! You're now running C++ in the browser! 🎉**

Questions? Check the README.md or open an issue.
