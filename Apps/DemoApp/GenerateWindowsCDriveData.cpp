// GenerateWindowsCDriveData - Windows C: Drive demo data for UltraCanvasGourceTree
// Add this function to UltraCanvasGourceTreeExamples.cpp
// Version: 1.0.0
// Last Modified: 2025-12-26
// Author: UltraCanvas Framework

// ===== HELPER: Generate realistic Windows C: drive structure with 6 levels =====
void GenerateWindowsCDriveData(UltraCanvasGourceTree* tree) {
    // Set root as C: drive
    tree->SetRootNode("C:", "Local Disk (C:)", "C:");
    
    std::time_t now = std::time(nullptr);
    auto daysAgo = [now](int days) { return now - 86400 * days; };
    auto hoursAgo = [now](int hours) { return now - 3600 * hours; };
    
    // Helper to create file info
    auto makeFileInfo = [&](size_t sizeKB, int createdDaysAgo, int accessedDaysAgo) {
        GourceFileInfo info;
        info.fileSize = sizeKB * 1024;
        info.creationTime = daysAgo(createdDaysAgo);
        info.lastAccessTime = daysAgo(accessedDaysAgo);
        info.modificationTime = daysAgo(std::min(createdDaysAgo, accessedDaysAgo + 1));
        return info;
    };
    
    // ========================================
    // LEVEL 1: Root folders
    // ========================================
    tree->AddDirectory("C:", "C:/Windows", "Windows");
    tree->AddDirectory("C:", "C:/Program Files", "Program Files");
    tree->AddDirectory("C:", "C:/Program Files (x86)", "Program Files (x86)");
    tree->AddDirectory("C:", "C:/Users", "Users");
    tree->AddDirectory("C:", "C:/ProgramData", "ProgramData");
    tree->AddDirectory("C:", "C:/Temp", "Temp");
    
    // Root files
    tree->AddFile("C:", "C:/pagefile.sys", "pagefile.sys", makeFileInfo(4096000, 365, 0));  // 4GB
    tree->AddFile("C:", "C:/hiberfil.sys", "hiberfil.sys", makeFileInfo(3072000, 365, 0));  // 3GB
    tree->AddFile("C:", "C:/swapfile.sys", "swapfile.sys", makeFileInfo(256000, 365, 0));   // 256MB
    
    // ========================================
    // LEVEL 2: Windows folder
    // ========================================
    tree->AddDirectory("C:/Windows", "C:/Windows/System32", "System32");
    tree->AddDirectory("C:/Windows", "C:/Windows/SysWOW64", "SysWOW64");
    tree->AddDirectory("C:/Windows", "C:/Windows/Fonts", "Fonts");
    tree->AddDirectory("C:/Windows", "C:/Windows/Logs", "Logs");
    tree->AddDirectory("C:/Windows", "C:/Windows/Temp", "Temp");
    tree->AddDirectory("C:/Windows", "C:/Windows/WinSxS", "WinSxS");
    tree->AddDirectory("C:/Windows", "C:/Windows/Prefetch", "Prefetch");
    tree->AddDirectory("C:/Windows", "C:/Windows/Microsoft.NET", "Microsoft.NET");
    
    // Windows root files
    tree->AddFile("C:/Windows", "C:/Windows/explorer.exe", "explorer.exe", makeFileInfo(4800, 365, 0));
    tree->AddFile("C:/Windows", "C:/Windows/notepad.exe", "notepad.exe", makeFileInfo(220, 365, 5));
    tree->AddFile("C:/Windows", "C:/Windows/regedit.exe", "regedit.exe", makeFileInfo(380, 365, 30));
    tree->AddFile("C:/Windows", "C:/Windows/write.exe", "write.exe", makeFileInfo(12, 365, 180));
    tree->AddFile("C:/Windows", "C:/Windows/WindowsUpdate.log", "WindowsUpdate.log", makeFileInfo(2500, 365, 1));
    
    // ========================================
    // LEVEL 3: System32 folder (key Windows components)
    // ========================================
    tree->AddDirectory("C:/Windows/System32", "C:/Windows/System32/drivers", "drivers");
    tree->AddDirectory("C:/Windows/System32", "C:/Windows/System32/config", "config");
    tree->AddDirectory("C:/Windows/System32", "C:/Windows/System32/wbem", "wbem");
    tree->AddDirectory("C:/Windows/System32", "C:/Windows/System32/DriverStore", "DriverStore");
    tree->AddDirectory("C:/Windows/System32", "C:/Windows/System32/WindowsPowerShell", "WindowsPowerShell");
    tree->AddDirectory("C:/Windows/System32", "C:/Windows/System32/spool", "spool");
    
    // Key System32 files
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/ntdll.dll", "ntdll.dll", makeFileInfo(1980, 365, 0));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/kernel32.dll", "kernel32.dll", makeFileInfo(1120, 365, 0));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/user32.dll", "user32.dll", makeFileInfo(1780, 365, 0));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/gdi32.dll", "gdi32.dll", makeFileInfo(1650, 365, 0));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/shell32.dll", "shell32.dll", makeFileInfo(21500, 365, 0));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/cmd.exe", "cmd.exe", makeFileInfo(320, 365, 2));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/taskmgr.exe", "taskmgr.exe", makeFileInfo(1200, 365, 0));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/mmc.exe", "mmc.exe", makeFileInfo(520, 365, 15));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/msvcrt.dll", "msvcrt.dll", makeFileInfo(780, 365, 0));
    tree->AddFile("C:/Windows/System32", "C:/Windows/System32/ole32.dll", "ole32.dll", makeFileInfo(1420, 365, 0));
    
    // ========================================
    // LEVEL 4: Drivers folder
    // ========================================
    tree->AddDirectory("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/etc", "etc");
    tree->AddDirectory("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/UMDF", "UMDF");
    
    // Driver files
    tree->AddFile("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/ntfs.sys", "ntfs.sys", makeFileInfo(2180, 365, 0));
    tree->AddFile("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/tcpip.sys", "tcpip.sys", makeFileInfo(2850, 365, 0));
    tree->AddFile("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/fltMgr.sys", "fltMgr.sys", makeFileInfo(420, 365, 0));
    tree->AddFile("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/disk.sys", "disk.sys", makeFileInfo(180, 365, 0));
    tree->AddFile("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/volsnap.sys", "volsnap.sys", makeFileInfo(420, 365, 0));
    tree->AddFile("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/ndis.sys", "ndis.sys", makeFileInfo(1250, 365, 0));
    tree->AddFile("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/storport.sys", "storport.sys", makeFileInfo(520, 365, 0));
    tree->AddFile("C:/Windows/System32/drivers", "C:/Windows/System32/drivers/Wdf01000.sys", "Wdf01000.sys", makeFileInfo(780, 365, 0));
    
    // ========================================
    // LEVEL 5: etc folder (network config)
    // ========================================
    tree->AddFile("C:/Windows/System32/drivers/etc", "C:/Windows/System32/drivers/etc/hosts", "hosts", makeFileInfo(1, 365, 30));
    tree->AddFile("C:/Windows/System32/drivers/etc", "C:/Windows/System32/drivers/etc/services", "services", makeFileInfo(18, 365, 180));
    tree->AddFile("C:/Windows/System32/drivers/etc", "C:/Windows/System32/drivers/etc/protocol", "protocol", makeFileInfo(2, 365, 365));
    tree->AddFile("C:/Windows/System32/drivers/etc", "C:/Windows/System32/drivers/etc/networks", "networks", makeFileInfo(1, 365, 365));
    tree->AddFile("C:/Windows/System32/drivers/etc", "C:/Windows/System32/drivers/etc/lmhosts.sam", "lmhosts.sam", makeFileInfo(4, 365, 365));
    
    // ========================================
    // LEVEL 3: Fonts folder
    // ========================================
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/arial.ttf", "arial.ttf", makeFileInfo(780, 365, 0));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/arialbd.ttf", "arialbd.ttf", makeFileInfo(720, 365, 0));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/times.ttf", "times.ttf", makeFileInfo(420, 365, 1));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/cour.ttf", "cour.ttf", makeFileInfo(280, 365, 0));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/segoeui.ttf", "segoeui.ttf", makeFileInfo(580, 365, 0));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/tahoma.ttf", "tahoma.ttf", makeFileInfo(420, 365, 0));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/verdana.ttf", "verdana.ttf", makeFileInfo(240, 365, 2));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/consola.ttf", "consola.ttf", makeFileInfo(280, 365, 1));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/calibri.ttf", "calibri.ttf", makeFileInfo(520, 365, 0));
    tree->AddFile("C:/Windows/Fonts", "C:/Windows/Fonts/cambria.ttf", "cambria.ttf", makeFileInfo(1250, 365, 5));
    
    // ========================================
    // LEVEL 2: Program Files
    // ========================================
    tree->AddDirectory("C:/Program Files", "C:/Program Files/Microsoft Office", "Microsoft Office");
    tree->AddDirectory("C:/Program Files", "C:/Program Files/Windows Defender", "Windows Defender");
    tree->AddDirectory("C:/Program Files", "C:/Program Files/Common Files", "Common Files");
    tree->AddDirectory("C:/Program Files", "C:/Program Files/Internet Explorer", "Internet Explorer");
    tree->AddDirectory("C:/Program Files", "C:/Program Files/Windows NT", "Windows NT");
    tree->AddDirectory("C:/Program Files", "C:/Program Files/WindowsApps", "WindowsApps");
    tree->AddDirectory("C:/Program Files", "C:/Program Files/7-Zip", "7-Zip");
    tree->AddDirectory("C:/Program Files", "C:/Program Files/Git", "Git");
    
    // ========================================
    // LEVEL 3: Microsoft Office
    // ========================================
    tree->AddDirectory("C:/Program Files/Microsoft Office", "C:/Program Files/Microsoft Office/root", "root");
    
    // ========================================
    // LEVEL 4: Office root
    // ========================================
    tree->AddDirectory("C:/Program Files/Microsoft Office/root", "C:/Program Files/Microsoft Office/root/Office16", "Office16");
    tree->AddDirectory("C:/Program Files/Microsoft Office/root", "C:/Program Files/Microsoft Office/root/Client", "Client");
    
    // ========================================
    // LEVEL 5: Office16 applications
    // ========================================
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16", "C:/Program Files/Microsoft Office/root/Office16/WINWORD.EXE", "WINWORD.EXE", makeFileInfo(2450, 180, 0));
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16", "C:/Program Files/Microsoft Office/root/Office16/EXCEL.EXE", "EXCEL.EXE", makeFileInfo(3120, 180, 0));
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16", "C:/Program Files/Microsoft Office/root/Office16/POWERPNT.EXE", "POWERPNT.EXE", makeFileInfo(2180, 180, 2));
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16", "C:/Program Files/Microsoft Office/root/Office16/OUTLOOK.EXE", "OUTLOOK.EXE", makeFileInfo(2850, 180, 0));
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16", "C:/Program Files/Microsoft Office/root/Office16/MSACCESS.EXE", "MSACCESS.EXE", makeFileInfo(1520, 180, 30));
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16", "C:/Program Files/Microsoft Office/root/Office16/ONENOTE.EXE", "ONENOTE.EXE", makeFileInfo(1850, 180, 5));
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16", "C:/Program Files/Microsoft Office/root/Office16/MSPUB.EXE", "MSPUB.EXE", makeFileInfo(1120, 180, 60));
    
    // ========================================
    // LEVEL 6: Office16 DLLs (deepest level)
    // ========================================
    tree->AddDirectory("C:/Program Files/Microsoft Office/root/Office16", "C:/Program Files/Microsoft Office/root/Office16/1033", "1033");
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16/1033", "C:/Program Files/Microsoft Office/root/Office16/1033/WWINTL.DLL", "WWINTL.DLL", makeFileInfo(1250, 180, 0));
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16/1033", "C:/Program Files/Microsoft Office/root/Office16/1033/XLINTL.DLL", "XLINTL.DLL", makeFileInfo(980, 180, 0));
    tree->AddFile("C:/Program Files/Microsoft Office/root/Office16/1033", "C:/Program Files/Microsoft Office/root/Office16/1033/GrooveIntlResource.dll", "GrooveIntlResource.dll", makeFileInfo(420, 180, 30));
    
    // ========================================
    // LEVEL 3: 7-Zip
    // ========================================
    tree->AddFile("C:/Program Files/7-Zip", "C:/Program Files/7-Zip/7z.exe", "7z.exe", makeFileInfo(520, 90, 5));
    tree->AddFile("C:/Program Files/7-Zip", "C:/Program Files/7-Zip/7zFM.exe", "7zFM.exe", makeFileInfo(920, 90, 5));
    tree->AddFile("C:/Program Files/7-Zip", "C:/Program Files/7-Zip/7zG.exe", "7zG.exe", makeFileInfo(680, 90, 10));
    tree->AddFile("C:/Program Files/7-Zip", "C:/Program Files/7-Zip/7z.dll", "7z.dll", makeFileInfo(1850, 90, 5));
    tree->AddFile("C:/Program Files/7-Zip", "C:/Program Files/7-Zip/License.txt", "License.txt", makeFileInfo(4, 90, 90));
    tree->AddFile("C:/Program Files/7-Zip", "C:/Program Files/7-Zip/readme.txt", "readme.txt", makeFileInfo(2, 90, 90));
    tree->AddFile("C:/Program Files/7-Zip", "C:/Program Files/7-Zip/History.txt", "History.txt", makeFileInfo(85, 90, 90));
    
    // ========================================
    // LEVEL 3-6: Git folder with deep structure
    // ========================================
    tree->AddDirectory("C:/Program Files/Git", "C:/Program Files/Git/bin", "bin");
    tree->AddDirectory("C:/Program Files/Git", "C:/Program Files/Git/cmd", "cmd");
    tree->AddDirectory("C:/Program Files/Git", "C:/Program Files/Git/mingw64", "mingw64");
    tree->AddDirectory("C:/Program Files/Git", "C:/Program Files/Git/usr", "usr");
    
    // Git bin files
    tree->AddFile("C:/Program Files/Git/bin", "C:/Program Files/Git/bin/git.exe", "git.exe", makeFileInfo(3200, 60, 0));
    tree->AddFile("C:/Program Files/Git/bin", "C:/Program Files/Git/bin/bash.exe", "bash.exe", makeFileInfo(2100, 60, 1));
    tree->AddFile("C:/Program Files/Git/bin", "C:/Program Files/Git/bin/sh.exe", "sh.exe", makeFileInfo(2100, 60, 1));
    
    // Git mingw64 deep structure
    tree->AddDirectory("C:/Program Files/Git/mingw64", "C:/Program Files/Git/mingw64/bin", "bin");
    tree->AddDirectory("C:/Program Files/Git/mingw64", "C:/Program Files/Git/mingw64/lib", "lib");
    tree->AddDirectory("C:/Program Files/Git/mingw64", "C:/Program Files/Git/mingw64/libexec", "libexec");
    
    tree->AddDirectory("C:/Program Files/Git/mingw64/libexec", "C:/Program Files/Git/mingw64/libexec/git-core", "git-core");
    tree->AddFile("C:/Program Files/Git/mingw64/libexec/git-core", "C:/Program Files/Git/mingw64/libexec/git-core/git-add.exe", "git-add.exe", makeFileInfo(2850, 60, 0));
    tree->AddFile("C:/Program Files/Git/mingw64/libexec/git-core", "C:/Program Files/Git/mingw64/libexec/git-core/git-commit.exe", "git-commit.exe", makeFileInfo(2850, 60, 0));
    tree->AddFile("C:/Program Files/Git/mingw64/libexec/git-core", "C:/Program Files/Git/mingw64/libexec/git-core/git-push.exe", "git-push.exe", makeFileInfo(2850, 60, 1));
    tree->AddFile("C:/Program Files/Git/mingw64/libexec/git-core", "C:/Program Files/Git/mingw64/libexec/git-core/git-pull.exe", "git-pull.exe", makeFileInfo(2850, 60, 0));
    tree->AddFile("C:/Program Files/Git/mingw64/libexec/git-core", "C:/Program Files/Git/mingw64/libexec/git-core/git-merge.exe", "git-merge.exe", makeFileInfo(2850, 60, 3));
    tree->AddFile("C:/Program Files/Git/mingw64/libexec/git-core", "C:/Program Files/Git/mingw64/libexec/git-core/git-remote.exe", "git-remote.exe", makeFileInfo(2850, 60, 2));
    
    // ========================================
    // LEVEL 2-6: Users folder (most active area)
    // ========================================
    tree->AddDirectory("C:/Users", "C:/Users/Default", "Default");
    tree->AddDirectory("C:/Users", "C:/Users/Public", "Public");
    tree->AddDirectory("C:/Users", "C:/Users/Stefan", "Stefan");
    
    // ========================================
    // LEVEL 3: User Stefan folders
    // ========================================
    tree->AddDirectory("C:/Users/Stefan", "C:/Users/Stefan/Desktop", "Desktop");
    tree->AddDirectory("C:/Users/Stefan", "C:/Users/Stefan/Documents", "Documents");
    tree->AddDirectory("C:/Users/Stefan", "C:/Users/Stefan/Downloads", "Downloads");
    tree->AddDirectory("C:/Users/Stefan", "C:/Users/Stefan/Pictures", "Pictures");
    tree->AddDirectory("C:/Users/Stefan", "C:/Users/Stefan/Music", "Music");
    tree->AddDirectory("C:/Users/Stefan", "C:/Users/Stefan/Videos", "Videos");
    tree->AddDirectory("C:/Users/Stefan", "C:/Users/Stefan/AppData", "AppData");
    tree->AddDirectory("C:/Users/Stefan", "C:/Users/Stefan/.vscode", ".vscode");
    
    // Desktop files
    tree->AddFile("C:/Users/Stefan/Desktop", "C:/Users/Stefan/Desktop/project_notes.txt", "project_notes.txt", makeFileInfo(15, 7, 0));
    tree->AddFile("C:/Users/Stefan/Desktop", "C:/Users/Stefan/Desktop/todo.md", "todo.md", makeFileInfo(8, 14, 0));
    tree->AddFile("C:/Users/Stefan/Desktop", "C:/Users/Stefan/Desktop/UltraOS_Design.pdf", "UltraOS_Design.pdf", makeFileInfo(2500, 30, 2));
    tree->AddFile("C:/Users/Stefan/Desktop", "C:/Users/Stefan/Desktop/screenshot.png", "screenshot.png", makeFileInfo(850, 3, 1));
    
    // ========================================
    // LEVEL 4: Documents subfolders
    // ========================================
    tree->AddDirectory("C:/Users/Stefan/Documents", "C:/Users/Stefan/Documents/Projects", "Projects");
    tree->AddDirectory("C:/Users/Stefan/Documents", "C:/Users/Stefan/Documents/Reports", "Reports");
    tree->AddDirectory("C:/Users/Stefan/Documents", "C:/Users/Stefan/Documents/Personal", "Personal");
    
    // ========================================
    // LEVEL 5: Projects folder
    // ========================================
    tree->AddDirectory("C:/Users/Stefan/Documents/Projects", "C:/Users/Stefan/Documents/Projects/UltraCanvas", "UltraCanvas");
    tree->AddDirectory("C:/Users/Stefan/Documents/Projects", "C:/Users/Stefan/Documents/Projects/UltraOS", "UltraOS");
    tree->AddDirectory("C:/Users/Stefan/Documents/Projects", "C:/Users/Stefan/Documents/Projects/WebApp", "WebApp");
    
    // ========================================
    // LEVEL 6: UltraCanvas project (deepest level)
    // ========================================
    tree->AddDirectory("C:/Users/Stefan/Documents/Projects/UltraCanvas", "C:/Users/Stefan/Documents/Projects/UltraCanvas/src", "src");
    tree->AddDirectory("C:/Users/Stefan/Documents/Projects/UltraCanvas", "C:/Users/Stefan/Documents/Projects/UltraCanvas/include", "include");
    tree->AddDirectory("C:/Users/Stefan/Documents/Projects/UltraCanvas", "C:/Users/Stefan/Documents/Projects/UltraCanvas/docs", "docs");
    
    tree->AddFile("C:/Users/Stefan/Documents/Projects/UltraCanvas/src", "C:/Users/Stefan/Documents/Projects/UltraCanvas/src/main.cpp", "main.cpp", makeFileInfo(25, 1, 0));
    tree->AddFile("C:/Users/Stefan/Documents/Projects/UltraCanvas/src", "C:/Users/Stefan/Documents/Projects/UltraCanvas/src/UltraCanvasGourceTree.cpp", "UltraCanvasGourceTree.cpp", makeFileInfo(85, 0, 0));
    tree->AddFile("C:/Users/Stefan/Documents/Projects/UltraCanvas/include", "C:/Users/Stefan/Documents/Projects/UltraCanvas/include/UltraCanvasGourceTree.h", "UltraCanvasGourceTree.h", makeFileInfo(18, 0, 0));
    tree->AddFile("C:/Users/Stefan/Documents/Projects/UltraCanvas", "C:/Users/Stefan/Documents/Projects/UltraCanvas/CMakeLists.txt", "CMakeLists.txt", makeFileInfo(5, 2, 0));
    tree->AddFile("C:/Users/Stefan/Documents/Projects/UltraCanvas", "C:/Users/Stefan/Documents/Projects/UltraCanvas/README.md", "README.md", makeFileInfo(12, 5, 1));
    
    // UltraOS project
    tree->AddFile("C:/Users/Stefan/Documents/Projects/UltraOS", "C:/Users/Stefan/Documents/Projects/UltraOS/kernel.cpp", "kernel.cpp", makeFileInfo(120, 15, 3));
    tree->AddFile("C:/Users/Stefan/Documents/Projects/UltraOS", "C:/Users/Stefan/Documents/Projects/UltraOS/bootloader.asm", "bootloader.asm", makeFileInfo(8, 30, 30));
    tree->AddFile("C:/Users/Stefan/Documents/Projects/UltraOS", "C:/Users/Stefan/Documents/Projects/UltraOS/Makefile", "Makefile", makeFileInfo(3, 20, 10));
    
    // ========================================
    // LEVEL 4: Downloads (recent activity)
    // ========================================
    tree->AddFile("C:/Users/Stefan/Downloads", "C:/Users/Stefan/Downloads/vscode-setup.exe", "vscode-setup.exe", makeFileInfo(95000, 7, 7));
    tree->AddFile("C:/Users/Stefan/Downloads", "C:/Users/Stefan/Downloads/cmake-3.28.1-windows-x86_64.msi", "cmake-3.28.1-windows-x86_64.msi", makeFileInfo(32000, 14, 14));
    tree->AddFile("C:/Users/Stefan/Downloads", "C:/Users/Stefan/Downloads/git-2.43.0-64-bit.exe", "git-2.43.0-64-bit.exe", makeFileInfo(58000, 60, 60));
    tree->AddFile("C:/Users/Stefan/Downloads", "C:/Users/Stefan/Downloads/documentation.pdf", "documentation.pdf", makeFileInfo(4500, 3, 1));
    tree->AddFile("C:/Users/Stefan/Downloads", "C:/Users/Stefan/Downloads/sample_data.csv", "sample_data.csv", makeFileInfo(250, 2, 0));
    tree->AddFile("C:/Users/Stefan/Downloads", "C:/Users/Stefan/Downloads/image_pack.zip", "image_pack.zip", makeFileInfo(85000, 5, 3));
    
    // ========================================
    // LEVEL 4: Pictures with subfolders
    // ========================================
    tree->AddDirectory("C:/Users/Stefan/Pictures", "C:/Users/Stefan/Pictures/Screenshots", "Screenshots");
    tree->AddDirectory("C:/Users/Stefan/Pictures", "C:/Users/Stefan/Pictures/Wallpapers", "Wallpapers");
    tree->AddDirectory("C:/Users/Stefan/Pictures", "C:/Users/Stefan/Pictures/Camera Roll", "Camera Roll");
    
    // Level 5: Screenshots
    tree->AddFile("C:/Users/Stefan/Pictures/Screenshots", "C:/Users/Stefan/Pictures/Screenshots/Screenshot_2024-12-01.png", "Screenshot_2024-12-01.png", makeFileInfo(1250, 25, 20));
    tree->AddFile("C:/Users/Stefan/Pictures/Screenshots", "C:/Users/Stefan/Pictures/Screenshots/Screenshot_2024-12-15.png", "Screenshot_2024-12-15.png", makeFileInfo(980, 11, 5));
    tree->AddFile("C:/Users/Stefan/Pictures/Screenshots", "C:/Users/Stefan/Pictures/Screenshots/debug_output.png", "debug_output.png", makeFileInfo(450, 2, 0));
    
    // Level 5: Wallpapers
    tree->AddFile("C:/Users/Stefan/Pictures/Wallpapers", "C:/Users/Stefan/Pictures/Wallpapers/nature_4k.jpg", "nature_4k.jpg", makeFileInfo(8500, 90, 30));
    tree->AddFile("C:/Users/Stefan/Pictures/Wallpapers", "C:/Users/Stefan/Pictures/Wallpapers/abstract_dark.png", "abstract_dark.png", makeFileInfo(3200, 60, 60));
    tree->AddFile("C:/Users/Stefan/Pictures/Wallpapers", "C:/Users/Stefan/Pictures/Wallpapers/mountains.jpg", "mountains.jpg", makeFileInfo(5800, 120, 45));
    
    // ========================================
    // LEVEL 4-6: AppData (deep Windows config)
    // ========================================
    tree->AddDirectory("C:/Users/Stefan/AppData", "C:/Users/Stefan/AppData/Local", "Local");
    tree->AddDirectory("C:/Users/Stefan/AppData", "C:/Users/Stefan/AppData/Roaming", "Roaming");
    tree->AddDirectory("C:/Users/Stefan/AppData", "C:/Users/Stefan/AppData/LocalLow", "LocalLow");
    
    // Level 5: Local apps
    tree->AddDirectory("C:/Users/Stefan/AppData/Local", "C:/Users/Stefan/AppData/Local/Microsoft", "Microsoft");
    tree->AddDirectory("C:/Users/Stefan/AppData/Local", "C:/Users/Stefan/AppData/Local/Google", "Google");
    tree->AddDirectory("C:/Users/Stefan/AppData/Local", "C:/Users/Stefan/AppData/Local/Programs", "Programs");
    tree->AddDirectory("C:/Users/Stefan/AppData/Local", "C:/Users/Stefan/AppData/Local/Temp", "Temp");
    
    // Level 6: Chrome data (deepest)
    tree->AddDirectory("C:/Users/Stefan/AppData/Local/Google", "C:/Users/Stefan/AppData/Local/Google/Chrome", "Chrome");
    tree->AddDirectory("C:/Users/Stefan/AppData/Local/Google/Chrome", "C:/Users/Stefan/AppData/Local/Google/Chrome/User Data", "User Data");
    tree->AddFile("C:/Users/Stefan/AppData/Local/Google/Chrome/User Data", "C:/Users/Stefan/AppData/Local/Google/Chrome/User Data/Local State", "Local State", makeFileInfo(45, 1, 0));
    tree->AddFile("C:/Users/Stefan/AppData/Local/Google/Chrome/User Data", "C:/Users/Stefan/AppData/Local/Google/Chrome/User Data/First Run", "First Run", makeFileInfo(1, 365, 365));
    
    // Level 6: VSCode data
    tree->AddDirectory("C:/Users/Stefan/AppData/Local/Programs", "C:/Users/Stefan/AppData/Local/Programs/Microsoft VS Code", "Microsoft VS Code");
    tree->AddFile("C:/Users/Stefan/AppData/Local/Programs/Microsoft VS Code", "C:/Users/Stefan/AppData/Local/Programs/Microsoft VS Code/Code.exe", "Code.exe", makeFileInfo(145000, 7, 0));
    tree->AddFile("C:/Users/Stefan/AppData/Local/Programs/Microsoft VS Code", "C:/Users/Stefan/AppData/Local/Programs/Microsoft VS Code/unins000.exe", "unins000.exe", makeFileInfo(3200, 7, 7));
    
    // Level 5: Roaming apps
    tree->AddDirectory("C:/Users/Stefan/AppData/Roaming", "C:/Users/Stefan/AppData/Roaming/Microsoft", "Microsoft");
    tree->AddDirectory("C:/Users/Stefan/AppData/Roaming", "C:/Users/Stefan/AppData/Roaming/Code", "Code");
    tree->AddDirectory("C:/Users/Stefan/AppData/Roaming", "C:/Users/Stefan/AppData/Roaming/npm", "npm");
    
    // Level 6: VSCode settings
    tree->AddDirectory("C:/Users/Stefan/AppData/Roaming/Code", "C:/Users/Stefan/AppData/Roaming/Code/User", "User");
    tree->AddFile("C:/Users/Stefan/AppData/Roaming/Code/User", "C:/Users/Stefan/AppData/Roaming/Code/User/settings.json", "settings.json", makeFileInfo(8, 3, 0));
    tree->AddFile("C:/Users/Stefan/AppData/Roaming/Code/User", "C:/Users/Stefan/AppData/Roaming/Code/User/keybindings.json", "keybindings.json", makeFileInfo(2, 30, 30));
    tree->AddFile("C:/Users/Stefan/AppData/Roaming/Code/User", "C:/Users/Stefan/AppData/Roaming/Code/User/snippets.json", "snippets.json", makeFileInfo(5, 14, 7));
    
    // ========================================
    // LEVEL 2: Program Files (x86)
    // ========================================
    tree->AddDirectory("C:/Program Files (x86)", "C:/Program Files (x86)/Microsoft", "Microsoft");
    tree->AddDirectory("C:/Program Files (x86)", "C:/Program Files (x86)/Windows Kits", "Windows Kits");
    tree->AddDirectory("C:/Program Files (x86)", "C:/Program Files (x86)/Common Files", "Common Files");
    
    // ========================================
    // LEVEL 2: ProgramData (hidden system data)
    // ========================================
    tree->AddDirectory("C:/ProgramData", "C:/ProgramData/Microsoft", "Microsoft");
    tree->AddDirectory("C:/ProgramData", "C:/ProgramData/Package Cache", "Package Cache");
    tree->AddDirectory("C:/ProgramData", "C:/ProgramData/chocolatey", "chocolatey");
    
    tree->AddDirectory("C:/ProgramData/Microsoft", "C:/ProgramData/Microsoft/Windows", "Windows");
    tree->AddDirectory("C:/ProgramData/Microsoft/Windows", "C:/ProgramData/Microsoft/Windows/Start Menu", "Start Menu");
    tree->AddDirectory("C:/ProgramData/Microsoft/Windows/Start Menu", "C:/ProgramData/Microsoft/Windows/Start Menu/Programs", "Programs");
    tree->AddFile("C:/ProgramData/Microsoft/Windows/Start Menu/Programs", "C:/ProgramData/Microsoft/Windows/Start Menu/Programs/desktop.ini", "desktop.ini", makeFileInfo(1, 365, 30));
    
    // ========================================
    // LEVEL 2: Temp folder
    // ========================================
    tree->AddFile("C:/Temp", "C:/Temp/setup_log.txt", "setup_log.txt", makeFileInfo(25, 7, 7));
    tree->AddFile("C:/Temp", "C:/Temp/crash_dump.dmp", "crash_dump.dmp", makeFileInfo(15000, 3, 3));
    tree->AddFile("C:/Temp", "C:/Temp/installer_cache.tmp", "installer_cache.tmp", makeFileInfo(8500, 1, 1));
}
