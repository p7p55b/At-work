#include "config.hpp"
#include <windows.h>
#include <vector>

namespace cfg {

static std::wstring to_wd(const std::string& s) {
    if (s.empty()) return L"";
    int cnt = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (cnt <= 0) return L"";
    std::wstring r(cnt, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &r[0], cnt);
    return r;
}

static std::string to_utf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int cnt = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (cnt <= 0) return "";
    std::string r(cnt, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), &r[0], cnt, nullptr, nullptr);
    return r;
}

static std::string rd_ini_str(const wchar_t* sct, const wchar_t* k, const std::string& dflt, const std::wstring& pth) {
    wchar_t buf[2048] = {};
    std::wstring wd = to_wd(dflt);
    GetPrivateProfileStringW(sct, k, wd.c_str(), buf, ARRAYSIZE(buf), pth.c_str());
    return to_utf8(buf);
}

static void wrt_ini_str(const wchar_t* sct, const wchar_t* k, const std::string& v, const std::wstring& pth) {
    std::wstring wv = to_wd(v);
    WritePrivateProfileStringW(sct, k, wv.c_str(), pth.c_str());
}

std::wstring CfgMgr::get_pth() {
    wchar_t pth[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, pth, MAX_PATH);
    wchar_t* p = wcsrchr(pth, L'\\');
    if (p) {
        *(p + 1) = L'\0';
    }
    return std::wstring(pth) + L"config.ini";
}

AppCfg CfgMgr::crt_dflt() {
    AppCfg c;
    c.client_id = "1559498097938407464";
    c.ical_url = "";
    c.auto_sync = true;
    c.sync_interval_seconds = 60;

    c.custom_details = "En service";
    c.custom_state = "Actif";
    c.custom_time_from = "09:00";
    c.custom_time_to = "18:00";
    c.custom_time_range = "from 09:00 to 18:00";
    c.custom_large_image = "https://media.giphy.com/media/unQ3IJU2RG7DO/giphy.gif";
    c.custom_small_image = "";

    c.presets = {
        {
            "taff",
            "Taff",
            "Au travail",
            "En poste",
            "09:00",
            "18:00",
            "from 09:00 to 18:00",
            "https://media.giphy.com/media/unQ3IJU2RG7DO/giphy.gif",
            "Taff",
            "",
            ""
        },
        {
            "meeting",
            "Meeting",
            "En reunion",
            "Ne pas deranger",
            "14:00",
            "15:00",
            "from 14:00 to 15:00",
            "https://media.giphy.com/media/ule4akeXnY9fb4xFWU/giphy.gif",
            "Meeting",
            "",
            ""
        },
        {
            "absence",
            "Absence",
            "Parti chercher de l'argent",
            "De retour bientot",
            "12:00",
            "14:00",
            "from 12:00 to 14:00",
            "https://media.giphy.com/media/67ThRZlYBvibtdF9UC/giphy.gif",
            "Absence",
            "",
            ""
        },
        {
            "focus",
            "Focus",
            "Focus developpement",
            "Concentration",
            "20:00",
            "23:00",
            "from 20:00 to 23:00",
            "https://media.giphy.com/media/LmN8OYiY4m0X85K0Zz/giphy.gif",
            "Focus",
            "",
            ""
        }
    };

    return c;
}

AppCfg CfgMgr::ld() {
    std::wstring pth = get_pth();

    DWORD attr = GetFileAttributesW(pth.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        AppCfg dflt = crt_dflt();
        sv(dflt);
        return dflt;
    }

    AppCfg c;
    std::string s_stp = rd_ini_str(L"System", L"setup_completed", "0", pth);
    c.setup_completed = (s_stp == "1");
    c.autostart = chk_tstrt();

    c.client_id = rd_ini_str(L"Discord", L"client_id", "", pth);
    c.ical_url  = rd_ini_str(L"Calendar", L"ical_url", "", pth);

    if (!c.client_id.empty() && !c.setup_completed) {
        c.setup_completed = true;
    }

    std::string s_sync = rd_ini_str(L"Calendar", L"auto_sync", "1", pth);
    c.auto_sync = (s_sync == "1");

    std::string s_sec = rd_ini_str(L"Calendar", L"sync_interval_seconds", "60", pth);
    try {
        c.sync_interval_seconds = (std::max)(10, std::stoi(s_sec));
    } catch (...) {
        c.sync_interval_seconds = 60;
    }

    c.custom_details     = rd_ini_str(L"Custom", L"details", "Au travail", pth);
    c.custom_state       = rd_ini_str(L"Custom", L"state", "En poste", pth);
    c.custom_time_from   = rd_ini_str(L"Custom", L"time_from", "09:00", pth);
    c.custom_time_to     = rd_ini_str(L"Custom", L"time_to", "18:00", pth);
    c.custom_time_range  = rd_ini_str(L"Custom", L"time_range", "from " + c.custom_time_from + " to " + c.custom_time_to, pth);
    c.custom_large_image = rd_ini_str(L"Custom", L"large_image", "https://media.giphy.com/media/unQ3IJU2RG7DO/giphy.gif", pth);
    c.custom_small_image = rd_ini_str(L"Custom", L"small_image", "", pth);

    std::string s_cnt = rd_ini_str(L"Presets", L"count", "0", pth);
    int cnt = 0;
    try {
        cnt = std::stoi(s_cnt);
    } catch (...) {
        cnt = 0;
    }

    if (cnt <= 0) {
        AppCfg dflt = crt_dflt();
        c.presets = dflt.presets;
        sv(c);
    } else {
        for (int i = 0; i < cnt; ++i) {
            std::wstring sct = L"Preset_" + std::to_wstring(i);
            Prst p;
            p.id          = rd_ini_str(sct.c_str(), L"id", "preset_" + std::to_string(i), pth);
            p.label       = rd_ini_str(sct.c_str(), L"label", "Preset " + std::to_string(i), pth);
            p.details     = rd_ini_str(sct.c_str(), L"details", "", pth);
            p.state       = rd_ini_str(sct.c_str(), L"state", "", pth);
            p.time_from   = rd_ini_str(sct.c_str(), L"time_from", "09:00", pth);
            p.time_to     = rd_ini_str(sct.c_str(), L"time_to", "18:00", pth);
            p.time_range  = rd_ini_str(sct.c_str(), L"time_range", "from " + p.time_from + " to " + p.time_to, pth);
            p.large_image = rd_ini_str(sct.c_str(), L"large_image", "", pth);
            p.large_text  = rd_ini_str(sct.c_str(), L"large_text", "", pth);
            p.small_image = rd_ini_str(sct.c_str(), L"small_image", "", pth);
            p.small_text  = rd_ini_str(sct.c_str(), L"small_text", "", pth);
            c.presets.push_back(p);
        }
    }

    return c;
}

bool CfgMgr::sv(const AppCfg& c) {
    std::wstring pth = get_pth();

    wrt_ini_str(L"System", L"setup_completed", c.setup_completed ? "1" : "0", pth);
    wrt_ini_str(L"Discord", L"client_id", c.client_id, pth);
    wrt_ini_str(L"Calendar", L"ical_url", c.ical_url, pth);
    wrt_ini_str(L"Calendar", L"auto_sync", c.auto_sync ? "1" : "0", pth);
    wrt_ini_str(L"Calendar", L"sync_interval_seconds", std::to_string(c.sync_interval_seconds), pth);

    wrt_ini_str(L"Custom", L"details", c.custom_details, pth);
    wrt_ini_str(L"Custom", L"state", c.custom_state, pth);
    wrt_ini_str(L"Custom", L"time_from", c.custom_time_from, pth);
    wrt_ini_str(L"Custom", L"time_to", c.custom_time_to, pth);
    wrt_ini_str(L"Custom", L"time_range", "from " + c.custom_time_from + " to " + c.custom_time_to, pth);
    wrt_ini_str(L"Custom", L"large_image", c.custom_large_image, pth);
    wrt_ini_str(L"Custom", L"small_image", c.custom_small_image, pth);

    wrt_ini_str(L"Presets", L"count", std::to_string(c.presets.size()), pth);
    for (size_t i = 0; i < c.presets.size(); ++i) {
        std::wstring sct = L"Preset_" + std::to_wstring(i);
        const auto& p = c.presets[i];
        wrt_ini_str(sct.c_str(), L"id", p.id, pth);
        wrt_ini_str(sct.c_str(), L"label", p.label, pth);
        wrt_ini_str(sct.c_str(), L"details", p.details, pth);
        wrt_ini_str(sct.c_str(), L"state", p.state, pth);
        wrt_ini_str(sct.c_str(), L"time_from", p.time_from, pth);
        wrt_ini_str(sct.c_str(), L"time_to", p.time_to, pth);
        wrt_ini_str(sct.c_str(), L"time_range", "from " + p.time_from + " to " + p.time_to, pth);
        wrt_ini_str(sct.c_str(), L"large_image", p.large_image, pth);
        wrt_ini_str(sct.c_str(), L"large_text", p.large_text, pth);
        wrt_ini_str(sct.c_str(), L"small_image", p.small_image, pth);
        wrt_ini_str(sct.c_str(), L"small_text", p.small_text, pth);
    }

    return true;
}

bool CfgMgr::chk_tstrt() {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS) {
        return false;
    }
    DWORD tp = 0;
    DWORD sz = 0;
    LONG res = RegQueryValueExW(hk, L"At-Work", nullptr, &tp, nullptr, &sz);
    RegCloseKey(hk);
    return (res == ERROR_SUCCESS);
}

bool CfgMgr::set_tstrt(bool en) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS) {
        return false;
    }
    RegDeleteValueW(hk, L"DiscordWorkRPC");
    if (en) {
        wchar_t pth[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, pth, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(pth) + L"\"";
        LONG res = RegSetValueExW(hk, L"At-Work", 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(cmd.c_str()),
                                  static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hk);
        return (res == ERROR_SUCCESS);
    } else {
        RegDeleteValueW(hk, L"At-Work");
        RegCloseKey(hk);
        return true;
    }
}

} // namespace cfg
