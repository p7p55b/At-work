#include <windows.h>
#include "gui.hpp"

int WINAPI wWinMain(HINSTANCE h_inst, HINSTANCE /*h_prv*/, PWSTR /*p_cmd*/, int cmd_shw) {
    try {
        HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\AtWork_SingleInstance_Mutex");
        DWORD err = GetLastError();
        HWND existing_wnd = FindWindowW(L"AtWorkWndClass", nullptr);
        if (!mtx || err == ERROR_ALREADY_EXISTS || err == ERROR_ACCESS_DENIED || existing_wnd != nullptr) {
            if (!existing_wnd) {
                existing_wnd = FindWindowW(L"DiscordWorkRpcWndClass", nullptr);
            }
            if (existing_wnd) {
                ShowWindow(existing_wnd, SW_SHOW);
                ShowWindow(existing_wnd, SW_RESTORE);
                SetForegroundWindow(existing_wnd);
                PostMessageW(existing_wnd, WM_USER + 1, 0, WM_LBUTTONDBLCLK);
            }
            if (mtx) {
                CloseHandle(mtx);
            }
            return 0;
        }

        ui::MainWindow wnd;
        if (!wnd.create(h_inst, (cmd_shw == 0) ? SW_SHOW : cmd_shw)) {
            if (mtx) {
                ReleaseMutex(mtx);
                CloseHandle(mtx);
            }
            return 1;
        }

        int r = wnd.run_message_loop();

        if (mtx) {
            ReleaseMutex(mtx);
            CloseHandle(mtx);
        }

        return r;
    } catch (...) {
        return 1;
    }
}
