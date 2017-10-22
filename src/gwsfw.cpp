// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include "../res/resource.h"

#include "backup.h"
#include "watchdog.h"

#include "gwsfw_names.h"

#pragma comment(lib, "shell32.lib")

#define WM_USER_SHELLICON (WM_USER + 1)

namespace {

struct globals_t
{
    HINSTANCE instance;
    WNDCLASSEX window_class = {0};
    HWND window;

    std::wstring tip;
    watchdog_t watchdog;
};

static globals_t globals;

}


std::wstring FindScreenshots()
{
    wchar_t path_documents[MAX_PATH];
    if (S_OK != SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, path_documents))
    {
        MessageBoxW(nullptr, L"Cannot find Documents folder!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return {};
    }

    std::wstring folder(path_documents);
    folder.append(L"\\");
    folder.append(PROJECT_NAME);
    folder.append(L"\\Screens");

    DWORD folder_attributes = GetFileAttributesW(folder.c_str());
    if (folder_attributes == INVALID_FILE_ATTRIBUTES || !(folder_attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        MessageBoxW(nullptr, (L"Cannot reach " + PROJECT_SHORT_NAME + L" screenshots folder!").c_str(), L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return {};
    }

    return folder;
}

bool IconCreate()
{
    NOTIFYICONDATA nid      = {0};
    nid.cbSize              = sizeof(NOTIFYICONDATA);
    nid.hWnd                = globals.window;
    nid.uID                 = IDI_ICON_GWSFW;
    nid.uFlags              = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    wcscpy_s(nid.szTip, globals.tip.c_str());
    nid.hIcon               = LoadIconW(globals.instance, (LPCTSTR)MAKEINTRESOURCE(IDI_ICON_GWSFW));
    nid.uCallbackMessage    = WM_USER_SHELLICON;

    return Shell_NotifyIconW(NIM_ADD, &nid) != 0;
}


bool IconRemove()
{
    NOTIFYICONDATA nid      = {0};
    nid.cbSize              = sizeof(NOTIFYICONDATA);
    nid.hWnd                = globals.window;
    nid.uID                 = IDI_ICON_GWSFW;

    return Shell_NotifyIconW(NIM_DELETE, &nid) != 0;
}


void IconSetTip(const std::wstring& tip)
{
    globals.tip = tip;

    NOTIFYICONDATA nid      = {0};
    nid.cbSize              = sizeof(NOTIFYICONDATA);
    nid.hWnd                = globals.window;
    nid.uID                 = IDI_ICON_GWSFW;
    nid.uFlags              = NIF_TIP;
    wcscpy_s(nid.szTip, globals.tip.c_str());

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}


void IconNotify(const std::wstring& text)
{
    NOTIFYICONDATA nid      = {0};
    nid.cbSize              = sizeof(NOTIFYICONDATA);
    nid.hWnd                = globals.window;
    nid.uID                 = IDI_ICON_GWSFW;
    nid.uFlags              = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, (PROJECT_SHORT_NAME + L" Screenshots Folder Watchdog").c_str());
    wcscpy_s(nid.szInfo, (L"Screenshots moved to " + text).c_str());
    nid.dwInfoFlags         = NIIF_INFO;

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}


bool OnShellIcon(WORD value)
{
    if (value == WM_RBUTTONDOWN)
    {
        HMENU hMenu, hSubMenu;
        hMenu = LoadMenuW(globals.instance, MAKEINTRESOURCE(IDR_MENU_TRAY));
        if (!hMenu) { return false; }

        hSubMenu = GetSubMenu(hMenu, 0);
        if (!hSubMenu) { DestroyMenu(hMenu); return false; }

        SetForegroundWindow(globals.window);

        POINT lpClickPoint;
        GetCursorPos(&lpClickPoint);
        TrackPopupMenu(hSubMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, lpClickPoint.x, lpClickPoint.y, 0, globals.window, NULL);
        SendMessageW(globals.window, WM_NULL, 0, 0);

        DestroyMenu(hSubMenu);
        DestroyMenu(hMenu);
    }
    return true;
}


bool OnCommand(WORD value)
{
    switch (value)
    {
        case ID_POPUP_EXIT:
            DestroyWindow(globals.window);
            break;
    }
    return true;
}


LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    static UINT msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    if (message == msgTaskbarCreated)
    {
        if (!IconCreate())
        {
            MessageBoxW(nullptr, L"Systray Icon Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
            DestroyWindow(globals.window);
            return -1;
        }
    }

    switch (message)
    {
        case WM_DESTROY:
            IconRemove();
            PostQuitMessage(0);
            break;

        case WM_CLOSE:
            DestroyWindow(globals.window);
            break;

        case WM_USER_SHELLICON:
            if (!OnShellIcon(LOWORD(lparam))) return -1;
            break;

        case WM_COMMAND:
            if (!OnCommand(LOWORD(wparam))) return -1;
            break;

        default:
            break;

    }

    return DefWindowProcW(window, message, wparam, lparam);
}


int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    std::wstring mutex_name(PROJECT_SHORT_NAME);
    mutex_name.append(L"sfw-");
    mutex_name.append(PROJECT_GUID);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED)
        return 0;

    globals.instance                    = hInstance;

    std::wstring app_name               = PROJECT_SHORT_NAME + L" Screenshots Folder Watchdog";

    globals.window_class.cbSize         = sizeof(WNDCLASSEX);
    globals.window_class.style          = CS_HREDRAW | CS_VREDRAW;
    globals.window_class.lpfnWndProc    = WndProc;
    globals.window_class.cbClsExtra     = 0;
    globals.window_class.cbWndExtra     = 0;
    globals.window_class.hInstance      = hInstance;
    globals.window_class.hIcon          = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_GWSFW));
    globals.window_class.hCursor        = LoadCursorW(NULL, IDC_ARROW);
    globals.window_class.hbrBackground  = (HBRUSH)GetStockObject(WHITE_BRUSH);
    globals.window_class.lpszMenuName   = nullptr;
    globals.window_class.lpszClassName  = app_name.c_str();
    globals.window_class.hIconSm        = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_GWSFW));
    if (!RegisterClassExW(&globals.window_class))
    {
        MessageBoxW(nullptr, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    globals.window = CreateWindowExW(WS_EX_CLIENTEDGE, app_name.c_str(), app_name.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr);

    if (globals.window == nullptr)
    {
        MessageBoxW(nullptr, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    IconSetTip(L"Initializing...");

    if (!IconCreate())
    {
        MessageBoxW(nullptr, L"Systray Icon Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    std::wstring screenshots_folder = FindScreenshots();
    if (screenshots_folder.empty())
        return 0;

    globals.watchdog.start(screenshots_folder);

    MSG message;

    for (;;)
    {
        if (globals.watchdog.user_data_changed.load())
        {
            globals.watchdog.user_data_mutex.lock();
            watchdog_t::user_data_t ud = globals.watchdog.user_data;
            globals.watchdog.user_data_mutex.unlock();
            globals.watchdog.user_data_changed.store(false);

            std::wstring tip(L"Watching: ");
            tip += screenshots_folder + L"\n";
            tip += std::to_wstring(ud.screenshots) + L" screens (" + std::to_wstring(ud.percentage) + L"% full)";
            IconSetTip(tip);

            // for testing:
            // if (ud.full || ud.screenshots >= 10)

            if (ud.full)
                IconNotify(backup(screenshots_folder));
        }

        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        else
            SleepEx(1, TRUE);
    }

    globals.watchdog.stop();

    return (int)message.wParam;
}
