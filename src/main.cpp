// MusicBrainz FLAC Tagger - Native Win32 C++ Application
// main.cpp - Application entry point and window management

#include "pch.h"
#include "MainWindow.h"
#include "resource.h"

// Global application instance
HINSTANCE g_hInstance = nullptr;

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    g_hInstance = hInstance;

    // Enable visual styles (requires manifest)
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    // Initialize COM for drag-and-drop
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize COM", L"Error", MB_ICONERROR);
        return 1;
    }

    // Create and show main window
    MainWindow mainWnd;
    if (!mainWnd.Create(hInstance)) {
        CoUninitialize();
        return 1;
    }
    mainWnd.Show(nCmdShow);

    // Message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
