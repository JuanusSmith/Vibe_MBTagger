// pch.h - Precompiled header
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00

// Do NOT define UNICODE/_UNICODE here - the vcxproj already passes them
// on the command line, causing C4005 redefinition warnings.

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <pathcch.h>

// CRT / STL
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cassert>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <optional>

// Lib links
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "pathcch.lib")

namespace fs = std::filesystem;
