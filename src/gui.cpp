#include "gui.hpp"
#include <windowsx.h>
#include <commctrl.h>
#include <winhttp.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

namespace ui {

constexpr UINT WM_TRAY_NOTIFY = WM_APP + 1;
constexpr UINT WM_CALENDAR_SYNC_DONE = WM_APP + 2;
constexpr UINT WM_IMAGE_DOWNLOADED = WM_APP + 3;
constexpr UINT WM_USER_PROFILE_UPDATED = WM_APP + 4;
constexpr UINT_PTR TIMER_ID_CALENDAR = 1001;
constexpr UINT_PTR TIMER_ID_TICK     = 1002;
constexpr UINT_PTR TIMER_ID_ANIM     = 1003;

struct ImgDwnldRes {
    MainWindow::ImageType type = MainWindow::ImageType::Avatar;
    std::string url;
    std::vector<uint8_t> data;

    MainWindow::ImageType tp() const { return type; }
    const std::vector<uint8_t>& dt() const { return data; }
};
using ImageDownloadResult = ImgDwnldRes;

static COLORREF ext_dmnnt_clr(Gdiplus::Bitmap* bmp) {
    if (!bmp) return RGB(35, 39, 42);
    int w = bmp->GetWidth();
    int h = bmp->GetHeight();
    if (w <= 0 || h <= 0) return RGB(35, 39, 42);

    long long ttl_r = 0, ttl_g = 0, ttl_b = 0;
    int cnt = 0;
    int bst_scr = -1;
    COLORREF bst_clr = RGB(35, 39, 42);

    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            Gdiplus::Color col;
            bmp->GetPixel(x, y, &col);
            if (col.GetA() < 128) continue;

            int r = col.GetR();
            int g = col.GetG();
            int b = col.GetB();

            ttl_r += r;
            ttl_g += g;
            ttl_b += b;
            cnt++;

            int mx_c = (std::max)({r, g, b});
            int mn_c = (std::min)({r, g, b});
            int df = mx_c - mn_c;

            if (mx_c < 30 || mn_c > 235) continue;

            if (df > bst_scr) {
                bst_scr = df;
                bst_clr = RGB(r, g, b);
            }
        }
    }

    if (bst_scr >= 20) {
        return bst_clr;
    }
    if (cnt > 0) {
        return RGB(static_cast<BYTE>(ttl_r / cnt),
                   static_cast<BYTE>(ttl_g / cnt),
                   static_cast<BYTE>(ttl_b / cnt));
    }
    return RGB(35, 39, 42);
}

// Navigation
constexpr int ID_TAB_ACTIVITY      = 2010;
constexpr int ID_TAB_DEBUG         = 2011;

// View 1 (Activity) IDs
constexpr int ID_BTN_CONNECT       = 2001;
constexpr int ID_BTN_SYNC_NOW      = 2003;
constexpr int ID_BTN_CLEAR_ACT     = 2004;
constexpr int ID_BTN_APPLY_CUSTOM  = 2005;
constexpr int ID_BTN_SAVE_PRESET   = 2006;
constexpr int ID_BTN_DELETE_PRESET = 2007;
constexpr int ID_BTN_PRESET_BASE   = 2100;

// View 2 (Debug & Settings) IDs
constexpr int ID_DBG_TEST_IPC      = 2201;
constexpr int ID_DBG_RECONNECT     = 2202;
constexpr int ID_DBG_DISCONNECT    = 2203;
constexpr int ID_DBG_TEST_CAL      = 2204;
constexpr int ID_DBG_FORCE_SYNC    = 2205;
constexpr int ID_DBG_SAVE_CONFIG   = 2206;
constexpr int ID_DBG_CLEAR_LOG     = 2207;

// Tray IDs
constexpr int ID_TRAY_OPEN         = 3001;
constexpr int ID_TRAY_SETTINGS     = 3002;
constexpr int ID_TRAY_SYNC         = 3003;
constexpr int ID_TRAY_CLEAR        = 3004;
constexpr int ID_TRAY_EXIT         = 3005;

struct CalSyncRes {
    bool success = false;
    std::string summary;
    std::time_t end_time = 0;

    bool ok() const { return success; }
    const std::string& smm() const { return summary; }
    std::time_t tm_e() const { return end_time; }
};
using CalendarSyncResult = CalSyncRes;

static std::wstring u8_to_wd(const std::string& s) {
    if (s.empty()) return L"";
    int cnt = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (cnt <= 0) return L"";
    std::wstring r(cnt, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &r[0], cnt);
    return r;
}

static std::string wd_to_u8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int cnt = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (cnt <= 0) return "";
    std::string r(cnt, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), &r[0], cnt, nullptr, nullptr);
    return r;
}

static std::string get_wnd_txt_u8(HWND hwnd) {
    int ln = GetWindowTextLengthW(hwnd);
    if (ln <= 0) return "";
    std::wstring wbuf(ln + 1, L'\0');
    GetWindowTextW(hwnd, &wbuf[0], ln + 1);
    wbuf.resize(ln);
    return wd_to_u8(wbuf);
}

inline static std::wstring utf8_to_wide(const std::string& s) { return u8_to_wd(s); }
inline static std::string wide_to_utf8(const std::wstring& ws) { return wd_to_u8(ws); }
inline static std::string get_window_text_utf8(HWND hwnd) { return get_wnd_txt_u8(hwnd); }

static std::string rslv_img_url(const std::string& url) {
    if (url.empty()) return "";
    if (url.find("giphy.com/gifs/") != std::string::npos && url.find("/media/") == std::string::npos) {
        size_t p_slsh = url.find_last_of('/');
        if (p_slsh != std::string::npos) {
            std::string sgm = url.substr(p_slsh + 1);
            size_t p_q = sgm.find('?');
            if (p_q != std::string::npos) sgm = sgm.substr(0, p_q);
            size_t p_dsh = sgm.find_last_of('-');
            std::string id = (p_dsh != std::string::npos) ? sgm.substr(p_dsh + 1) : sgm;
            if (!id.empty()) {
                return "https://media.giphy.com/media/" + id + "/giphy.gif";
            }
        }
    }
    return url;
}

static std::string resolve_image_url(const std::string& url) {
    return rslv_img_url(url);
}

static bool dwnld_url_bts(const std::string& url, std::vector<uint8_t>& out_bts) {
    out_bts.clear();
    if (url.empty()) return false;

    int w_ln = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (w_ln <= 0) return false;
    std::wstring w_url(w_ln, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &w_url[0], w_ln);

    URL_COMPONENTS cmp = {};
    cmp.dwStructSize = sizeof(cmp);
    wchar_t hst[256] = {};
    wchar_t pth[2048] = {};
    cmp.lpszHostName = hst;
    cmp.dwHostNameLength = ARRAYSIZE(hst);
    cmp.lpszUrlPath = pth;
    cmp.dwUrlPathLength = ARRAYSIZE(pth);

    if (!WinHttpCrackUrl(w_url.c_str(), 0, 0, &cmp)) return false;

    HINTERNET ssn = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) At-Work/1.0",
                                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ssn) return false;

    HINTERNET cn = WinHttpConnect(ssn, hst, cmp.nPort, 0);
    if (!cn) {
        WinHttpCloseHandle(ssn);
        return false;
    }

    DWORD flgs = (cmp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET rq = WinHttpOpenRequest(cn, L"GET", pth, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flgs);
    if (!rq) {
        WinHttpCloseHandle(cn);
        WinHttpCloseHandle(ssn);
        return false;
    }

    DWORD rdr = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(rq, WINHTTP_OPTION_REDIRECT_POLICY, &rdr, sizeof(rdr));

    if (!WinHttpSendRequest(rq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(rq, nullptr)) {
        WinHttpCloseHandle(rq);
        WinHttpCloseHandle(cn);
        WinHttpCloseHandle(ssn);
        return false;
    }

    DWORD sc = 0;
    DWORD sc_sz = sizeof(sc);
    if (WinHttpQueryHeaders(rq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sc_sz, WINHTTP_NO_HEADER_INDEX)) {
        if (sc != 200) {
            WinHttpCloseHandle(rq);
            WinHttpCloseHandle(cn);
            WinHttpCloseHandle(ssn);
            return false;
        }
    }

    DWORD b_rd = 0;
    std::vector<uint8_t> chnk(8192);
    while (WinHttpReadData(rq, chnk.data(), static_cast<DWORD>(chnk.size()), &b_rd) && b_rd > 0) {
        if (out_bts.size() + b_rd > 15 * 1024 * 1024) break;
        out_bts.insert(out_bts.end(), chnk.data(), chnk.data() + b_rd);
    }

    WinHttpCloseHandle(rq);
    WinHttpCloseHandle(cn);
    WinHttpCloseHandle(ssn);
    return !out_bts.empty();
}

static bool download_url_bytes(const std::string& url, std::vector<uint8_t>& out_bytes) {
    return dwnld_url_bts(url, out_bytes);
}

struct LdImgDt {
    Gdiplus::Bitmap* bitmap = nullptr;
    IStream* stream = nullptr;
    UINT frame_count = 1;

    Gdiplus::Bitmap* bmp() const { return bitmap; }
    IStream* strm() const { return stream; }
    UINT frm_cnt() const { return frame_count; }
};
using LoadedImageData = LdImgDt;

static LdImgDt ld_img_bts(const std::vector<uint8_t>& bts) {
    LdImgDt r;
    if (bts.empty()) return r;
    HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, bts.size());
    if (!h_mem) return r;
    void* p_mem = GlobalLock(h_mem);
    if (!p_mem) {
        GlobalFree(h_mem);
        return r;
    }
    std::memcpy(p_mem, bts.data(), bts.size());
    GlobalUnlock(h_mem);

    IStream* p_strm = nullptr;
    if (CreateStreamOnHGlobal(h_mem, TRUE, &p_strm) != S_OK) {
        GlobalFree(h_mem);
        return r;
    }

    Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromStream(p_strm);
    if (src && src->GetLastStatus() == Gdiplus::Ok) {
        r.bitmap = src;
        r.stream = p_strm;
        GUID dim = Gdiplus::FrameDimensionTime;
        r.frame_count = src->GetFrameCount(&dim);
        if (r.frame_count == 0) r.frame_count = 1;
    } else {
        if (src) delete src;
        p_strm->Release();
    }
    return r;
}

static void fr_ld_img(Gdiplus::Bitmap*& bmp, IStream*& strm) {
    if (bmp) {
        delete bmp;
        bmp = nullptr;
    }
    if (strm) {
        strm->Release();
        strm = nullptr;
    }
}

static Gdiplus::Bitmap* ld_bmp_bts(const std::vector<uint8_t>& bts) {
    LdImgDt img = ld_img_bts(bts);
    if (!img.bitmap) return nullptr;
    int w = img.bitmap->GetWidth();
    int h = img.bitmap->GetHeight();
    Gdiplus::Bitmap* stndln = nullptr;
    if (w > 0 && h > 0) {
        stndln = new Gdiplus::Bitmap(w, h, PixelFormat32bppARGB);
        Gdiplus::Graphics g(stndln);
        g.DrawImage(img.bitmap, 0, 0, w, h);
    }
    fr_ld_img(img.bitmap, img.stream);
    return stndln;
}

inline static COLORREF extract_dominant_color(Gdiplus::Bitmap* bmp) { return ext_dmnnt_clr(bmp); }
inline static LdImgDt load_image_from_bytes(const std::vector<uint8_t>& bts) { return ld_img_bts(bts); }
inline static void free_loaded_image(Gdiplus::Bitmap*& bmp, IStream*& strm) { fr_ld_img(bmp, strm); }
inline static Gdiplus::Bitmap* load_bitmap_from_bytes(const std::vector<uint8_t>& bts) { return ld_bmp_bts(bts); }

static bool prs_tm_sngl(const std::string& s, int& out_h, int& out_m) {
    out_h = 0;
    out_m = 0;
    std::string digits;
    for (char c : s) {
        if (c >= '0' && c <= '9') digits.push_back(c);
    }
    if (s.find(':') == std::string::npos && s.find('h') == std::string::npos && s.find('H') == std::string::npos) {
        if (digits.size() == 4) {
            out_h = std::stoi(digits.substr(0, 2));
            out_m = std::stoi(digits.substr(2, 2));
            return (out_h >= 0 && out_h <= 23 && out_m >= 0 && out_m <= 59);
        } else if (digits.size() == 3) {
            out_h = std::stoi(digits.substr(0, 1));
            out_m = std::stoi(digits.substr(1, 2));
            return (out_h >= 0 && out_h <= 23 && out_m >= 0 && out_m <= 59);
        }
    }
    std::regex tm_ptrn(R"((\d{1,2})(?:[:hH](\d{1,2}))?)");
    std::smatch mtch;
    if (std::regex_search(s, mtch, tm_ptrn)) {
        out_h = std::stoi(mtch[1].str());
        if (mtch[2].matched && !mtch[2].str().empty()) {
            out_m = std::stoi(mtch[2].str());
        }
        return (out_h >= 0 && out_h <= 23 && out_m >= 0 && out_m <= 59);
    }
    return false;
}

static std::string normalize_time_str(const std::string& s, const std::string& fallback) {
    int h = 0, m = 0;
    if (!prs_tm_sngl(s, h, m)) return fallback;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return std::string(buf);
}

inline static bool parse_time_single(const std::string& input, int& out_h, int& out_m) {
    return prs_tm_sngl(input, out_h, out_m);
}

static bool prs_tm_prts(const std::string& s_frm, const std::string& s_to, int64_t& out_s, int64_t& out_e) {
    out_s = 0;
    out_e = 0;
    std::time_t nw = std::time(nullptr);

    int h1 = 0, m1 = 0;
    int h2 = 0, m2 = 0;
    bool has_frm = prs_tm_sngl(s_frm, h1, m1);
    bool has_to  = prs_tm_sngl(s_to, h2, m2);

    if (!has_frm || !has_to) {
        return false;
    }

    std::tm tm_nw{};
    localtime_s(&tm_nw, &nw);

    std::tm tm_s = tm_nw;
    tm_s.tm_hour = h1;
    tm_s.tm_min = m1;
    tm_s.tm_sec = 0;
    std::time_t t_s = std::mktime(&tm_s);

    std::tm tm_e = tm_nw;
    tm_e.tm_hour = h2;
    tm_e.tm_min = m2;
    tm_e.tm_sec = 0;
    std::time_t t_e = std::mktime(&tm_e);

    if (h1 < h2 || (h1 == h2 && m1 < m2)) {
        // Plage en journee (ex: 09:00 -> 18:00)
        if (nw >= t_s && nw < t_e) {
            out_s = 0;
            out_e = static_cast<int64_t>(t_e);
            return true;
        }
        return false;
    } else if (h1 > h2 || (h1 == h2 && m1 > m2)) {
        // Plage de nuit passant minuit (ex: 22:00 -> 02:00)
        if (nw >= t_s) {
            out_s = 0;
            out_e = static_cast<int64_t>(t_e + 24 * 3600);
            return true;
        } else if (nw < t_e) {
            out_s = 0;
            out_e = static_cast<int64_t>(t_e);
            return true;
        }
        return false;
    }

    return false;
}

static bool parse_time_parts(const std::string& from_str, const std::string& to_str, int64_t& out_start, int64_t& out_end) {
    return prs_tm_prts(from_str, to_str, out_start, out_end);
}

MainWindow::MainWindow()
    : m_hwnd(nullptr)
    , m_instance(nullptr)
    , m_current_view(ViewMode::Activity)
    , m_brush_bg(nullptr)
    , m_brush_pane(nullptr)
    , m_brush_mid(nullptr)
    , m_brush_accent(nullptr)
    , m_brush_online(nullptr)
    , m_brush_offline(nullptr)
    , m_pen_line(nullptr)
    , m_pen_mid(nullptr)
    , m_pen_online_glow(nullptr)
    , m_pen_null(nullptr)
    , m_font_ui(nullptr)
    , m_font_title(nullptr)
    , m_font_section(nullptr)
    , m_font_mono(nullptr)
    , m_font_log(nullptr)
    , m_btn_tab_activity(nullptr)
    , m_btn_tab_debug(nullptr)
    , m_label_discord_account(nullptr)
    , m_btn_reconnect(nullptr)
    , m_label_rpc_status(nullptr)
    , m_edit_ical_url(nullptr)
    , m_chk_auto_sync(nullptr)
    , m_btn_sync_now(nullptr)
    , m_label_cal_status(nullptr)
    , m_btn_presets{}
    , m_btn_clear_activity(nullptr)
    , m_label_preset_name(nullptr)
    , m_edit_preset_name(nullptr)
    , m_label_details(nullptr)
    , m_edit_details(nullptr)
    , m_label_state(nullptr)
    , m_edit_state(nullptr)
    , m_label_large_img(nullptr)
    , m_edit_large_img(nullptr)
    , m_label_small_img(nullptr)
    , m_edit_small_img(nullptr)
    , m_label_from(nullptr)
    , m_edit_time_from(nullptr)
    , m_label_to(nullptr)
    , m_edit_time_to(nullptr)
    , m_btn_apply_custom(nullptr)
    , m_btn_save_preset(nullptr)
    , m_btn_delete_preset(nullptr)
    , m_dbg_label_pipe(nullptr)
    , m_dbg_label_user(nullptr)
    , m_dbg_edit_client_id(nullptr)
    , m_dbg_btn_test_ipc(nullptr)
    , m_dbg_btn_reconnect(nullptr)
    , m_dbg_btn_disconnect(nullptr)
    , m_dbg_label_ipc_status(nullptr)
    , m_dbg_edit_ical_url(nullptr)
    , m_dbg_edit_interval(nullptr)
    , m_dbg_chk_auto_sync(nullptr)
    , m_dbg_btn_test_cal(nullptr)
    , m_dbg_btn_force_sync(nullptr)
    , m_dbg_label_cal_status(nullptr)
    , m_dbg_chk_autostart(nullptr)
    , m_dbg_btn_save(nullptr)
    , m_dbg_edit_log(nullptr)
    , m_dbg_btn_clear_log(nullptr)
    , m_tray_data{}
    , m_tray_icon_added(false)
    , m_sync_in_progress(false)
    , m_rpc_is_online(false)
    , m_cal_is_active(false)
    , m_show_preview(true)
    , m_activity_start_time(0)
    , m_activity_end_time(0)
    , m_font_discord_header(nullptr)
    , m_font_discord_app(nullptr)
    , m_font_discord_sub(nullptr)
    , m_font_badge(nullptr)
    , m_brush_discord_card(nullptr)
    , m_brush_discord_banner(nullptr)
    , m_brush_discord_asset(nullptr)
    , m_pen_discord_line(nullptr)
    , m_gdiplus_token(0)
    , m_avatar_bitmap(nullptr)
    , m_current_avatar_url("")
    , m_avatar_loading(false)
    , m_decor_bitmap(nullptr)
    , m_current_decor_url("")
    , m_decor_loading(false)
    , m_banner_bitmap(nullptr)
    , m_banner_stream(nullptr)
    , m_current_banner_url("")
    , m_banner_loading(false)
    , m_banner_frame_index(0)
    , m_banner_frame_count(1)
    , m_asset_bitmap(nullptr)
    , m_asset_stream(nullptr)
    , m_current_asset_url("")
    , m_asset_loading(false)
    , m_asset_frame_index(0)
    , m_asset_frame_count(1)
    , m_dominant_banner_color(RGB(32, 34, 37))
    , m_has_dominant_banner_color(false)
    , m_last_profile_lookup_id("")
    , m_profile_lookup_in_progress(false)
{
    Gdiplus::GdiplusStartupInput gdi_in;
    Gdiplus::GdiplusStartup(&m_gdiplus_token, &gdi_in, nullptr);

    m_config = config::ConfigManager::load();
    if (!m_config.setup_completed) {
        m_current_view = ViewMode::DebugSettings;
    }
}

MainWindow::~MainWindow() {
    remove_tray_icon();
    cleanup_theme_resources();
    if (m_avatar_bitmap) {
        delete m_avatar_bitmap;
        m_avatar_bitmap = nullptr;
    }
    if (m_decor_bitmap) {
        delete m_decor_bitmap;
        m_decor_bitmap = nullptr;
    }
    free_loaded_image(m_banner_bitmap, m_banner_stream);
    free_loaded_image(m_asset_bitmap, m_asset_stream);
    if (m_gdiplus_token) {
        Gdiplus::GdiplusShutdown(m_gdiplus_token);
        m_gdiplus_token = 0;
    }
}

void MainWindow::init_thm_rsrc() {
    m_brush_bg      = CreateSolidBrush(theme::COLOR_BG);
    m_brush_pane    = CreateSolidBrush(theme::COLOR_PANE);
    m_brush_mid     = CreateSolidBrush(theme::COLOR_MID);
    m_brush_accent  = CreateSolidBrush(theme::COLOR_ACCENT);
    m_brush_online  = CreateSolidBrush(theme::COLOR_ONLINE);
    m_brush_offline = CreateSolidBrush(theme::COLOR_OFFLINE);

    m_pen_line        = CreatePen(PS_SOLID, 1, theme::COLOR_LINE);
    m_pen_mid         = CreatePen(PS_SOLID, 1, theme::COLOR_MID);
    m_pen_online_glow = CreatePen(PS_SOLID, 1, theme::COLOR_ONLINE);
    m_pen_null        = CreatePen(PS_NULL, 0, 0);

    m_font_ui = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_font_title = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_font_section = CreateFontW(-11, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_font_mono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    m_font_log = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    m_brush_discord_card   = CreateSolidBrush(RGB(17, 18, 20));
    m_brush_discord_banner = CreateSolidBrush(RGB(35, 39, 42));
    m_brush_discord_asset  = CreateSolidBrush(RGB(43, 45, 49));
    m_pen_discord_line     = CreatePen(PS_SOLID, 1, RGB(43, 45, 49));

    m_font_discord_header = CreateFontW(-10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_font_discord_app    = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_font_discord_sub    = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_font_badge          = CreateFontW(-10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void MainWindow::cln_thm_rsrc() {
    if (m_brush_bg)      DeleteObject(m_brush_bg);
    if (m_brush_pane)    DeleteObject(m_brush_pane);
    if (m_brush_mid)     DeleteObject(m_brush_mid);
    if (m_brush_accent)  DeleteObject(m_brush_accent);
    if (m_brush_online)  DeleteObject(m_brush_online);
    if (m_brush_offline) DeleteObject(m_brush_offline);

    if (m_brush_discord_card)   DeleteObject(m_brush_discord_card);
    if (m_brush_discord_banner) DeleteObject(m_brush_discord_banner);
    if (m_brush_discord_asset)  DeleteObject(m_brush_discord_asset);
    if (m_pen_discord_line)     DeleteObject(m_pen_discord_line);

    if (m_pen_line)        DeleteObject(m_pen_line);
    if (m_pen_mid)         DeleteObject(m_pen_mid);
    if (m_pen_online_glow) DeleteObject(m_pen_online_glow);
    if (m_pen_null)        DeleteObject(m_pen_null);

    if (m_font_ui)      DeleteObject(m_font_ui);
    if (m_font_title)   DeleteObject(m_font_title);
    if (m_font_section) DeleteObject(m_font_section);
    if (m_font_mono)    DeleteObject(m_font_mono);
    if (m_font_log)     DeleteObject(m_font_log);

    if (m_font_discord_header) DeleteObject(m_font_discord_header);
    if (m_font_discord_app)    DeleteObject(m_font_discord_app);
    if (m_font_discord_sub)    DeleteObject(m_font_discord_sub);
    if (m_font_badge)          DeleteObject(m_font_badge);
}

bool MainWindow::crt(HINSTANCE instance, int cmd_show) {
    m_instance = instance;

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    init_thm_rsrc();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_prc_thnk;
    wc.hInstance = instance;
    wc.lpszClassName = L"AtWorkWndClass";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }

    int width = 980;
    int height = 800;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = (screen_w - width) / 2;
    int pos_y = (screen_h - height) / 2;

    m_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"At-Work",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        pos_x, pos_y, width, height,
        nullptr, nullptr, instance, this
    );

    if (!m_hwnd) return false;

    init_ctrls(m_hwnd);
    init_try_icn();

    ShowWindow(m_hwnd, cmd_show);
    UpdateWindow(m_hwnd);

    log_dbg("SYS", "Demarrage de At-Work...");

    on_rcnnct_clk();

    if (m_config.auto_sync && !m_config.ical_url.empty()) {
        trg_async_cal_sync();
        SetTimer(m_hwnd, TIMER_ID_CALENDAR, static_cast<UINT>(m_config.sync_interval_seconds * 1000), nullptr);
    }

    SetTimer(m_hwnd, TIMER_ID_TICK, 1000, nullptr);
    SetTimer(m_hwnd, TIMER_ID_ANIM, 100, nullptr);

    return true;
}

void MainWindow::init_ctrls(HWND hwnd) {
    m_activity_controls.clear();
    m_debug_controls.clear();

    // ==========================================
    // BARRE DE NAVIGATION (HEADER)
    // ==========================================
    m_btn_tab_activity = CreateWindowExW(
        0, L"BUTTON", L"[ ACTIVITE ]",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        350, 8, 120, 26, hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_TAB_ACTIVITY)), m_instance, nullptr
    );

    m_btn_tab_debug = CreateWindowExW(
        0, L"BUTTON", L"[ DEBUG & REGLAGES ]",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        480, 8, 150, 26, hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_TAB_DEBUG)), m_instance, nullptr
    );

    // ==========================================
    // VUE 1 : TABLEAU DE BORD D'ACTIVITE
    // ==========================================
    auto add_act = [this](HWND h) { m_activity_controls.push_back(h); return h; };

    // Section 1: Discord RPC
    add_act(CreateWindowExW(0, L"STATIC", L"compte lie :", WS_CHILD | WS_VISIBLE,
                            25, 87, 85, 20, hwnd, nullptr, m_instance, nullptr));

    m_label_discord_account = add_act(CreateWindowExW(0, L"STATIC", L"detection en cours...", WS_CHILD | WS_VISIBLE,
                                                     115, 87, 300, 20, hwnd, nullptr, m_instance, nullptr));

    m_btn_reconnect = add_act(CreateWindowExW(0, L"BUTTON", L"[ RECONNECTER ]", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                              450, 84, 150, 26, hwnd,
                                              reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_CONNECT)), m_instance, nullptr));

    m_label_rpc_status = add_act(CreateWindowExW(0, L"STATIC", L"statut : initialisation", WS_CHILD | WS_VISIBLE,
                                                 45, 126, 550, 20, hwnd, nullptr, m_instance, nullptr));

    // Section 2: Google Calendar
    add_act(CreateWindowExW(0, L"STATIC", L"url ical :", WS_CHILD | WS_VISIBLE,
                            25, 215, 85, 20, hwnd, nullptr, m_instance, nullptr));

    m_edit_ical_url = add_act(CreateWindowExW(0, L"EDIT", utf8_to_wide(m_config.ical_url).c_str(),
                                              WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                              115, 213, 485, 24, hwnd, nullptr, m_instance, nullptr));

    m_chk_auto_sync = add_act(CreateWindowExW(0, L"BUTTON", L"sync automatique (60s)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                              25, 248, 230, 22, hwnd, nullptr, m_instance, nullptr));
    SendMessageW(m_chk_auto_sync, BM_SETCHECK, m_config.auto_sync ? BST_CHECKED : BST_UNCHECKED, 0);

    m_btn_sync_now = add_act(CreateWindowExW(0, L"BUTTON", L"[ SYNCHRONISER ]", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                             450, 247, 150, 24, hwnd,
                                             reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_SYNC_NOW)), m_instance, nullptr));

    m_label_cal_status = add_act(CreateWindowExW(0, L"STATIC", L"agenda : en attente", WS_CHILD | WS_VISIBLE,
                                                 45, 284, 550, 20, hwnd, nullptr, m_instance, nullptr));

    // Section 3: Presets rapides
    m_btn_presets.clear();
    for (size_t i = 0; i < m_config.presets.size(); ++i) {
        std::wstring btn_label = L"[ " + utf8_to_wide(m_config.presets[i].label) + L" ]";
        std::transform(btn_label.begin(), btn_label.end(), btn_label.begin(), ::towupper);

        HWND btn = add_act(CreateWindowExW(
            0, L"BUTTON", btn_label.c_str(),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_PRESET_BASE + i)), m_instance, nullptr
        ));
        m_btn_presets.push_back(btn);
    }

    m_btn_clear_activity = add_act(CreateWindowExW(
        0, L"BUTTON", L"[ EFFACER ]",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 10, 10, hwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_CLEAR_ACT)), m_instance, nullptr
    ));

    // Section 4: Activite personnalisee & statuts
    std::string init_name = m_config.presets.empty() ? "Taff" : m_config.presets[0].label;
    m_label_preset_name = add_act(CreateWindowExW(0, L"STATIC", L"nom statut :", WS_CHILD | WS_VISIBLE,
                            25, 452, 100, 18, hwnd, nullptr, m_instance, nullptr));
    m_edit_preset_name = add_act(CreateWindowExW(0, L"EDIT", utf8_to_wide(init_name).c_str(),
                                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                                 135, 450, 465, 24, hwnd, nullptr, m_instance, nullptr));

    m_label_details = add_act(CreateWindowExW(0, L"STATIC", L"details :", WS_CHILD | WS_VISIBLE,
                            25, 482, 100, 18, hwnd, nullptr, m_instance, nullptr));
    m_edit_details = add_act(CreateWindowExW(0, L"EDIT", utf8_to_wide(m_config.custom_details).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                                             135, 480, 465, 46, hwnd, nullptr, m_instance, nullptr));

    m_label_state = add_act(CreateWindowExW(0, L"STATIC", L"etat :", WS_CHILD | WS_VISIBLE,
                            25, 534, 100, 18, hwnd, nullptr, m_instance, nullptr));
    m_edit_state = add_act(CreateWindowExW(0, L"EDIT", utf8_to_wide(m_config.custom_state).c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                                           135, 532, 465, 46, hwnd, nullptr, m_instance, nullptr));

    m_label_large_img = add_act(CreateWindowExW(0, L"STATIC", L"image (gif) :", WS_CHILD | WS_VISIBLE,
                            25, 586, 100, 18, hwnd, nullptr, m_instance, nullptr));
    m_edit_large_img = add_act(CreateWindowExW(0, L"EDIT", utf8_to_wide(m_config.custom_large_image).c_str(),
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                               135, 584, 465, 24, hwnd, nullptr, m_instance, nullptr));

    m_label_small_img = add_act(CreateWindowExW(0, L"STATIC", L"petit logo :", WS_CHILD | WS_VISIBLE,
                            25, 614, 100, 18, hwnd, nullptr, m_instance, nullptr));
    m_edit_small_img = add_act(CreateWindowExW(0, L"EDIT", utf8_to_wide(m_config.custom_small_image).c_str(),
                                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                               135, 612, 465, 24, hwnd, nullptr, m_instance, nullptr));

    m_label_from = add_act(CreateWindowExW(0, L"STATIC", L"from :", WS_CHILD | WS_VISIBLE,
                            25, 642, 100, 18, hwnd, nullptr, m_instance, nullptr));
    std::string init_from = m_config.custom_time_from.empty() ? "09:00" : m_config.custom_time_from;
    std::string init_to   = m_config.custom_time_to.empty() ? "18:00" : m_config.custom_time_to;
    m_edit_time_from = add_act(CreateWindowExW(0, L"EDIT", utf8_to_wide(init_from).c_str(),
                                                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                                135, 640, 210, 24, hwnd, nullptr, m_instance, nullptr));

    m_label_to = add_act(CreateWindowExW(0, L"STATIC", L"to :", WS_CHILD | WS_VISIBLE,
                                         353, 642, 30, 18, hwnd, nullptr, m_instance, nullptr));
    m_edit_time_to = add_act(CreateWindowExW(0, L"EDIT", utf8_to_wide(init_to).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                             390, 640, 210, 24, hwnd, nullptr, m_instance, nullptr));

    m_btn_apply_custom = add_act(CreateWindowExW(0, L"BUTTON", L"[ APPLIQUER ]", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                 135, 670, 140, 28, hwnd,
                                                 reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_APPLY_CUSTOM)), m_instance, nullptr));

    m_btn_save_preset = add_act(CreateWindowExW(0, L"BUTTON", L"[ ENREGISTRER ]", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                285, 670, 150, 28, hwnd,
                                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_SAVE_PRESET)), m_instance, nullptr));

    m_btn_delete_preset = add_act(CreateWindowExW(0, L"BUTTON", L"[ SUPPRIMER ]", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                                  445, 670, 130, 28, hwnd,
                                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_DELETE_PRESET)), m_instance, nullptr));

    // ==========================================
    // VUE 2 : PAGE DE DEBUG & REGLAGES (INTERACTIVE DANS LA MEME FENETRE)
    // ==========================================
    auto add_dbg = [this](HWND h) { m_debug_controls.push_back(h); return h; };

    // Section 1 (Debug): Discord IPC
    add_dbg(CreateWindowExW(0, L"STATIC", L"pipe actif :", WS_CHILD,
                            25, 80, 85, 20, hwnd, nullptr, m_instance, nullptr));
    m_dbg_label_pipe = add_dbg(CreateWindowExW(0, L"STATIC", L"detection...", WS_CHILD,
                                               115, 80, 200, 20, hwnd, nullptr, m_instance, nullptr));

    add_dbg(CreateWindowExW(0, L"STATIC", L"compte :", WS_CHILD,
                            330, 80, 65, 20, hwnd, nullptr, m_instance, nullptr));
    m_dbg_label_user = add_dbg(CreateWindowExW(0, L"STATIC", L"non detecte", WS_CHILD,
                                               400, 80, 200, 20, hwnd, nullptr, m_instance, nullptr));

    add_dbg(CreateWindowExW(0, L"STATIC", L"client id :", WS_CHILD,
                            25, 110, 85, 20, hwnd, nullptr, m_instance, nullptr));
    m_dbg_edit_client_id = add_dbg(CreateWindowExW(0, L"EDIT", utf8_to_wide(m_config.client_id).c_str(),
                                                  WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                                  115, 108, 200, 24, hwnd, nullptr, m_instance, nullptr));
    add_dbg(CreateWindowExW(0, L"STATIC", L"(vide = defaut auto)", WS_CHILD,
                            325, 110, 150, 20, hwnd, nullptr, m_instance, nullptr));

    m_dbg_btn_test_ipc = add_dbg(CreateWindowExW(0, L"BUTTON", L"[ TESTER ]", WS_CHILD | BS_OWNERDRAW,
                                                 25, 142, 100, 26, hwnd,
                                                 reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_DBG_TEST_IPC)), m_instance, nullptr));
    m_dbg_btn_reconnect = add_dbg(CreateWindowExW(0, L"BUTTON", L"[ RECONNECTER ]", WS_CHILD | BS_OWNERDRAW,
                                                  135, 142, 135, 26, hwnd,
                                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_DBG_RECONNECT)), m_instance, nullptr));
    m_dbg_btn_disconnect = add_dbg(CreateWindowExW(0, L"BUTTON", L"[ DECONNECTER ]", WS_CHILD | BS_OWNERDRAW,
                                                   280, 142, 135, 26, hwnd,
                                                   reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_DBG_DISCONNECT)), m_instance, nullptr));

    m_dbg_label_ipc_status = add_dbg(CreateWindowExW(0, L"STATIC", L"statut ipc : en attente", WS_CHILD,
                                                     25, 175, 570, 18, hwnd, nullptr, m_instance, nullptr));

    // Section 2 (Debug): Google Calendar
    add_dbg(CreateWindowExW(0, L"STATIC", L"url ical :", WS_CHILD,
                            25, 230, 85, 20, hwnd, nullptr, m_instance, nullptr));
    m_dbg_edit_ical_url = add_dbg(CreateWindowExW(0, L"EDIT", utf8_to_wide(m_config.ical_url).c_str(),
                                                 WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                                 115, 228, 485, 24, hwnd, nullptr, m_instance, nullptr));

    add_dbg(CreateWindowExW(0, L"STATIC", L"intervalle (s) :", WS_CHILD,
                            25, 262, 100, 20, hwnd, nullptr, m_instance, nullptr));
    std::wstring interval_str = std::to_wstring(m_config.sync_interval_seconds);
    m_dbg_edit_interval = add_dbg(CreateWindowExW(0, L"EDIT", interval_str.c_str(),
                                                  WS_CHILD | WS_BORDER | ES_NUMBER,
                                                  135, 260, 70, 24, hwnd, nullptr, m_instance, nullptr));

    m_dbg_chk_auto_sync = add_dbg(CreateWindowExW(0, L"BUTTON", L"sync automatique en arriere-plan", WS_CHILD | BS_AUTOCHECKBOX,
                                                  220, 261, 250, 22, hwnd, nullptr, m_instance, nullptr));
    SendMessageW(m_dbg_chk_auto_sync, BM_SETCHECK, m_config.auto_sync ? BST_CHECKED : BST_UNCHECKED, 0);

    m_dbg_btn_test_cal = add_dbg(CreateWindowExW(0, L"BUTTON", L"[ TESTER ICAL ]", WS_CHILD | BS_OWNERDRAW,
                                                 25, 292, 135, 26, hwnd,
                                                 reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_DBG_TEST_CAL)), m_instance, nullptr));
    m_dbg_btn_force_sync = add_dbg(CreateWindowExW(0, L"BUTTON", L"[ FORCER SYNC ]", WS_CHILD | BS_OWNERDRAW,
                                                   170, 292, 135, 26, hwnd,
                                                   reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_DBG_FORCE_SYNC)), m_instance, nullptr));

    m_dbg_label_cal_status = add_dbg(CreateWindowExW(0, L"STATIC", L"statut agenda : en attente", WS_CHILD,
                                                     25, 324, 570, 18, hwnd, nullptr, m_instance, nullptr));

    // Section 3 (Debug): Preferences
    m_dbg_chk_autostart = add_dbg(CreateWindowExW(0, L"BUTTON", L"lancer at-work au demarrage de windows (hkcu)",
                                                  WS_CHILD | BS_AUTOCHECKBOX,
                                                  25, 372, 420, 22, hwnd, nullptr, m_instance, nullptr));
    SendMessageW(m_dbg_chk_autostart, BM_SETCHECK, m_config.autostart ? BST_CHECKED : BST_UNCHECKED, 0);

    m_dbg_btn_save = add_dbg(CreateWindowExW(0, L"BUTTON", L"[ ENREGISTRER CONFIG ]", WS_CHILD | BS_OWNERDRAW,
                                             450, 368, 175, 28, hwnd,
                                             reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_DBG_SAVE_CONFIG)), m_instance, nullptr));

    // Section 4 (Debug): Console de logs en direct
    m_dbg_edit_log = add_dbg(CreateWindowExW(0, L"EDIT", L"",
                                             WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                             25, 455, 590, 175, hwnd, nullptr, m_instance, nullptr));

    m_dbg_btn_clear_log = add_dbg(CreateWindowExW(0, L"BUTTON", L"[ VIDER JOURNAL ]", WS_CHILD | BS_OWNERDRAW,
                                                  475, 636, 140, 26, hwnd,
                                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_DBG_CLEAR_LOG)), m_instance, nullptr));

    // Application globale des polices
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_ui), TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }

    SendMessageW(m_label_discord_account, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_title), TRUE);
    SendMessageW(m_label_rpc_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);
    SendMessageW(m_label_cal_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);
    SendMessageW(m_edit_ical_url, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);

    SendMessageW(m_dbg_label_pipe, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);
    SendMessageW(m_dbg_label_user, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_title), TRUE);
    SendMessageW(m_dbg_edit_client_id, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);
    SendMessageW(m_dbg_label_ipc_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);
    SendMessageW(m_dbg_edit_ical_url, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);
    SendMessageW(m_dbg_edit_interval, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);
    SendMessageW(m_dbg_label_cal_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_mono), TRUE);
    SendMessageW(m_dbg_edit_log, WM_SETFONT, reinterpret_cast<WPARAM>(m_font_log), TRUE);

    // Initialisation de la visibilite de la vue
    swtch_vw(m_current_view);
}

void MainWindow::swtch_vw(ViewMode mode) {
    m_current_view = mode;

    int show_act = (m_current_view == ViewMode::Activity) ? SW_SHOW : SW_HIDE;
    int show_dbg = (m_current_view == ViewMode::DebugSettings) ? SW_SHOW : SW_HIDE;

    for (HWND c : m_activity_controls) {
        if (c) ShowWindow(c, show_act);
    }
    for (HWND c : m_debug_controls) {
        if (c) ShowWindow(c, show_dbg);
    }

    if (m_current_view == ViewMode::DebugSettings) {
        // Rafraichir les infos dans la vue debug
        std::string pipe = m_rpc.get_active_pipe_path();
        std::string user = m_rpc.get_connected_user();
        SetWindowTextW(m_dbg_label_pipe, utf8_to_wide(pipe.empty() ? "non detecte / ferme" : pipe).c_str());
        SetWindowTextW(m_dbg_label_user, utf8_to_wide(user.empty() ? "aucun compte" : user).c_str());
        std::string ipc_st = m_rpc.is_connected() ? "statut ipc: connecte et actif (ready)" : ("statut ipc: deconnecte (" + m_rpc.get_last_error() + ")");
        SetWindowTextW(m_dbg_label_ipc_status, utf8_to_wide(ipc_st).c_str());
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    rlyt_ctrls(rc.right, rc.bottom);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void MainWindow::log_dbg(const std::string& tag, const std::string& msg) {
    std::time_t now = std::time(nullptr);
    struct tm t = {};
    localtime_s(&t, &now);
    char time_str[16];
    std::strftime(time_str, sizeof(time_str), "%H:%M:%S", &t);

    std::string line = "[" + std::string(time_str) + "] [" + tag + "] " + msg + "\r\n";
    std::wstring wline = utf8_to_wide(line);

    if (m_dbg_edit_log && IsWindow(m_dbg_edit_log)) {
        int len = GetWindowTextLengthW(m_dbg_edit_log);
        SendMessageW(m_dbg_edit_log, EM_SETSEL, len, len);
        SendMessageW(m_dbg_edit_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(wline.c_str()));
        SendMessageW(m_dbg_edit_log, EM_SCROLLCARET, 0, 0);
    }
}

void MainWindow::rlyt_ctrls(int client_w, int client_h) {
    if (client_w <= 0 || client_h <= 0) return;

    // Header : Onglets de navigation
    if (m_btn_tab_activity && m_btn_tab_debug) {
        SetWindowPos(m_btn_tab_activity, nullptr, client_w - 295, 8, 120, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(m_btn_tab_debug, nullptr, client_w - 165, 8, 150, 26, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (m_current_view == ViewMode::Activity) {
        m_show_preview = (client_w >= 880);
        int pane_right = m_show_preview ? 535 : (client_w - 15);

        // Section 1 : Discord
        if (m_btn_reconnect) {
            SetWindowPos(m_btn_reconnect, nullptr, pane_right - 155, 84, 140, 26, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (m_label_discord_account) {
            int label_w = (pane_right - 155) - 125;
            if (label_w < 100) label_w = 100;
            SetWindowPos(m_label_discord_account, nullptr, 115, 87, label_w, 20, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (m_label_rpc_status) {
            SetWindowPos(m_label_rpc_status, nullptr, 45, 126, pane_right - 60, 20, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // Section 2 : Calendar
        if (m_edit_ical_url) {
            SetWindowPos(m_edit_ical_url, nullptr, 115, 213, pane_right - 130, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (m_btn_sync_now) {
            SetWindowPos(m_btn_sync_now, nullptr, pane_right - 155, 247, 140, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (m_label_cal_status) {
            SetWindowPos(m_label_cal_status, nullptr, 45, 284, pane_right - 60, 20, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // Section 3 : Presets rapides
        size_t total_items = m_btn_presets.size() + (m_btn_clear_activity ? 1 : 0);
        int gap = 6;
        int avail_w = pane_right - 40;
        int start_x = 25;

        if (total_items <= 5) {
            int btn_w = (avail_w - (static_cast<int>(total_items) - 1) * gap) / (std::max)(1, static_cast<int>(total_items));
            if (btn_w < 50) btn_w = 50;
            for (size_t i = 0; i < m_btn_presets.size(); ++i) {
                if (m_btn_presets[i]) {
                    int x = start_x + static_cast<int>(i) * (btn_w + gap);
                    SetWindowPos(m_btn_presets[i], nullptr, x, 368, btn_w, 30, SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            if (m_btn_clear_activity) {
                int x = start_x + static_cast<int>(m_btn_presets.size()) * (btn_w + gap);
                SetWindowPos(m_btn_clear_activity, nullptr, x, 368, btn_w, 30, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        } else {
            // Repartir sur 2 lignes si plus de 5 boutons
            int items_per_row = (static_cast<int>(total_items) + 1) / 2;
            int btn_w = (avail_w - (items_per_row - 1) * gap) / (std::max)(1, items_per_row);
            if (btn_w < 45) btn_w = 45;

            for (size_t i = 0; i < m_btn_presets.size(); ++i) {
                if (m_btn_presets[i]) {
                    int row = static_cast<int>(i) / items_per_row;
                    int col = static_cast<int>(i) % items_per_row;
                    int x = start_x + col * (btn_w + gap);
                    int y = 356 + row * 28;
                    SetWindowPos(m_btn_presets[i], nullptr, x, y, btn_w, 25, SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            if (m_btn_clear_activity) {
                size_t clear_idx = m_btn_presets.size();
                int row = static_cast<int>(clear_idx) / items_per_row;
                int col = static_cast<int>(clear_idx) % items_per_row;
                int x = start_x + col * (btn_w + gap);
                int y = 356 + row * 28;
                SetWindowPos(m_btn_clear_activity, nullptr, x, y, btn_w, 25, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        // Section 4 : Activite manuelle & statuts custom
        int edit_w = pane_right - 135 - 15;
        if (edit_w < 150) edit_w = 150;

        if (m_label_preset_name) SetWindowPos(m_label_preset_name, nullptr, 25, 452, 100, 18, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_edit_preset_name)  SetWindowPos(m_edit_preset_name,  nullptr, 135, 450, edit_w, 24, SWP_NOZORDER | SWP_NOACTIVATE);

        if (m_label_details)     SetWindowPos(m_label_details,     nullptr, 25, 482, 100, 18, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_edit_details)      SetWindowPos(m_edit_details,      nullptr, 135, 480, edit_w, 46, SWP_NOZORDER | SWP_NOACTIVATE);

        if (m_label_state)       SetWindowPos(m_label_state,       nullptr, 25, 534, 100, 18, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_edit_state)        SetWindowPos(m_edit_state,        nullptr, 135, 532, edit_w, 46, SWP_NOZORDER | SWP_NOACTIVATE);

        if (m_label_large_img)   SetWindowPos(m_label_large_img,   nullptr, 25, 586, 100, 18, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_edit_large_img)    SetWindowPos(m_edit_large_img,    nullptr, 135, 584, edit_w, 24, SWP_NOZORDER | SWP_NOACTIVATE);

        if (m_label_small_img)   SetWindowPos(m_label_small_img,   nullptr, 25, 614, 100, 18, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_edit_small_img)    SetWindowPos(m_edit_small_img,    nullptr, 135, 612, edit_w, 24, SWP_NOZORDER | SWP_NOACTIVATE);

        int half_w = (edit_w - 45) / 2;
        if (half_w < 50) half_w = 50;
        int to_edit_w = edit_w - half_w - 45;
        if (to_edit_w < 50) to_edit_w = 50;

        if (m_label_from)        SetWindowPos(m_label_from,        nullptr, 25, 642, 100, 18, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_edit_time_from)    SetWindowPos(m_edit_time_from,    nullptr, 135, 640, half_w, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_label_to)          SetWindowPos(m_label_to,          nullptr, 135 + half_w + 8, 642, 30, 18, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_edit_time_to)      SetWindowPos(m_edit_time_to,      nullptr, 135 + half_w + 45, 640, to_edit_w, 24, SWP_NOZORDER | SWP_NOACTIVATE);

        int b_gap = 6;
        int b_w = (edit_w - 2 * b_gap) / 3;
        if (b_w < 70) b_w = 70;

        if (m_btn_apply_custom)  SetWindowPos(m_btn_apply_custom,  nullptr, 135, 670, b_w, 28, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_btn_save_preset)   SetWindowPos(m_btn_save_preset,   nullptr, 135 + b_w + b_gap, 670, b_w, 28, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_btn_delete_preset) SetWindowPos(m_btn_delete_preset, nullptr, 135 + 2 * (b_w + b_gap), 670, b_w, 28, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        // Section 1 Debug
        if (m_dbg_label_ipc_status) SetWindowPos(m_dbg_label_ipc_status, nullptr, 25, 175, client_w - 50, 18, SWP_NOZORDER | SWP_NOACTIVATE);

        // Section 2 Debug
        if (m_dbg_edit_ical_url)    SetWindowPos(m_dbg_edit_ical_url,    nullptr, 115, 228, client_w - 135, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_dbg_label_cal_status) SetWindowPos(m_dbg_label_cal_status, nullptr, 25, 324, client_w - 50, 18, SWP_NOZORDER | SWP_NOACTIVATE);

        // Section 3 Debug
        if (m_dbg_btn_save)         SetWindowPos(m_dbg_btn_save,         nullptr, client_w - 195, 368, 175, 28, SWP_NOZORDER | SWP_NOACTIVATE);

        // Section 4 Debug : Live log
        if (m_dbg_edit_log)         SetWindowPos(m_dbg_edit_log,         nullptr, 25, 455, client_w - 50, 205, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_dbg_btn_clear_log)    SetWindowPos(m_dbg_btn_clear_log,    nullptr, client_w - 170, 668, 145, 26, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void MainWindow::pnt_wnd(HDC real_hdc) {
    RECT client_rc;
    GetClientRect(m_hwnd, &client_rc);
    int client_w = client_rc.right - client_rc.left;
    int client_h = client_rc.bottom - client_rc.top;
    if (client_w <= 0 || client_h <= 0) return;

    HDC hdc = CreateCompatibleDC(real_hdc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(real_hdc, client_w, client_h);
    HGDIOBJ old_bmp = SelectObject(hdc, mem_bmp);

    // 1. Fond general Slate Khaki (#545e60)
    FillRect(hdc, &client_rc, m_brush_bg);

    // 2. Header tactique Deep Obsidian Petrol (#162326)
    RECT header_rc = { 0, 0, client_rc.right, 42 };
    FillRect(hdc, &header_rc, m_brush_mid);

    HGDIOBJ old_pen = SelectObject(hdc, m_pen_line);
    MoveToEx(hdc, 0, 42, nullptr);
    LineTo(hdc, client_rc.right, 42);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme::COLOR_FG);
    HGDIOBJ old_font = SelectObject(hdc, m_font_title);
    TextOutW(hdc, 18, 12, L"AT-WORK", 7);

    bool show_preview = (client_rc.right >= 880);
    int pane_right = (m_current_view == ViewMode::Activity && show_preview) ? 535 : (client_rc.right - 15);

    if (m_current_view == ViewMode::Activity) {
        // --- Panneaux Vue 1 (Activite) ---
        struct PaneDef {
            RECT rc;
            const wchar_t* title;
        };

        PaneDef panes[] = {
            { { 15, 54, pane_right, 166 }, L"DISCORD RPC" },
            { { 15, 178, pane_right, 320 }, L"GOOGLE CALENDAR (ICAL)" },
            { { 15, 332, pane_right, 418 }, L"PRESETS RAPIDES" },
            { { 15, 426, pane_right, 708 }, L"ACTIVITE & STATUTS PERSONNALISES" }
        };

        HGDIOBJ old_brush = SelectObject(hdc, m_brush_pane);

        for (const auto& p : panes) {
            SelectObject(hdc, m_brush_pane);
            SelectObject(hdc, m_pen_line);
            RoundRect(hdc, p.rc.left, p.rc.top, p.rc.right, p.rc.bottom, 6, 6);

            SelectObject(hdc, m_font_section);
            SetTextColor(hdc, theme::COLOR_ACCENT);
            TextOutW(hdc, p.rc.left + 10, p.rc.top + 7, p.title, static_cast<int>(wcslen(p.title)));
        }

        // Pastilles d'etat
        int rpc_cx = 28;
        int rpc_cy = 134;
        if (m_rpc_is_online) {
            SelectObject(hdc, m_pen_online_glow);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, rpc_cx - 6, rpc_cy - 6, rpc_cx + 6, rpc_cy + 6);

            SelectObject(hdc, m_pen_null);
            SelectObject(hdc, m_brush_online);
            Ellipse(hdc, rpc_cx - 4, rpc_cy - 4, rpc_cx + 4, rpc_cy + 4);
        } else {
            SelectObject(hdc, m_pen_null);
            SelectObject(hdc, m_brush_offline);
            Ellipse(hdc, rpc_cx - 4, rpc_cy - 4, rpc_cx + 4, rpc_cy + 4);
        }

        int cal_cx = 28;
        int cal_cy = 292;
        if (m_cal_is_active) {
            SelectObject(hdc, m_pen_online_glow);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, cal_cx - 6, cal_cy - 6, cal_cx + 6, cal_cy + 6);

            SelectObject(hdc, m_pen_null);
            SelectObject(hdc, m_brush_online);
            Ellipse(hdc, cal_cx - 4, cal_cy - 4, cal_cx + 4, cal_cy + 4);
        } else {
            SelectObject(hdc, m_pen_null);
            SelectObject(hdc, m_brush_offline);
            Ellipse(hdc, cal_cx - 4, cal_cy - 4, cal_cx + 4, cal_cy + 4);
        }

        SelectObject(hdc, m_font_mono);
        SetTextColor(hdc, theme::COLOR_ACCENT);
        TextOutW(hdc, 18, 716, L"// PROTOCOL: DISCORD IPC PIPE-0 // ENGINE: RFC-5545 TLS", 55);

        // Preview Render (Discord) si largeur suffisante
        if (show_preview) {
            RECT preview_rc = { pane_right + 15, 54, client_rc.right - 15, 708 };
            drw_dsc_prvw(hdc, preview_rc);
        }

        SelectObject(hdc, old_brush);
    } else {
        // --- Panneaux Vue 2 (Debug & Reglages) ---
        struct PaneBox {
            RECT rc;
            const wchar_t* title;
            const wchar_t* note;
        };

        PaneBox boxes[] = {
            { { 15, 54, pane_right, 202 }, L"1. DIAGNOSTIC DISCORD IPC (INTERACTIF)", L"connexion directe named pipe // protocole discord desktop" },
            { { 15, 212, pane_right, 348 }, L"2. DIAGNOSTIC GOOGLE CALENDAR (INTERACTIF)", L"requete tls rfc-5545 // detection d'evenements" },
            { { 15, 356, pane_right, 410 }, L"3. PREFERENCES SYSTEME", L"demarrage en arriere-plan et persistance" },
            { { 15, 420, pane_right, 708 }, L"4. JOURNAL DE DEBUG EN DIRECT (LIVE STREAM)", L"evenements console avec horodatage" }
        };

        HGDIOBJ old_brush = SelectObject(hdc, m_brush_pane);

        for (const auto& b : boxes) {
            SelectObject(hdc, m_brush_pane);
            SelectObject(hdc, m_pen_line);
            RoundRect(hdc, b.rc.left, b.rc.top, b.rc.right, b.rc.bottom, 6, 6);

            SelectObject(hdc, m_font_section);
            SetTextColor(hdc, theme::COLOR_ACCENT);
            TextOutW(hdc, b.rc.left + 10, b.rc.top + 6, b.title, static_cast<int>(wcslen(b.title)));

            SelectObject(hdc, m_font_ui);
            SetTextColor(hdc, theme::COLOR_LINE);
            TextOutW(hdc, b.rc.left + 10, b.rc.top + 20, b.note, static_cast<int>(wcslen(b.note)));
        }

        SelectObject(hdc, m_font_mono);
        SetTextColor(hdc, theme::COLOR_ACCENT);
        TextOutW(hdc, 18, 680, L"// MODE: DIAGNOSTIC & CONFIGURATION // SANS POPUP", 49);

        SelectObject(hdc, old_brush);
    }

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_font);

    BitBlt(real_hdc, 0, 0, client_w, client_h, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(hdc);
}

void MainWindow::drw_dsc_prvw(HDC hdc, const RECT& preview_rc) {
    // 1. Cadre du panneau Preview Render
    SelectObject(hdc, m_brush_pane);
    SelectObject(hdc, m_pen_line);
    RoundRect(hdc, preview_rc.left, preview_rc.top, preview_rc.right, preview_rc.bottom, 6, 6);

    // Titre et sous-titre de la section
    SelectObject(hdc, m_font_section);
    SetTextColor(hdc, theme::COLOR_ACCENT);
    TextOutW(hdc, preview_rc.left + 12, preview_rc.top + 7, L"PREVIEW RENDER (DISCORD)", 24);

    SelectObject(hdc, m_font_ui);
    SetTextColor(hdc, theme::COLOR_LINE);
    TextOutW(hdc, preview_rc.left + 12, preview_rc.top + 22, L"apercu simule temps reel sur profil discord", 43);

    // 2. Carte de profil Discord simulee
    int card_pad = 14;
    int card_left = preview_rc.left + card_pad;
    int card_right = preview_rc.right - card_pad;
    int card_top = preview_rc.top + 45;

    // Declenchement du telechargement asynchrone de l'avatar Discord si disponible
    std::string av_url = m_rpc.get_avatar_url();
    if (!av_url.empty() && av_url != m_current_avatar_url && !m_avatar_loading) {
        trigger_image_download(ImageType::Avatar, av_url);
    }

    // Informations du profil reel Discord
    auto profile = m_rpc.get_user_profile();
    if (!profile.avatar_decoration_url.empty() && profile.avatar_decoration_url != m_current_decor_url && !m_decor_loading) {
        trigger_image_download(ImageType::Decoration, profile.avatar_decoration_url);
    }

    // Banniere Discord Nitro (recherche et telechargement)
    if (!profile.banner_url.empty() && profile.banner_url != m_current_banner_url && !m_banner_loading) {
        trigger_image_download(ImageType::Banner, profile.banner_url);
    } else if (profile.banner_url.empty() && !profile.id.empty() && m_last_profile_lookup_id != profile.id) {
        m_last_profile_lookup_id = profile.id;
        trigger_profile_lookup(profile.id);
    }

    std::string disp_name = profile.global_name.empty() ? profile.username : profile.global_name;
    if (disp_name.empty()) {
        std::string conn_user = m_rpc.get_connected_user();
        disp_name = conn_user.empty() ? "Utilisateur Discord" : conn_user;
    }
    std::string handle = profile.username.empty() ? "" : ("@" + profile.username);

    // Contenu live de l'activite
    std::string live_details = get_window_text_utf8(m_edit_details);
    std::string live_state   = get_window_text_utf8(m_edit_state);
    std::string live_large   = get_window_text_utf8(m_edit_large_img);
    std::string live_small   = get_window_text_utf8(m_edit_small_img);

    std::string resolved_large = resolve_image_url(live_large);
    if (!resolved_large.empty() && (resolved_large.rfind("http://", 0) == 0 || resolved_large.rfind("https://", 0) == 0)) {
        if (resolved_large != m_current_asset_url && !m_asset_loading) {
            trigger_image_download(ImageType::Asset, resolved_large);
        }
    } else if (resolved_large.empty() && m_asset_bitmap) {
        free_loaded_image(m_asset_bitmap, m_asset_stream);
        m_current_asset_url.clear();
    }

    int av_d = 46;
    int av_x = card_left + 16;
    int av_y = card_top + 34;

    int asset_x = card_left + 14;
    int asset_y = card_top + 126;
    int asset_sz = 60;
    int text_x = asset_x + asset_sz + 12;
    int text_w = card_right - text_x - 12;

    // Calcul dynamique de la hauteur du texte (details et etat en mode paragraphe)
    std::wstring wdetails = utf8_to_wide(live_details.empty() ? "(aucun detail)" : live_details);
    std::wstring wstate   = utf8_to_wide(live_state.empty() ? "(aucun etat)" : live_state);

    HGDIOBJ prev_font = SelectObject(hdc, m_font_discord_sub);
    RECT rc_calc_det = { 0, 0, text_w, 0 };
    DrawTextW(hdc, wdetails.c_str(), -1, &rc_calc_det, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int det_h = rc_calc_det.bottom - rc_calc_det.top;
    if (det_h < 16) det_h = 16;
    if (det_h > 48) det_h = 48;

    RECT rc_calc_st = { 0, 0, text_w, 0 };
    DrawTextW(hdc, wstate.c_str(), -1, &rc_calc_st, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int st_h = rc_calc_st.bottom - rc_calc_st.top;
    if (st_h < 16) st_h = 16;
    if (st_h > 48) st_h = 48;
    SelectObject(hdc, prev_font);

    int text_col_h = 18 + 2 + det_h + 2 + st_h + 2 + 16;
    int content_bottom = asset_y + (std::max)(asset_sz, text_col_h);
    int needed_card_bottom = content_bottom + 12 + 26 + 12;
    int min_card_bottom = card_top + 265;
    int card_bottom = (std::max)(min_card_bottom, needed_card_bottom);
    RECT card_rc = { card_left, card_top, card_right, card_bottom };

    SelectObject(hdc, m_brush_discord_card);
    SelectObject(hdc, m_pen_discord_line);
    RoundRect(hdc, card_rc.left, card_rc.top, card_rc.right, card_rc.bottom, 8, 8);

    // --- PHASE 1: GDI+ RENDERING (Graphics isole dans un scope pour liberer HDC avant le texte GDI) ---
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

        // Banniere Discord (Image/GIF Discord Nitro ou couleur dominante / d'accentuation)
        COLORREF banner_col = profile.has_banner_color ? profile.banner_color :
                             (m_has_dominant_banner_color ? m_dominant_banner_color : RGB(35, 39, 42));
        Gdiplus::SolidBrush banner_brush(Gdiplus::Color(255, GetRValue(banner_col), GetGValue(banner_col), GetBValue(banner_col)));
        Gdiplus::GraphicsPath banner_path;
        int b_r = 8;
        int b_d = b_r * 2;
        banner_path.AddArc(card_rc.left, card_rc.top, b_d, b_d, 180, 90);
        banner_path.AddArc(card_rc.right - b_d - 1, card_rc.top, b_d, b_d, 270, 90);
        banner_path.AddLine(card_rc.right - 1, card_rc.top + 60, card_rc.left, card_rc.top + 60);
        banner_path.CloseFigure();

        // Fond couleur sous la banniere (visible en cas de transparence du GIF)
        g.FillPath(&banner_brush, &banner_path);

        if (m_banner_bitmap) {
            g.SetClip(&banner_path);
            int bw = card_rc.right - card_rc.left;
            int bh = 60;
            int img_w = m_banner_bitmap->GetWidth();
            int img_h = m_banner_bitmap->GetHeight();
            if (img_w > 0 && img_h > 0) {
                float scale = (std::max)(static_cast<float>(bw) / img_w, static_cast<float>(bh) / img_h);
                int draw_w = static_cast<int>(img_w * scale);
                int draw_h = static_cast<int>(img_h * scale);
                int draw_x = card_rc.left + (bw - draw_w) / 2;
                int draw_y = card_rc.top + (bh - draw_h) / 2;
                g.DrawImage(m_banner_bitmap, draw_x, draw_y, draw_w, draw_h);
            } else {
                g.DrawImage(m_banner_bitmap, card_rc.left, card_rc.top, bw, bh);
            }
            g.ResetClip();
        }

        // Avatar Discord reel (cercle avec anneau sombre separateur #111214)
        Gdiplus::SolidBrush card_bg(Gdiplus::Color(255, 17, 18, 20));
        g.FillEllipse(&card_bg, av_x - 3, av_y - 3, av_d + 6, av_d + 6);

        if (m_avatar_bitmap) {
            Gdiplus::GraphicsPath clip_path;
            clip_path.AddEllipse(av_x, av_y, av_d, av_d);
            g.SetClip(&clip_path);
            g.DrawImage(m_avatar_bitmap, av_x, av_y, av_d, av_d);
            g.ResetClip();
        } else {
            Gdiplus::SolidBrush def_av_b(Gdiplus::Color(255, 88, 101, 242));
            g.FillEllipse(&def_av_b, av_x, av_y, av_d, av_d);
        }

        // Decoration d'avatar Discord officielle (cadre ornemental superpose a l'avatar)
        if (m_decor_bitmap) {
            int decor_sz = static_cast<int>(av_d * 1.20f);
            int decor_x = av_x + (av_d - decor_sz) / 2;
            int decor_y = av_y + (av_d - decor_sz) / 2;
            g.DrawImage(m_decor_bitmap, decor_x, decor_y, decor_sz, decor_sz);
        }

        // Pastille de statut Discord
        int sdot_d = 14;
        int sdot_x = av_x + av_d - 11;
        int sdot_y = av_y + av_d - 11;
        g.FillEllipse(&card_bg, sdot_x - 2, sdot_y - 2, sdot_d + 4, sdot_d + 4);
        Gdiplus::SolidBrush sdot_brush(m_rpc_is_online ? Gdiplus::Color(255, 35, 165, 89) : Gdiplus::Color(255, 128, 132, 142));
        g.FillEllipse(&sdot_brush, sdot_x, sdot_y, sdot_d, sdot_d);

        std::string chk_from = get_window_text_utf8(m_edit_time_from);
        std::string chk_to   = get_window_text_utf8(m_edit_time_to);
        int64_t chk_s = 0, chk_e = 0;
        bool chk_in_slot = parse_time_parts(chk_from, chk_to, chk_s, chk_e) || (m_cal_is_active && m_activity_end_time > std::time(nullptr));

        // Image d'activite (Large Asset) avec coins arrondis si telechargee et dans la tranche active
        if (chk_in_slot && m_asset_bitmap) {
            Gdiplus::GraphicsPath asset_path;
            int r = 8;
            int d = r * 2;
            asset_path.AddArc(asset_x, asset_y, d, d, 180, 90);
            asset_path.AddArc(asset_x + asset_sz - d, asset_y, d, d, 270, 90);
            asset_path.AddArc(asset_x + asset_sz - d, asset_y + asset_sz - d, d, d, 0, 90);
            asset_path.AddArc(asset_x, asset_y + asset_sz - d, d, d, 90, 90);
            asset_path.CloseFigure();

            g.SetClip(&asset_path);
            g.DrawImage(m_asset_bitmap, asset_x, asset_y, asset_sz, asset_sz);
            g.ResetClip();

            Gdiplus::Pen asset_border(Gdiplus::Color(255, 55, 58, 64), 1.0f);
            g.DrawPath(&asset_border, &asset_path);
        }
    }
    // --- FIN PHASE 1 GDI+ : HDC est maintenant libere et pret pour le texte GDI pur ---

    // Initiales utilisateur si pas d'avatar charge
    if (!m_avatar_bitmap && !disp_name.empty()) {
        wchar_t initial[2] = { static_cast<wchar_t>(towupper(disp_name[0])), 0 };
        SelectObject(hdc, m_font_title);
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT init_rc = { av_x, av_y, av_x + av_d, av_y + av_d };
        DrawTextW(hdc, initial, 1, &init_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Nom et tag utilisateur Discord reel
    int user_x = av_x + av_d + 14;
    int user_max_w = card_rc.right - user_x - 12;

    SelectObject(hdc, m_font_discord_app);
    SetTextColor(hdc, RGB(242, 243, 245));
    std::wstring wdisp = utf8_to_wide(disp_name);
    RECT rc_disp = { user_x, card_rc.top + 62, user_x + user_max_w, card_rc.top + 80 };
    DrawTextW(hdc, wdisp.c_str(), -1, &rc_disp, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (!handle.empty()) {
        SelectObject(hdc, m_font_discord_sub);
        SetTextColor(hdc, RGB(148, 155, 164));
        std::wstring whandle = utf8_to_wide(handle);
        RECT rc_handle = { user_x, card_rc.top + 80, user_x + user_max_w, card_rc.top + 96 };
        DrawTextW(hdc, whandle.c_str(), -1, &rc_handle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // Ligne de separation
    HPEN sep_pen = CreatePen(PS_SOLID, 1, RGB(38, 41, 46));
    SelectObject(hdc, sep_pen);
    MoveToEx(hdc, card_rc.left + 12, card_rc.top + 104, nullptr);
    LineTo(hdc, card_rc.right - 12, card_rc.top + 104);
    DeleteObject(sep_pen);

    std::time_t now = std::time(nullptr);
    std::string live_from = get_window_text_utf8(m_edit_time_from);
    std::string live_to   = get_window_text_utf8(m_edit_time_to);

    int64_t target_start = 0;
    int64_t target_end = 0;
    bool has_live_time = parse_time_parts(live_from, live_to, target_start, target_end);
    if (!has_live_time && m_cal_is_active && m_activity_end_time > now) {
        target_start = m_activity_start_time;
        target_end = m_activity_end_time;
        has_live_time = true;
    }

    if (has_live_time) {
        // Titre de section activite : PLAYING A GAME
        SelectObject(hdc, m_font_discord_header);
        SetTextColor(hdc, RGB(148, 155, 164));
        TextOutW(hdc, card_rc.left + 14, card_rc.top + 110, L"PLAYING A GAME", 14);

        // Placeholder pour l'Asset si aucun bitmap charge
        if (!m_asset_bitmap) {
            RECT asset_rc = { asset_x, asset_y, asset_x + asset_sz, asset_y + asset_sz };
            HBRUSH asset_brush = CreateSolidBrush(RGB(43, 45, 49));
            HPEN asset_pen = CreatePen(PS_SOLID, 1, RGB(55, 58, 64));
            SelectObject(hdc, asset_brush);
            SelectObject(hdc, asset_pen);
            RoundRect(hdc, asset_rc.left, asset_rc.top, asset_rc.right, asset_rc.bottom, 8, 8);
            DeleteObject(asset_brush);
            DeleteObject(asset_pen);

            bool is_gif = (live_large.find(".gif") != std::string::npos || live_large.find("giphy") != std::string::npos || live_large.find("tenor") != std::string::npos);
            if (is_gif) {
                RECT badge_rc = { asset_x + 12, asset_y + 19, asset_x + asset_sz - 12, asset_y + 39 };
                HBRUSH blurple_brush = CreateSolidBrush(RGB(88, 101, 242));
                SelectObject(hdc, blurple_brush);
                SelectObject(hdc, m_pen_null);
                RoundRect(hdc, badge_rc.left, badge_rc.top, badge_rc.right, badge_rc.bottom, 4, 4);
                DeleteObject(blurple_brush);

                SelectObject(hdc, m_font_badge);
                SetTextColor(hdc, RGB(255, 255, 255));
                DrawTextW(hdc, L"GIF", 3, &badge_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else if (!live_large.empty()) {
                RECT badge_rc = { asset_x + 12, asset_y + 19, asset_x + asset_sz - 12, asset_y + 39 };
                HBRUSH img_brush = CreateSolidBrush(RGB(60, 64, 72));
                SelectObject(hdc, img_brush);
                SelectObject(hdc, m_pen_null);
                RoundRect(hdc, badge_rc.left, badge_rc.top, badge_rc.right, badge_rc.bottom, 4, 4);
                DeleteObject(img_brush);

                SelectObject(hdc, m_font_badge);
                SetTextColor(hdc, RGB(220, 222, 225));
                DrawTextW(hdc, L"IMG", 3, &badge_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                RECT badge_rc = { asset_x + 10, asset_y + 19, asset_x + asset_sz - 10, asset_y + 39 };
                SelectObject(hdc, m_font_badge);
                SetTextColor(hdc, RGB(148, 155, 164));
                DrawTextW(hdc, L"RPC", 3, &badge_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        // Petit badge overlay si small_image present
        if (!live_small.empty()) {
            int sm_cx = asset_x + asset_sz - 8;
            int sm_cy = asset_y + asset_sz - 8;
            int sm_r = 10;
            HBRUSH sm_brush = CreateSolidBrush(RGB(32, 34, 37));
            HPEN sm_pen = CreatePen(PS_SOLID, 2, RGB(17, 18, 20));
            SelectObject(hdc, sm_brush);
            SelectObject(hdc, sm_pen);
            Ellipse(hdc, sm_cx - sm_r, sm_cy - sm_r, sm_cx + sm_r, sm_cy + sm_r);
            DeleteObject(sm_brush);
            DeleteObject(sm_pen);

            SelectObject(hdc, m_font_badge);
            SetTextColor(hdc, theme::COLOR_ACCENT);
            RECT sm_rc = { sm_cx - sm_r, sm_cy - sm_r, sm_cx + sm_r, sm_cy + sm_r };
            DrawTextW(hdc, L"+", 1, &sm_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Nom de l'application
        std::string app_name = profile.app_name.empty() ? "At-Work" : profile.app_name;
        if (profile.app_name.empty() && m_config.client_id == "383226320970055681") {
            app_name = "Visual Studio Code";
        }
        int cur_text_y = asset_y + 1;
        RECT rc_app = { text_x, cur_text_y, text_x + text_w, cur_text_y + 18 };
        SelectObject(hdc, m_font_discord_app);
        SetTextColor(hdc, RGB(242, 243, 245));
        std::wstring wapp = utf8_to_wide(app_name);
        DrawTextW(hdc, wapp.c_str(), -1, &rc_app, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        cur_text_y += 18 + 2;

        // Details (paragraphe dynamique)
        RECT rc_det = { text_x, cur_text_y, text_x + text_w, cur_text_y + det_h };
        SelectObject(hdc, m_font_discord_sub);
        SetTextColor(hdc, RGB(219, 222, 225));
        DrawTextW(hdc, wdetails.c_str(), -1, &rc_det, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        cur_text_y += det_h + 2;

        // State (paragraphe dynamique)
        RECT rc_st = { text_x, cur_text_y, text_x + text_w, cur_text_y + st_h };
        SelectObject(hdc, m_font_discord_sub);
        SetTextColor(hdc, RGB(148, 155, 164));
        DrawTextW(hdc, wstate.c_str(), -1, &rc_st, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        cur_text_y += st_h + 2;

        // Timer / Compte a rebours Discord dynamique (uniquement dans la tranche active)
        if (target_end > now) {
            wchar_t time_buf[64] = {};
            int64_t rem = target_end - now;
            int hrs = static_cast<int>(rem / 3600);
            int mins = static_cast<int>((rem % 3600) / 60);
            int secs = static_cast<int>(rem % 60);
            if (hrs > 0) {
                swprintf_s(time_buf, L"%d:%02d:%02d restant", hrs, mins, secs);
            } else {
                swprintf_s(time_buf, L"%02d:%02d restant", mins, secs);
            }
            RECT rc_tm = { text_x, cur_text_y, text_x + text_w, cur_text_y + 16 };
            SelectObject(hdc, m_font_discord_sub);
            SetTextColor(hdc, RGB(148, 155, 164));
            DrawTextW(hdc, time_buf, -1, &rc_tm, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }
    } else {
        // Hors tranche horaire active : aucune activite ni timestamp affiche
        SelectObject(hdc, m_font_discord_header);
        SetTextColor(hdc, RGB(115, 120, 128));
        TextOutW(hdc, card_rc.left + 14, card_rc.top + 118, L"AUCUNE ACTIVITE EN COURS", 24);

        SelectObject(hdc, m_font_discord_sub);
        SetTextColor(hdc, RGB(148, 155, 164));
        std::wstring w_inactive = utf8_to_wide("Hors tranche active (" + live_from + " - " + live_to + ")");
        RECT rc_inact = { card_rc.left + 14, card_rc.top + 138, card_rc.right - 14, card_rc.top + 160 };
        DrawTextW(hdc, w_inactive.c_str(), -1, &rc_inact, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // Statut IPC bas de carte
    RECT stat_box = { card_rc.left + 14, card_bottom - 36, card_rc.right - 14, card_bottom - 12 };
    HBRUSH stat_box_brush = CreateSolidBrush(RGB(24, 26, 29));
    HPEN stat_box_pen = CreatePen(PS_SOLID, 1, RGB(38, 41, 46));
    SelectObject(hdc, stat_box_brush);
    SelectObject(hdc, stat_box_pen);
    RoundRect(hdc, stat_box.left, stat_box.top, stat_box.right, stat_box.bottom, 4, 4);
    DeleteObject(stat_box_brush);
    DeleteObject(stat_box_pen);

    // Pastille statut
    int sdot_cx = stat_box.left + 14;
    int sdot_cy = (stat_box.top + stat_box.bottom) / 2;
    HBRUSH sdot_b = CreateSolidBrush(m_rpc_is_online ? RGB(35, 165, 89) : RGB(218, 55, 60));
    SelectObject(hdc, sdot_b);
    SelectObject(hdc, m_pen_null);
    Ellipse(hdc, sdot_cx - 4, sdot_cy - 4, sdot_cx + 4, sdot_cy + 4);
    DeleteObject(sdot_b);

    SelectObject(hdc, m_font_mono);
    SetTextColor(hdc, m_rpc_is_online ? theme::COLOR_ONLINE : theme::COLOR_OFFLINE);
    const wchar_t* stat_str = m_rpc_is_online ? L"TRANSMISSION IPC ACTIVE" : L"DISCORD DECONNECTE";
    TextOutW(hdc, sdot_cx + 10, sdot_cy - 6, stat_str, static_cast<int>(wcslen(stat_str)));

    // 3. Panneau Telemetrie technique sous la carte Discord
    int telem_top = card_bottom + 14;
    int telem_bottom = preview_rc.bottom - 12;
    RECT telem_rc = { preview_rc.left + card_pad, telem_top, preview_rc.right - card_pad, telem_bottom };

    SelectObject(hdc, m_brush_mid);
    SelectObject(hdc, m_pen_line);
    RoundRect(hdc, telem_rc.left, telem_top, telem_rc.right, telem_bottom, 6, 6);

    SelectObject(hdc, m_font_section);
    SetTextColor(hdc, theme::COLOR_ACCENT);
    TextOutW(hdc, telem_rc.left + 10, telem_top + 8, L"TELEMETRIE DE CHARGE UTILE (PAYLOAD)", 36);

    SelectObject(hdc, m_font_mono);
    SetTextColor(hdc, theme::COLOR_FG);

    auto draw_kv = [&](int row, const wchar_t* key, const std::string& val) {
        int y = telem_top + 28 + row * 18;
        if (y + 16 > telem_bottom) return;
        TextOutW(hdc, telem_rc.left + 10, y, key, static_cast<int>(wcslen(key)));
        std::wstring wval = utf8_to_wide(val.empty() ? "(aucun)" : val);
        RECT val_rc = { telem_rc.left + 130, y, telem_rc.right - 10, y + 16 };
        SetTextColor(hdc, theme::COLOR_ACCENT);
        DrawTextW(hdc, wval.c_str(), -1, &val_rc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(hdc, theme::COLOR_FG);
    };

    draw_kv(0, L"LARGE ASSET :", live_large);
    draw_kv(1, L"SMALL ASSET :", live_small);
    draw_kv(2, L"PIPE CANAL  :", m_rpc.get_active_pipe_path());
    draw_kv(3, L"CLIENT ID   :", m_rpc.get_client_id().empty() ? m_config.client_id : m_rpc.get_client_id());
    draw_kv(4, L"STATUS      :", m_rpc_is_online ? "SYNCHRONISE" : "ATTENTE");
}

void MainWindow::drw_ownr_btn(LPDRAWITEMSTRUCT dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool is_selected = (dis->itemState & ODS_SELECTED);
    bool is_disabled = (dis->itemState & ODS_DISABLED);
    int ctl_id = static_cast<int>(dis->CtlID);

    bool is_active_tab = false;
    if (ctl_id == ID_TAB_ACTIVITY && m_current_view == ViewMode::Activity) {
        is_active_tab = true;
    } else if (ctl_id == ID_TAB_DEBUG && m_current_view == ViewMode::DebugSettings) {
        is_active_tab = true;
    }

    HBRUSH bg_brush = (is_selected || is_active_tab) ? m_brush_accent : m_brush_mid;
    HPEN border_pen = m_pen_line;
    COLORREF text_color = (is_selected || is_active_tab) ? theme::COLOR_MID : (is_disabled ? theme::COLOR_OFFLINE : theme::COLOR_FG);

    HGDIOBJ old_brush = SelectObject(hdc, bg_brush);
    HGDIOBJ old_pen = SelectObject(hdc, border_pen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);
    HGDIOBJ old_font = SelectObject(hdc, (is_selected || is_active_tab) ? m_font_title : m_font_ui);

    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, old_font);
}

void MainWindow::upd_rpc_st_lbl(const std::string& text, bool online) {
    m_rpc_is_online = online;
    SetWindowTextW(m_label_rpc_status, utf8_to_wide("statut : " + text).c_str());
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::upd_cal_st_lbl(const std::string& text, bool active) {
    m_cal_is_active = active;
    SetWindowTextW(m_label_cal_status, utf8_to_wide("agenda : " + text).c_str());
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::on_rcnnct_clk() {
    log_dbg("IPC", "Tentative de connexion a Discord Desktop via Named Pipe...");
    std::string client_id = m_config.client_id;
    if (m_rpc.connect(client_id)) {
        std::string user = m_rpc.get_connected_user();
        std::string pipe = m_rpc.get_active_pipe_path();
        SetWindowTextW(m_label_discord_account, utf8_to_wide(user).c_str());
        upd_rpc_st_lbl("connecte via discord desktop (ipc active)", true);

        SetWindowTextW(m_dbg_label_pipe, utf8_to_wide(pipe).c_str());
        SetWindowTextW(m_dbg_label_user, utf8_to_wide(user).c_str());
        SetWindowTextW(m_dbg_label_ipc_status, L"statut ipc: connecte et pret");

        log_dbg("IPC", "Succes: connecte a " + pipe + " (" + user + ")");

        std::string av_url = m_rpc.get_avatar_url();
        if (!av_url.empty() && av_url != m_current_avatar_url && !m_avatar_loading) {
            trg_img_dwnld(ImageType::Avatar, av_url);
        }

        auto profile = m_rpc.get_user_profile();
        if (!profile.avatar_decoration_url.empty() && profile.avatar_decoration_url != m_current_decor_url && !m_decor_loading) {
            trg_img_dwnld(ImageType::Decoration, profile.avatar_decoration_url);
        }

        if (!profile.banner_url.empty() && profile.banner_url != m_current_banner_url && !m_banner_loading) {
            trg_img_dwnld(ImageType::Banner, profile.banner_url);
        } else if (profile.banner_url.empty() && !profile.id.empty()) {
            m_last_profile_lookup_id = profile.id;
            trg_prfl_lkp(profile.id);
        }
    } else {
        SetWindowTextW(m_label_discord_account, L"non detecte (ouvrez discord desktop)");
        upd_rpc_st_lbl("echec: " + m_rpc.get_last_error(), false);

        SetWindowTextW(m_dbg_label_pipe, L"deconnecte");
        SetWindowTextW(m_dbg_label_user, L"non detecte");
        std::string err = "statut ipc: echec (" + m_rpc.get_last_error() + ")";
        SetWindowTextW(m_dbg_label_ipc_status, utf8_to_wide(err).c_str());

        log_dbg("IPC", "Echec: " + m_rpc.get_last_error());

        if (m_avatar_bitmap) {
            delete m_avatar_bitmap;
            m_avatar_bitmap = nullptr;
        }
        m_current_avatar_url.clear();

        if (m_decor_bitmap) {
            delete m_decor_bitmap;
            m_decor_bitmap = nullptr;
        }
        m_current_decor_url.clear();

        free_loaded_image(m_banner_bitmap, m_banner_stream);
        m_current_banner_url.clear();
        m_banner_frame_index = 0;
        m_banner_frame_count = 1;
        m_last_profile_lookup_id.clear();

        m_dominant_banner_color = RGB(32, 34, 37);
        m_has_dominant_banner_color = false;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

namespace {
    struct SchedActState {
        bool active = false;
        bool was_in_slot = false;
        std::string label;
        std::string time_from;
        std::string time_to;
        discord::Activity act;
        std::time_t last_reassert = 0;
    };
    static SchedActState g_sched;
}

void MainWindow::on_prst_clk(size_t index) {
    if (index >= m_config.presets.size()) return;

    const auto& p = m_config.presets[index];

    SetWindowTextW(m_edit_preset_name, utf8_to_wide(p.label).c_str());
    SetWindowTextW(m_edit_details, utf8_to_wide(p.details).c_str());
    SetWindowTextW(m_edit_state, utf8_to_wide(p.state).c_str());
    SetWindowTextW(m_edit_large_img, utf8_to_wide(p.large_image).c_str());
    SetWindowTextW(m_edit_small_img, utf8_to_wide(p.small_image).c_str());
    std::string t_from = normalize_time_str(p.time_from.empty() ? "09:00" : p.time_from, "09:00");
    std::string t_to   = normalize_time_str(p.time_to.empty()   ? "18:00" : p.time_to,   "18:00");
    SetWindowTextW(m_edit_time_from, utf8_to_wide(t_from).c_str());
    SetWindowTextW(m_edit_time_to,   utf8_to_wide(t_to).c_str());

    m_config.custom_details = p.details;
    m_config.custom_state = p.state;
    m_config.custom_time_from = t_from;
    m_config.custom_time_to = t_to;
    m_config.custom_time_range = "from " + t_from + " to " + t_to;
    m_config.custom_large_image = p.large_image;
    m_config.custom_small_image = p.small_image;
    config::ConfigManager::save(m_config);

    discord::Activity act;
    act.details = p.details;
    act.state = p.state;
    act.large_image = resolve_image_url(p.large_image);
    act.large_text = p.large_text;
    act.small_image = resolve_image_url(p.small_image);
    act.small_text = p.small_text;
    bool in_slot = parse_time_parts(t_from, t_to, act.start_time, act.end_time);

    g_sched.active = true;
    g_sched.label = p.label;
    g_sched.time_from = t_from;
    g_sched.time_to = t_to;
    g_sched.act = act;

    if (!m_rpc.is_connected()) {
        on_rcnnct_clk();
    }

    if (m_rpc.is_connected()) {
        if (in_slot) {
            if (m_rpc.set_activity(act)) {
                m_activity_start_time = act.start_time;
                m_activity_end_time = act.end_time;
                g_sched.was_in_slot = true;
                g_sched.last_reassert = std::time(nullptr);
                upd_rpc_st_lbl("actif [" + p.label + "]", true);
                log_dbg("RPC", "Preset applique [" + p.label + "]: " + p.details + " / " + p.state);
            } else {
                upd_rpc_st_lbl("erreur envoi: " + m_rpc.get_last_error(), false);
                log_dbg("RPC", "Erreur envoi preset: " + m_rpc.get_last_error());
            }
        } else {
            m_rpc.clear_activity();
            m_activity_start_time = 0;
            m_activity_end_time = 0;
            g_sched.was_in_slot = false;
            upd_rpc_st_lbl("hors tranche (" + t_from + " - " + t_to + ") - activite masquee", true);
            log_dbg("RPC", "Preset [" + p.label + "] hors tranche active (" + t_from + " - " + t_to + "): activite masquee sur Discord.");
        }
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::on_clr_act_clk() {
    g_sched.active = false;
    g_sched.was_in_slot = false;
    m_activity_start_time = 0;
    m_activity_end_time = 0;
    if (m_rpc.is_connected()) {
        m_rpc.clear_activity();
        upd_rpc_st_lbl("activite effacee", true);
        log_dbg("RPC", "Activite Discord reinitialisee a vide.");
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::on_apply_cstm_clk() {
    m_config.custom_details = get_window_text_utf8(m_edit_details);
    m_config.custom_state = get_window_text_utf8(m_edit_state);
    m_config.custom_time_from = normalize_time_str(get_window_text_utf8(m_edit_time_from), "09:00");
    m_config.custom_time_to   = normalize_time_str(get_window_text_utf8(m_edit_time_to), "18:00");
    SetWindowTextW(m_edit_time_from, utf8_to_wide(m_config.custom_time_from).c_str());
    SetWindowTextW(m_edit_time_to,   utf8_to_wide(m_config.custom_time_to).c_str());
    m_config.custom_time_range = "from " + m_config.custom_time_from + " to " + m_config.custom_time_to;
    m_config.custom_large_image = resolve_image_url(get_window_text_utf8(m_edit_large_img));
    m_config.custom_small_image = resolve_image_url(get_window_text_utf8(m_edit_small_img));
    SetWindowTextW(m_edit_large_img, utf8_to_wide(m_config.custom_large_image).c_str());
    SetWindowTextW(m_edit_small_img, utf8_to_wide(m_config.custom_small_image).c_str());
    config::ConfigManager::save(m_config);

    discord::Activity act;
    act.details = m_config.custom_details;
    act.state = m_config.custom_state;
    act.large_image = m_config.custom_large_image;
    act.large_text = m_config.custom_details;
    act.small_image = m_config.custom_small_image;
    act.small_text = m_config.custom_state;
    bool in_slot = parse_time_parts(m_config.custom_time_from, m_config.custom_time_to, act.start_time, act.end_time);

    g_sched.active = true;
    g_sched.label = "Custom";
    g_sched.time_from = m_config.custom_time_from;
    g_sched.time_to = m_config.custom_time_to;
    g_sched.act = act;

    if (!m_rpc.is_connected()) {
        on_rcnnct_clk();
    }

    if (m_rpc.is_connected()) {
        if (in_slot) {
            if (m_rpc.set_activity(act)) {
                m_activity_start_time = act.start_time;
                m_activity_end_time = act.end_time;
                g_sched.was_in_slot = true;
                g_sched.last_reassert = std::time(nullptr);
                upd_rpc_st_lbl("activite personnalisee activee", true);
                log_dbg("RPC", "Activite manuelle appliquee: " + act.details + " / " + act.state + " [" + m_config.custom_time_range + "]");
            } else {
                upd_rpc_st_lbl("erreur transmission payload", true);
                log_dbg("RPC", "Erreur transmission activite: " + m_rpc.get_last_error());
            }
        } else {
            m_rpc.clear_activity();
            m_activity_start_time = 0;
            m_activity_end_time = 0;
            g_sched.was_in_slot = false;
            upd_rpc_st_lbl("hors tranche (" + m_config.custom_time_from + " - " + m_config.custom_time_to + ") - activite masquee", true);
            log_dbg("RPC", "Activite manuelle hors tranche active (" + m_config.custom_time_from + " - " + m_config.custom_time_to + "): activite masquee sur Discord.");
        }
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::on_sv_prst_clk() {
    std::string name = get_window_text_utf8(m_edit_preset_name);
    if (name.empty()) {
        name = "Custom";
        SetWindowTextW(m_edit_preset_name, L"Custom");
    }
    std::string details = get_window_text_utf8(m_edit_details);
    std::string state   = get_window_text_utf8(m_edit_state);
    std::string large     = resolve_image_url(get_window_text_utf8(m_edit_large_img));
    std::string small_img = resolve_image_url(get_window_text_utf8(m_edit_small_img));
    std::string t_from    = normalize_time_str(get_window_text_utf8(m_edit_time_from), "09:00");
    std::string t_to      = normalize_time_str(get_window_text_utf8(m_edit_time_to), "18:00");
    SetWindowTextW(m_edit_time_from, utf8_to_wide(t_from).c_str());
    SetWindowTextW(m_edit_time_to,   utf8_to_wide(t_to).c_str());
    std::string tr = "from " + t_from + " to " + t_to;

    bool found = false;
    for (auto& p : m_config.presets) {
        if (_stricmp(p.label.c_str(), name.c_str()) == 0) {
            p.details = details;
            p.state = state;
            p.time_from = t_from;
            p.time_to = t_to;
            p.time_range = tr;
            p.large_image = large;
            p.large_text = name;
            p.small_image = small_img;
            p.small_text = state;
            found = true;
            break;
        }
    }

    if (!found) {
        config::Preset new_p;
        new_p.id = "preset_" + std::to_string(std::time(nullptr));
        new_p.label = name;
        new_p.details = details;
        new_p.state = state;
        new_p.time_from = t_from;
        new_p.time_to = t_to;
        new_p.time_range = tr;
        new_p.large_image = large;
        new_p.large_text = name;
        new_p.small_image = small_img;
        new_p.small_text = state;
        m_config.presets.push_back(new_p);
    }

    config::ConfigManager::save(m_config);
    log_dbg("CFG", "Statut enregistre: [" + name + "] (" + std::to_string(m_config.presets.size()) + " presets au total)");

    rcrt_prst_btns();
    on_apply_cstm_clk();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void MainWindow::on_dlt_prst_clk() {
    std::string name = get_window_text_utf8(m_edit_preset_name);
    if (name.empty()) return;

    auto it = std::find_if(m_config.presets.begin(), m_config.presets.end(), [&](const config::Preset& p) {
        return _stricmp(p.label.c_str(), name.c_str()) == 0;
    });

    if (it != m_config.presets.end()) {
        std::string deleted_name = it->label;
        m_config.presets.erase(it);
        config::ConfigManager::save(m_config);
        log_dbg("CFG", "Preset supprime: [" + deleted_name + "]");

        rcrt_prst_btns();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void MainWindow::rcrt_prst_btns() {
    for (HWND btn : m_btn_presets) {
        if (btn && IsWindow(btn)) {
            auto it = std::find(m_activity_controls.begin(), m_activity_controls.end(), btn);
            if (it != m_activity_controls.end()) {
                m_activity_controls.erase(it);
            }
            DestroyWindow(btn);
        }
    }
    m_btn_presets.clear();

    for (size_t i = 0; i < m_config.presets.size(); ++i) {
        std::wstring btn_label = L"[ " + utf8_to_wide(m_config.presets[i].label) + L" ]";
        std::transform(btn_label.begin(), btn_label.end(), btn_label.begin(), ::towupper);

        HWND btn = CreateWindowExW(
            0, L"BUTTON", btn_label.c_str(),
            WS_CHILD | (m_current_view == ViewMode::Activity ? WS_VISIBLE : 0) | BS_OWNERDRAW,
            0, 0, 10, 10, m_hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_BTN_PRESET_BASE + i)), m_instance, nullptr
        );
        m_btn_presets.push_back(btn);
        m_activity_controls.push_back(btn);
    }

    RECT client_rc;
    GetClientRect(m_hwnd, &client_rc);
    rlyt_ctrls(client_rc.right - client_rc.left, client_rc.bottom - client_rc.top);
}

// ==========================================
// ACTIONS VUE 2 (DEBUG & REGLAGES)
// ==========================================
void MainWindow::on_dbg_tst_ipc_clk() {
    std::string cid = get_window_text_utf8(m_dbg_edit_client_id);
    log_dbg("IPC", "Test IPC en cours avec client_id: " + (cid.empty() ? "(defaut)" : cid) + "...");
    SetWindowTextW(m_dbg_label_ipc_status, L"statut ipc: test en cours...");

    discord::RpcClient test_client;
    if (test_client.connect(cid)) {
        std::string user = test_client.get_connected_user();
        std::string pipe = test_client.get_active_pipe_path();
        std::string msg = "statut ipc: succes (" + user + " via " + pipe + ")";
        SetWindowTextW(m_dbg_label_ipc_status, utf8_to_wide(msg).c_str());
        SetWindowTextW(m_dbg_label_pipe, utf8_to_wide(pipe).c_str());
        SetWindowTextW(m_dbg_label_user, utf8_to_wide(user).c_str());
        log_dbg("IPC", "Test reussi: compte detecte " + user + " sur " + pipe);
        test_client.disconnect();
    } else {
        std::string err = "statut ipc: echec (" + test_client.get_last_error() + ")";
        SetWindowTextW(m_dbg_label_ipc_status, utf8_to_wide(err).c_str());
        log_dbg("IPC", "Echec test IPC: " + test_client.get_last_error());
    }
}

void MainWindow::on_dbg_rcnnct_clk() {
    m_config.client_id = get_window_text_utf8(m_dbg_edit_client_id);
    on_rcnnct_clk();
}

void MainWindow::on_dbg_dscnct_clk() {
    log_dbg("IPC", "Fermeture manuelle de la connexion IPC...");
    m_rpc.disconnect();
    upd_rpc_st_lbl("deconnecte manuellement", false);
    SetWindowTextW(m_dbg_label_pipe, L"deconnecte");
    SetWindowTextW(m_dbg_label_ipc_status, L"statut ipc: deconnecte");

    if (m_avatar_bitmap) {
        delete m_avatar_bitmap;
        m_avatar_bitmap = nullptr;
    }
    m_current_avatar_url.clear();

    if (m_decor_bitmap) {
        delete m_decor_bitmap;
        m_decor_bitmap = nullptr;
    }
    m_current_decor_url.clear();

    free_loaded_image(m_banner_bitmap, m_banner_stream);
    m_current_banner_url.clear();
    m_banner_frame_index = 0;
    m_banner_frame_count = 1;
    m_last_profile_lookup_id.clear();

    m_dominant_banner_color = RGB(32, 34, 37);
    m_has_dominant_banner_color = false;

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::on_dbg_tst_cal_clk() {
    std::string url = get_window_text_utf8(m_dbg_edit_ical_url);
    if (url.empty()) {
        SetWindowTextW(m_dbg_label_cal_status, L"statut agenda: url ical vide");
        log_dbg("CAL", "Erreur: URL d'agenda iCal vide.");
        return;
    }

    log_dbg("CAL", "Requete TLS WinHTTP vers l'agenda iCal...");
    SetWindowTextW(m_dbg_label_cal_status, L"statut agenda: requete winhttp tls en cours...");

    HWND target = m_hwnd;
    std::thread([this, target, url]() {
        calendar::CalendarClient test_cal;
        std::string content;
        std::string error;
        bool ok = test_cal.fetch_ical(url, content, error);

        if (ok) {
            auto events = test_cal.parse_events(content);
            auto cur = test_cal.get_current_event(events);
            std::string msg = "statut agenda: flux valide (" + std::to_string(events.size()) + " evenements detectes";
            if (cur) {
                msg += ", actif: " + cur->summary;
            } else {
                msg += ", aucun actif maintenant";
            }
            msg += ")";

            PostMessageW(target, WM_COMMAND, MAKEWPARAM(ID_DBG_TEST_CAL, 1), 0);
            log_dbg("CAL", msg);
        } else {
            std::string msg = "statut agenda: echec (" + error + ")";
            log_dbg("CAL", msg);
        }
    }).detach();
}

void MainWindow::on_dbg_frc_sync_clk() {
    m_config.ical_url = get_window_text_utf8(m_dbg_edit_ical_url);
    SetWindowTextW(m_edit_ical_url, utf8_to_wide(m_config.ical_url).c_str());
    log_dbg("CAL", "Synchronisation forcee declenchee.");
    trg_async_cal_sync();
}

void MainWindow::on_dbg_sv_cfg_clk() {
    m_config.client_id = get_window_text_utf8(m_dbg_edit_client_id);
    m_config.ical_url  = get_window_text_utf8(m_dbg_edit_ical_url);
    SetWindowTextW(m_edit_ical_url, utf8_to_wide(m_config.ical_url).c_str());

    bool autostart_checked = (SendMessageW(m_dbg_chk_autostart, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_config.autostart = autostart_checked;
    config::ConfigManager::set_autostart(autostart_checked);

    bool auto_sync_checked = (SendMessageW(m_dbg_chk_auto_sync, BM_GETCHECK, 0, 0) == BST_CHECKED);
    m_config.auto_sync = auto_sync_checked;
    SendMessageW(m_chk_auto_sync, BM_SETCHECK, auto_sync_checked ? BST_CHECKED : BST_UNCHECKED, 0);

    std::string interval_str = get_window_text_utf8(m_dbg_edit_interval);
    try {
        m_config.sync_interval_seconds = (std::max)(10, std::stoi(interval_str));
    } catch (...) {
        m_config.sync_interval_seconds = 60;
    }

    m_config.setup_completed = true;
    config::ConfigManager::save(m_config);

    log_dbg("CFG", "Configuration enregistree avec succes (autostart: " + std::to_string(m_config.autostart) + ", intervalle: " + std::to_string(m_config.sync_interval_seconds) + "s)");

    KillTimer(m_hwnd, TIMER_ID_CALENDAR);
    if (m_config.auto_sync && !m_config.ical_url.empty()) {
        SetTimer(m_hwnd, TIMER_ID_CALENDAR, static_cast<UINT>(m_config.sync_interval_seconds * 1000), nullptr);
    }
}

void MainWindow::on_dbg_clr_log_clk() {
    if (m_dbg_edit_log && IsWindow(m_dbg_edit_log)) {
        SetWindowTextW(m_dbg_edit_log, L"");
    }
}

void MainWindow::on_sync_cal_clk() {
    std::string ical_url = get_wnd_txt_u8(m_edit_ical_url);
    m_config.ical_url = ical_url;
    SetWindowTextW(m_dbg_edit_ical_url, u8_to_wd(ical_url).c_str());
    m_config.auto_sync = (SendMessageW(m_chk_auto_sync, BM_GETCHECK, 0, 0) == BST_CHECKED);
    config::ConfigManager::save(m_config);

    trg_async_cal_sync();
}

void MainWindow::trg_img_dwnld(ImageType type, const std::string& url) {
    if (url.empty()) return;
    if (type == ImageType::Avatar) {
        if (m_avatar_loading.exchange(true)) return;
    } else if (type == ImageType::Decoration) {
        if (m_decor_loading.exchange(true)) return;
    } else if (type == ImageType::Banner) {
        if (m_banner_loading.exchange(true)) return;
    } else {
        if (m_asset_loading.exchange(true)) return;
    }

    HWND target_hwnd = m_hwnd;
    std::thread([target_hwnd, type, url]() {
        auto result = new ImageDownloadResult();
        result->type = type;
        result->url = url;
        download_url_bytes(url, result->data);
        PostMessageW(target_hwnd, WM_IMAGE_DOWNLOADED, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void MainWindow::trg_prfl_lkp(const std::string& uid) {
    if (uid.empty()) return;
    if (m_profile_lookup_in_progress.exchange(true)) return;

    HWND trgt = m_hwnd;
    std::thread([this, trgt, uid]() {
        std::string url = "https://japi.rest/discord/v1/user/" + uid;
        std::vector<uint8_t> dt;
        bool ok = dwnld_url_bts(url, dt);
        if (ok && !dt.empty()) {
            std::string jsn(reinterpret_cast<const char*>(dt.data()), dt.size());

            std::string bnr_hsh = discord::ext_json_fld(jsn, "banner");
            std::string bnr_url = discord::ext_json_fld(jsn, "bannerURL");
            std::string bnr_clr_hx = discord::ext_json_fld(jsn, "banner_color");
            std::string accnt_str = discord::ext_json_fld(jsn, "accent_color");

            if (bnr_url.empty() && !bnr_hsh.empty() && bnr_hsh != "null") {
                std::string ext = (bnr_hsh.rfind("a_", 0) == 0) ? ".gif" : ".png";
                bnr_url = "https://cdn.discordapp.com/banners/" + uid + "/" + bnr_hsh + ext + "?size=480";
            }

            COLORREF bnr_col = RGB(35, 39, 42);
            bool has_col = false;
            if (!bnr_clr_hx.empty() && bnr_clr_hx[0] == '#') {
                try {
                    unsigned long vl = std::stoul(bnr_clr_hx.substr(1), nullptr, 16);
                    bnr_col = RGB((vl >> 16) & 0xFF, (vl >> 8) & 0xFF, vl & 0xFF);
                    has_col = true;
                } catch (...) {}
            } else if (!accnt_str.empty()) {
                try {
                    unsigned long vl = std::stoul(accnt_str);
                    bnr_col = RGB((vl >> 16) & 0xFF, (vl >> 8) & 0xFF, vl & 0xFF);
                    has_col = true;
                } catch (...) {}
            }

            m_rpc.set_bnr_inf(bnr_hsh, bnr_url, bnr_col, has_col);

            if (!bnr_url.empty()) {
                PostMessageW(trgt, WM_USER_PROFILE_UPDATED, 0, 0);
            }
        }
        m_profile_lookup_in_progress = false;
    }).detach();
}

void MainWindow::trg_async_cal_sync() {
    if (m_sync_in_progress.exchange(true)) {
        return;
    }

    std::string url = get_wnd_txt_u8(m_edit_ical_url);
    if (url.empty()) {
        m_sync_in_progress = false;
        upd_cal_st_lbl("url ical non definie", false);
        return;
    }

    upd_cal_st_lbl("requete tls en cours...", false);
    log_dbg("CAL", "Recuperation du flux iCal via TLS...");

    HWND trgt_hwnd = m_hwnd;
    std::thread([this, trgt_hwnd, url]() {
        std::string cnt;
        std::string err;
        CalSyncRes* res = new CalSyncRes();

        if (m_calendar.ftch_cal(url, cnt, err)) {
            auto evs = m_calendar.prs_evs(cnt);
            auto crr = m_calendar.get_crr_ev(evs);
            res->success = true;
            if (crr) {
                res->summary = crr->smm();
                res->end_time = crr->tm_e();
            }
        } else {
            res->success = false;
            res->summary = err;
        }

        PostMessageW(trgt_hwnd, WM_CALENDAR_SYNC_DONE, 0, reinterpret_cast<LPARAM>(res));
    }).detach();
}

void MainWindow::hndl_cal_sync_cmplt(bool ok, const std::string& smm, std::time_t tm_e) {
    m_sync_in_progress = false;

    if (!ok) {
        upd_cal_st_lbl("erreur: " + smm, false);
        log_dbg("CAL", "Erreur synchronisation agenda: " + smm);
        return;
    }

    if (smm.empty()) {
        upd_cal_st_lbl("aucun evenement actif", false);
        log_dbg("CAL", "Synchronisation reussie: aucun evenement en cours.");
        return;
    }

    struct tm end_tm = {};
    localtime_s(&end_tm, &tm_e);
    char tm_buf[32];
    std::strftime(tm_buf, sizeof(tm_buf), "%H:%M", &end_tm);

    std::string st_txt = smm + " (fin: " + tm_buf + ")";
    upd_cal_st_lbl(st_txt, true);
    log_dbg("CAL", "Evenement actif detecte: " + smm + " jusqu'a " + tm_buf);

    if (m_rpc.is_cnnctd()) {
        discord::Actvty act;
        act.details = smm;
        act.state = "Fin a " + std::string(tm_buf);
        act.end_time = tm_e;

        if (!m_config.custom_large_image.empty()) {
            act.large_image = m_config.custom_large_image;
            act.large_text = smm;
        }
        if (!m_config.custom_small_image.empty()) {
            act.small_image = m_config.custom_small_image;
            act.small_text = "Agenda Google";
        }

        m_activity_start_time = std::time(nullptr);
        m_rpc.set_actvty(act);
        log_dbg("RPC", "Rich Presence mise a jour depuis l'agenda: " + smm);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void MainWindow::init_try_icn() {
    std::memset(&m_tray_data, 0, sizeof(m_tray_data));
    m_tray_data.cbSize = sizeof(NOTIFYICONDATAW);
    m_tray_data.hWnd = m_hwnd;
    m_tray_data.uID = 1;
    m_tray_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_tray_data.uCallbackMessage = WM_TRAY_NOTIFY;
    m_tray_data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(m_tray_data.szTip, L"At-Work");

    Shell_NotifyIconW(NIM_ADD, &m_tray_data);
    m_tray_icon_added = true;
}

void MainWindow::rm_try_icn() {
    if (m_tray_icon_added) {
        Shell_NotifyIconW(NIM_DELETE, &m_tray_data);
        m_tray_icon_added = false;
    }
}

void MainWindow::shw_try_mnu() {
    POINT pt;
    GetCursorPos(&pt);

    HMENU mnu = CreatePopupMenu();
    InsertMenuW(mnu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_OPEN, L"Ouvrir");
    InsertMenuW(mnu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_SETTINGS, L"Debug & Reglages");
    InsertMenuW(mnu, 2, MF_BYPOSITION | MF_STRING, ID_TRAY_SYNC, L"Synchroniser agenda");
    InsertMenuW(mnu, 3, MF_BYPOSITION | MF_STRING, ID_TRAY_CLEAR, L"Effacer activite");
    InsertMenuW(mnu, 4, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(mnu, 5, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Quitter");

    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(mnu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(mnu);
}

LRESULT CALLBACK MainWindow::wnd_prc_thnk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* slf = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        slf = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(slf));
    } else {
        slf = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (slf) {
        return slf->hndl_msg(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::hndl_msg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
            if (hit == HTCLIENT) {
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ScreenToClient(hwnd, &pt);
                // Le bandeau superieur tactique (0..42) agit comme barre de titre deplacable
                if (pt.y >= 0 && pt.y <= 42) {
                    // Verifier si le curseur est sur l'un des boutons d'onglets
                    POINT screen_pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                    RECT rc1, rc2;
                    if (m_btn_tab_activity && GetWindowRect(m_btn_tab_activity, &rc1) && PtInRect(&rc1, screen_pt)) {
                        return HTCLIENT;
                    }
                    if (m_btn_tab_debug && GetWindowRect(m_btn_tab_debug, &rc2) && PtInRect(&rc2, screen_pt)) {
                        return HTCLIENT;
                    }
                    return HTCAPTION;
                }
            }
            return hit;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (pt.y >= 0 && pt.y <= 42) {
                POINT screen_pt = pt;
                ClientToScreen(hwnd, &screen_pt);
                RECT rc1, rc2;
                bool on_btn = false;
                if (m_btn_tab_activity && GetWindowRect(m_btn_tab_activity, &rc1) && PtInRect(&rc1, screen_pt)) on_btn = true;
                if (m_btn_tab_debug && GetWindowRect(m_btn_tab_debug, &rc2) && PtInRect(&rc2, screen_pt)) on_btn = true;
                if (!on_btn) {
                    ReleaseCapture();
                    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                    return 0;
                }
            }
            break;
        }

        case WM_GETMINMAXINFO: {
            auto mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 580;
            mmi->ptMinTrackSize.y = 760;
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            pnt_wnd(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DRAWITEM: {
            auto dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
            if (dis && dis->CtlType == ODT_BUTTON) {
                drw_ownr_btn(dis);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc_static = reinterpret_cast<HDC>(wp);
            SetBkMode(hdc_static, TRANSPARENT);
            SetTextColor(hdc_static, theme::COLOR_FG);
            return reinterpret_cast<LRESULT>(m_brush_pane);
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc_edit = reinterpret_cast<HDC>(wp);
            SetBkColor(hdc_edit, theme::COLOR_MID);
            SetTextColor(hdc_edit, theme::COLOR_FG);
            return reinterpret_cast<LRESULT>(m_brush_mid);
        }

        case WM_COMMAND: {
            HWND ctrl_hwnd = reinterpret_cast<HWND>(lp);
            if (HIWORD(wp) == EN_CHANGE) {
                if (ctrl_hwnd && (ctrl_hwnd == m_edit_time_from || ctrl_hwnd == m_edit_time_to)) {
                    static bool s_formatting = false;
                    static std::wstring s_prev_from = L"09:00";
                    static std::wstring s_prev_to   = L"18:00";
                    if (!s_formatting) {
                        std::wstring& prev = (ctrl_hwnd == m_edit_time_from) ? s_prev_from : s_prev_to;
                        int len = GetWindowTextLengthW(ctrl_hwnd);
                        std::wstring raw(len > 0 ? len : 0, L'\0');
                        if (len > 0) {
                            GetWindowTextW(ctrl_hwnd, &raw[0], len + 1);
                        }
                        bool is_deleting = (raw.size() < prev.size());
                        std::wstring digits;
                        for (wchar_t c : raw) {
                            if (c >= L'0' && c <= L'9' && digits.size() < 4) {
                                digits.push_back(c);
                            }
                        }
                        std::wstring formatted;
                        if (digits.size() >= 3) {
                            formatted = digits.substr(0, 2) + L":" + digits.substr(2);
                        } else if (digits.size() == 2) {
                            formatted = is_deleting ? digits : (digits + L":");
                        } else {
                            formatted = digits;
                        }
                        prev = formatted;
                        if (formatted != raw) {
                            s_formatting = true;
                            SetWindowTextW(ctrl_hwnd, formatted.c_str());
                            int end_pos = static_cast<int>(formatted.size());
                            SendMessageW(ctrl_hwnd, EM_SETSEL, end_pos, end_pos);
                            s_formatting = false;
                        }
                    }
                }
                if (m_current_view == ViewMode::Activity && m_show_preview) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (HIWORD(wp) == EN_KILLFOCUS) {
                if (ctrl_hwnd && (ctrl_hwnd == m_edit_time_from || ctrl_hwnd == m_edit_time_to)) {
                    std::string cur = get_window_text_utf8(ctrl_hwnd);
                    if (!cur.empty()) {
                        std::string fallback = (ctrl_hwnd == m_edit_time_from) ? "09:00" : "18:00";
                        std::string norm = normalize_time_str(cur, fallback);
                        if (norm != cur) {
                            SetWindowTextW(ctrl_hwnd, utf8_to_wide(norm).c_str());
                        }
                    }
                }
            }
            int id = LOWORD(wp);

            // Onglets
            if (id == ID_TAB_ACTIVITY) {
                swtch_vw(ViewMode::Activity);
                return 0;
            } else if (id == ID_TAB_DEBUG) {
                swtch_vw(ViewMode::DebugSettings);
                return 0;
            }

            // Vue 1 : Activite
            if (id == ID_BTN_CONNECT) {
                on_rcnnct_clk();
                return 0;
            } else if (id == ID_BTN_SYNC_NOW) {
                on_sync_cal_clk();
                return 0;
            } else if (id == ID_BTN_CLEAR_ACT) {
                on_clr_act_clk();
                return 0;
            } else if (id == ID_BTN_APPLY_CUSTOM) {
                on_apply_cstm_clk();
                return 0;
            } else if (id == ID_BTN_SAVE_PRESET) {
                on_sv_prst_clk();
                return 0;
            } else if (id == ID_BTN_DELETE_PRESET) {
                on_dlt_prst_clk();
                return 0;
            } else if (id >= ID_BTN_PRESET_BASE && id < ID_BTN_PRESET_BASE + static_cast<int>(m_config.presets.size())) {
                on_prst_clk(id - ID_BTN_PRESET_BASE);
                return 0;
            }

            // Vue 2 : Debug & Reglages
            if (id == ID_DBG_TEST_IPC) {
                on_dbg_tst_ipc_clk();
                return 0;
            } else if (id == ID_DBG_RECONNECT) {
                on_dbg_rcnnct_clk();
                return 0;
            } else if (id == ID_DBG_DISCONNECT) {
                on_dbg_dscnct_clk();
                return 0;
            } else if (id == ID_DBG_TEST_CAL) {
                on_dbg_tst_cal_clk();
                return 0;
            } else if (id == ID_DBG_FORCE_SYNC) {
                on_dbg_frc_sync_clk();
                return 0;
            } else if (id == ID_DBG_SAVE_CONFIG) {
                on_dbg_sv_cfg_clk();
                return 0;
            } else if (id == ID_DBG_CLEAR_LOG) {
                on_dbg_clr_log_clk();
                return 0;
            }

            // Systray
            if (id == ID_TRAY_OPEN) {
                ShowWindow(m_hwnd, SW_SHOW);
                ShowWindow(m_hwnd, SW_RESTORE);
                SetForegroundWindow(m_hwnd);
                return 0;
            } else if (id == ID_TRAY_SETTINGS) {
                ShowWindow(m_hwnd, SW_SHOW);
                ShowWindow(m_hwnd, SW_RESTORE);
                SetForegroundWindow(m_hwnd);
                swtch_vw(ViewMode::DebugSettings);
                return 0;
            } else if (id == ID_TRAY_SYNC) {
                trg_async_cal_sync();
                return 0;
            } else if (id == ID_TRAY_CLEAR) {
                on_clr_act_clk();
                return 0;
            } else if (id == ID_TRAY_EXIT) {
                DestroyWindow(m_hwnd);
                return 0;
            }
            break;
        }

        case WM_SIZE: {
            if (wp == SIZE_MINIMIZED) {
                ShowWindow(m_hwnd, SW_HIDE);
                return 0;
            }
            int nw_w = LOWORD(lp);
            int nw_h = HIWORD(lp);
            rlyt_ctrls(nw_w, nw_h);
            return 0;
        }

        case WM_TRAY_NOTIFY: {
            if (lp == WM_LBUTTONDBLCLK) {
                ShowWindow(m_hwnd, SW_SHOW);
                ShowWindow(m_hwnd, SW_RESTORE);
                SetForegroundWindow(m_hwnd);
            } else if (lp == WM_RBUTTONUP) {
                shw_try_mnu();
            }
            return 0;
        }

        case WM_CALENDAR_SYNC_DONE: {
            auto res = reinterpret_cast<CalSyncRes*>(lp);
            if (res) {
                hndl_cal_sync_cmplt(res->success, res->summary, res->end_time);
                delete res;
            }
            return 0;
        }

        case WM_IMAGE_DOWNLOADED: {
            auto res = reinterpret_cast<ImgDwnldRes*>(lp);
            if (res) {
                if (res->type == ImageType::Avatar) {
                    m_avatar_loading = false;
                    m_current_avatar_url = res->url;
                    if (!res->data.empty()) {
                        Gdiplus::Bitmap* bmp = ld_bmp_bts(res->data);
                        if (bmp) {
                            delete m_avatar_bitmap;
                            m_avatar_bitmap = bmp;
                            m_dominant_banner_color = ext_dmnnt_clr(bmp);
                            m_has_dominant_banner_color = true;
                        }
                    }
                } else if (res->type == ImageType::Decoration) {
                    m_decor_loading = false;
                    m_current_decor_url = res->url;
                    if (!res->data.empty()) {
                        Gdiplus::Bitmap* bmp = ld_bmp_bts(res->data);
                        if (bmp) {
                            delete m_decor_bitmap;
                            m_decor_bitmap = bmp;
                        }
                    }
                } else if (res->type == ImageType::Banner) {
                    m_banner_loading = false;
                    m_current_banner_url = res->url;
                    if (!res->data.empty()) {
                        LdImgDt limg = ld_img_bts(res->data);
                        if (limg.bitmap) {
                            fr_ld_img(m_banner_bitmap, m_banner_stream);
                            m_banner_bitmap = limg.bitmap;
                            m_banner_stream = limg.stream;
                            m_banner_frame_count = limg.frame_count;
                            m_banner_frame_index = 0;
                        }
                    }
                } else {
                    m_asset_loading = false;
                    m_current_asset_url = res->url;
                    if (!res->data.empty()) {
                        LdImgDt limg = ld_img_bts(res->data);
                        if (limg.bitmap) {
                            fr_ld_img(m_asset_bitmap, m_asset_stream);
                            m_asset_bitmap = limg.bitmap;
                            m_asset_stream = limg.stream;
                            m_asset_frame_count = limg.frame_count;
                            m_asset_frame_index = 0;
                        }
                    }
                }
                delete res;
                if (m_current_view == ViewMode::Activity && m_show_preview) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_USER_PROFILE_UPDATED: {
            auto profile = m_rpc.get_prfl();
            if (!profile.banner_url.empty() && profile.banner_url != m_current_banner_url && !m_banner_loading) {
                trg_img_dwnld(ImageType::Banner, profile.banner_url);
            }
            if (m_current_view == ViewMode::Activity && m_show_preview) {
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_TIMER: {
            if (wp == TIMER_ID_ANIM) {
                if (m_current_view == ViewMode::Activity && m_show_preview) {
                    bool need_redraw = false;
                    if (m_banner_bitmap && m_banner_frame_count > 1) {
                        GUID dim = Gdiplus::FrameDimensionTime;
                        m_banner_frame_index = (m_banner_frame_index + 1) % m_banner_frame_count;
                        m_banner_bitmap->SelectActiveFrame(&dim, m_banner_frame_index);
                        need_redraw = true;
                    }
                    if (m_asset_bitmap && m_asset_frame_count > 1) {
                        GUID dim = Gdiplus::FrameDimensionTime;
                        m_asset_frame_index = (m_asset_frame_index + 1) % m_asset_frame_count;
                        m_asset_bitmap->SelectActiveFrame(&dim, m_asset_frame_index);
                        need_redraw = true;
                    }
                    if (need_redraw) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    }
                }
                return 0;
            } else if (wp == TIMER_ID_CALENDAR) {
                if (SendMessageW(m_chk_auto_sync, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    trg_async_cal_sync();
                }
                return 0;
            } else if (wp == TIMER_ID_TICK) {
                std::time_t now = std::time(nullptr);
                if (g_sched.active && m_rpc.is_connected()) {
                    bool in_slot = parse_time_parts(g_sched.time_from, g_sched.time_to, g_sched.act.start_time, g_sched.act.end_time);
                    if (in_slot) {
                        m_activity_start_time = g_sched.act.start_time;
                        m_activity_end_time = g_sched.act.end_time;
                        if (!g_sched.was_in_slot || (now - g_sched.last_reassert >= 10)) {
                            if (m_rpc.set_activity(g_sched.act)) {
                                if (!g_sched.was_in_slot) {
                                    upd_rpc_st_lbl("actif [" + g_sched.label + "]", true);
                                    log_dbg("RPC", "Debut de tranche active (" + g_sched.time_from + " - " + g_sched.time_to + "): activite affichee.");
                                }
                                g_sched.was_in_slot = true;
                                g_sched.last_reassert = now;
                            }
                        }
                    } else if (g_sched.was_in_slot) {
                        m_rpc.clear_activity();
                        m_activity_start_time = 0;
                        m_activity_end_time = 0;
                        g_sched.was_in_slot = false;
                        upd_rpc_st_lbl("hors tranche (" + g_sched.time_from + " - " + g_sched.time_to + ") - activite masquee", true);
                        log_dbg("RPC", "Fin de tranche active (" + g_sched.time_from + " - " + g_sched.time_to + "): activite effacee.");
                    }
                } else if (m_cal_is_active && m_activity_end_time > 0 && now >= m_activity_end_time) {
                    if (m_rpc.is_connected()) {
                        m_rpc.clear_activity();
                    }
                    m_activity_start_time = 0;
                    m_activity_end_time = 0;
                    upd_cal_st_lbl("aucun evenement actif", false);
                }
                if (m_current_view == ViewMode::Activity && m_show_preview) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;
        }

        case WM_CLOSE: {
            ShowWindow(m_hwnd, SW_HIDE);
            return 0;
        }

        case WM_DESTROY: {
            KillTimer(m_hwnd, TIMER_ID_ANIM);
            KillTimer(m_hwnd, TIMER_ID_CALENDAR);
            KillTimer(m_hwnd, TIMER_ID_TICK);
            PostQuitMessage(0);
            return 0;
        }

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int MainWindow::run_msg_lp() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace ui
