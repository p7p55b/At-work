#include <windows.h>
#include "gui.hpp"

int WINAPI wWinMain(HINSTANCE h_inst, HINSTANCE /*h_prv*/, PWSTR /*p_cmd*/, int cmd_shw) {
    try {
        HANDLE mtx = CreateMutexW(nullptr, FALSE, L"AtWork_SingleInstance_Mutex_v2");
        if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
            DWORD wt_res = WaitForSingleObject(mtx, 0);
            if (wt_res != WAIT_OBJECT_0 && wt_res != WAIT_ABANDONED) {
                HWND wnd = FindWindowW(L"AtWorkWndClass", L"At-Work");
                if (wnd) {
                    ShowWindow(wnd, SW_RESTORE);
                    SetForegroundWindow(wnd);
                }
                CloseHandle(mtx);
                return 0;
            }
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
