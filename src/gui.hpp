#pragma once

#include <windows.h>
#include <objidl.h>
#include <shellapi.h>
#include <gdiplus.h>

#ifdef small
#undef small
#endif
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include "discord_ipc.hpp"
#include "calendar_sync.hpp"
#include "config.hpp"

namespace ui {

// STUDIO DESIGN SYSTEM - Tactical Industrial Slate
namespace theme {
    constexpr COLORREF COLOR_BG      = RGB(84, 94, 96);    // #545e60 Slate Khaki
    constexpr COLORREF COLOR_PANE    = RGB(42, 53, 54);    // #2a3536 Tactical Slate Grey
    constexpr COLORREF COLOR_MID     = RGB(22, 35, 38);    // #162326 Deep Obsidian Petrol
    constexpr COLORREF COLOR_FG      = RGB(235, 230, 202); // #ebe6ca Bone Parchment
    constexpr COLORREF COLOR_LINE    = RGB(205, 204, 178); // #cdccb2 Wireframe Sand Bone
    constexpr COLORREF COLOR_ACCENT  = RGB(151, 177, 170); // #97b1aa Faded Sage Teal

    constexpr COLORREF COLOR_ONLINE  = RGB(74, 222, 128);  // #4ade80 Neon Phosphor Green
    constexpr COLORREF COLOR_OFFLINE = RGB(102, 102, 102); // #666666 Neutral Matte Grey
    constexpr COLORREF COLOR_DANGER  = RGB(239, 68, 68);   // #ef4444 Signal Red

    constexpr COLORREF CLR_BG        = COLOR_BG;
    constexpr COLORREF CLR_PN        = COLOR_PANE;
    constexpr COLORREF CLR_MD        = COLOR_MID;
    constexpr COLORREF CLR_FG        = COLOR_FG;
    constexpr COLORREF CLR_LN        = COLOR_LINE;
    constexpr COLORREF CLR_ACC       = COLOR_ACCENT;
    constexpr COLORREF CLR_ONLN      = COLOR_ONLINE;
    constexpr COLORREF CLR_OFFLN     = COLOR_OFFLINE;
    constexpr COLORREF CLR_DNGR      = COLOR_DANGER;
}

enum class ViewMode {
    Activity,
    DebugSettings
};
using VwMd = ViewMode;

class MainWindow {
public:
    enum class ImageType { Avatar, Asset, Decoration, Banner };

    MainWindow();
    ~MainWindow();

    bool crt(HINSTANCE h_inst, int cmd_shw);
    bool create(HINSTANCE h_inst, int cmd_shw) { return crt(h_inst, cmd_shw); }

    int run_msg_lp();
    int run_message_loop() { return run_msg_lp(); }

private:
    static LRESULT CALLBACK wnd_prc_thnk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK window_proc_thunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return wnd_prc_thnk(hwnd, msg, wp, lp); }

    LRESULT hndl_msg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return hndl_msg(hwnd, msg, wp, lp); }

    void init_thm_rsrc();
    void cleanup_theme_resources() { cln_thm_rsrc(); }
    void cln_thm_rsrc();
    void init_theme_resources() { init_thm_rsrc(); }

    void init_ctrls(HWND hwnd);
    void init_controls(HWND hwnd) { init_ctrls(hwnd); }

    void rlyt_ctrls(int w, int h);
    void relayout_controls(int w, int h) { rlyt_ctrls(w, h); }

    void swtch_vw(ViewMode md);
    void switch_view(ViewMode md) { swtch_vw(md); }

    void log_dbg(const std::string& tg, const std::string& msg);
    void log_debug(const std::string& tg, const std::string& msg) { log_dbg(tg, msg); }

    void pnt_wnd(HDC hdc);
    void paint_window(HDC hdc) { pnt_wnd(hdc); }

    void drw_ownr_btn(LPDRAWITEMSTRUCT dis);
    void draw_owner_button(LPDRAWITEMSTRUCT dis) { drw_ownr_btn(dis); }

    void upd_rpc_st_lbl(const std::string& txt, bool onln);
    void update_rpc_status_label(const std::string& txt, bool onln) { upd_rpc_st_lbl(txt, onln); }

    void upd_cal_st_lbl(const std::string& txt, bool actv);
    void update_cal_status_label(const std::string& txt, bool actv) { upd_cal_st_lbl(txt, actv); }

    // View 1 (Activity) Actions
    void on_rcnnct_clk();
    void on_reconnect_clicked() { on_rcnnct_clk(); }

    void on_sync_cal_clk();
    void on_sync_calendar_clicked() { on_sync_cal_clk(); }

    void on_prst_clk(size_t idx);
    void on_preset_clicked(size_t idx) { on_prst_clk(idx); }

    void on_clr_act_clk();
    void on_clear_activity_clicked() { on_clr_act_clk(); }

    void on_apply_cstm_clk();
    void on_apply_custom_clicked() { on_apply_cstm_clk(); }

    void on_sv_prst_clk();
    void on_save_preset_clicked() { on_sv_prst_clk(); }

    void on_dlt_prst_clk();
    void on_delete_preset_clicked() { on_dlt_prst_clk(); }

    void rcrt_prst_btns();
    void recreate_preset_buttons() { rcrt_prst_btns(); }

    // View 2 (Debug & Settings) Actions
    void on_dbg_tst_ipc_clk();
    void on_dbg_test_ipc_clicked() { on_dbg_tst_ipc_clk(); }

    void on_dbg_rcnnct_clk();
    void on_dbg_reconnect_clicked() { on_dbg_rcnnct_clk(); }

    void on_dbg_dscnct_clk();
    void on_dbg_disconnect_clicked() { on_dbg_dscnct_clk(); }

    void on_dbg_tst_cal_clk();
    void on_dbg_test_cal_clicked() { on_dbg_tst_cal_clk(); }

    void on_dbg_frc_sync_clk();
    void on_dbg_force_sync_clicked() { on_dbg_frc_sync_clk(); }

    void on_dbg_sv_cfg_clk();
    void on_dbg_save_config_clicked() { on_dbg_sv_cfg_clk(); }

    void on_dbg_clr_log_clk();
    void on_dbg_clear_log_clicked() { on_dbg_clr_log_clk(); }

    void trg_async_cal_sync();
    void trigger_async_calendar_sync() { trg_async_cal_sync(); }

    void hndl_cal_sync_cmplt(bool ok, const std::string& smm, std::time_t tm_e);
    void handle_calendar_sync_completed(bool ok, const std::string& smm, std::time_t tm_e) { hndl_cal_sync_cmplt(ok, smm, tm_e); }

    void drw_dsc_prvw(HDC hdc, const RECT& rc_prvw);
    void draw_discord_preview(HDC hdc, const RECT& rc_prvw) { drw_dsc_prvw(hdc, rc_prvw); }

    void trg_img_dwnld(ImageType tp, const std::string& url);
    void trigger_image_download(ImageType tp, const std::string& url) { trg_img_dwnld(tp, url); }

    void trg_prfl_lkp(const std::string& uid);
    void trigger_profile_lookup(const std::string& uid) { trg_prfl_lkp(uid); }

    void init_try_icn();
    void init_tray_icon() { init_try_icn(); }

    void rm_try_icn();
    void remove_tray_icon() { rm_try_icn(); }

    void shw_try_mnu();
    void show_tray_menu() { shw_try_mnu(); }

    HWND m_hwnd;
    HINSTANCE m_instance;
    ViewMode m_current_view;

    // Theme GDI Resources
    HBRUSH m_brush_bg;
    HBRUSH m_brush_pane;
    HBRUSH m_brush_mid;
    HBRUSH m_brush_accent;
    HBRUSH m_brush_online;
    HBRUSH m_brush_offline;

    HPEN m_pen_line;
    HPEN m_pen_mid;
    HPEN m_pen_online_glow;
    HPEN m_pen_null;

    HFONT m_font_ui;
    HFONT m_font_title;
    HFONT m_font_section;
    HFONT m_font_mono;
    HFONT m_font_log;

    // Discord Preview Resources
    bool m_show_preview;
    int64_t m_activity_start_time;
    int64_t m_activity_end_time;
    HFONT m_font_discord_header;
    HFONT m_font_discord_app;
    HFONT m_font_discord_sub;
    HFONT m_font_badge;
    HBRUSH m_brush_discord_card;
    HBRUSH m_brush_discord_banner;
    HBRUSH m_brush_discord_asset;
    HPEN m_pen_discord_line;

    ULONG_PTR m_gdiplus_token;
    Gdiplus::Bitmap* m_avatar_bitmap;
    std::string m_current_avatar_url;
    std::atomic<bool> m_avatar_loading;

    Gdiplus::Bitmap* m_decor_bitmap;
    std::string m_current_decor_url;
    std::atomic<bool> m_decor_loading;

    Gdiplus::Bitmap* m_banner_bitmap;
    IStream* m_banner_stream;
    std::string m_current_banner_url;
    std::atomic<bool> m_banner_loading;
    UINT m_banner_frame_index;
    UINT m_banner_frame_count;

    Gdiplus::Bitmap* m_asset_bitmap;
    IStream* m_asset_stream;
    std::string m_current_asset_url;
    std::atomic<bool> m_asset_loading;
    UINT m_asset_frame_index;
    UINT m_asset_frame_count;

    COLORREF m_dominant_banner_color;
    bool m_has_dominant_banner_color;

    std::string m_last_profile_lookup_id;
    std::atomic<bool> m_profile_lookup_in_progress;

    // Header Navigation Tabs
    HWND m_btn_tab_activity;
    HWND m_btn_tab_debug;

    // Controls - View 1: Activity Dashboard
    std::vector<HWND> m_activity_controls;
    HWND m_label_discord_account;
    HWND m_btn_reconnect;
    HWND m_label_rpc_status;

    HWND m_edit_ical_url;
    HWND m_chk_auto_sync;
    HWND m_btn_sync_now;
    HWND m_label_cal_status;

    std::vector<HWND> m_btn_presets;
    HWND m_btn_clear_activity;

    HWND m_label_preset_name;
    HWND m_edit_preset_name;
    HWND m_label_details;
    HWND m_edit_details;
    HWND m_label_state;
    HWND m_edit_state;
    HWND m_label_large_img;
    HWND m_edit_large_img;
    HWND m_label_small_img;
    HWND m_edit_small_img;
    HWND m_label_from;
    HWND m_edit_time_from;
    HWND m_label_to;
    HWND m_edit_time_to;
    HWND m_btn_apply_custom;
    HWND m_btn_save_preset;
    HWND m_btn_delete_preset;

    // Controls - View 2: Interactive Debug & Settings
    std::vector<HWND> m_debug_controls;
    HWND m_dbg_label_pipe;
    HWND m_dbg_label_user;
    HWND m_dbg_edit_client_id;
    HWND m_dbg_btn_test_ipc;
    HWND m_dbg_btn_reconnect;
    HWND m_dbg_btn_disconnect;
    HWND m_dbg_label_ipc_status;

    HWND m_dbg_edit_ical_url;
    HWND m_dbg_edit_interval;
    HWND m_dbg_chk_auto_sync;
    HWND m_dbg_btn_test_cal;
    HWND m_dbg_btn_force_sync;
    HWND m_dbg_label_cal_status;

    HWND m_dbg_chk_autostart;
    HWND m_dbg_btn_save;

    HWND m_dbg_edit_log;
    HWND m_dbg_btn_clear_log;

    // Modules
    dsc::RpcClnt m_rpc;
    cal::CalClnt m_calendar;
    cfg::AppCfg m_config;

    // State
    NOTIFYICONDATAW m_tray_data;
    bool m_tray_icon_added;
    std::atomic<bool> m_sync_in_progress;
    bool m_rpc_is_online;
    bool m_cal_is_active;
    std::mutex m_state_mutex;
};

using MainWnd = MainWindow;

} // namespace ui
