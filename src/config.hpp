#pragma once

#include <string>
#include <vector>

namespace cfg {

struct Prst {
    std::string id;
    std::string label;
    std::string details;
    std::string state;
    std::string time_from;
    std::string time_to;
    std::string time_range;
    std::string large_image;
    std::string large_text;
    std::string small_image;
    std::string small_text;

    const std::string& lbl() const { return label; }
    const std::string& dtls() const { return details; }
    const std::string& st() const { return state; }
    const std::string& tm_frm() const { return time_from; }
    const std::string& tm_to() const { return time_to; }
    const std::string& tm_rng() const { return time_range; }
    const std::string& lrg_img() const { return large_image; }
    const std::string& lrg_txt() const { return large_text; }
    const std::string& sml_img() const { return small_image; }
    const std::string& sml_txt() const { return small_text; }
};

struct AppCfg {
    bool setup_completed = false;
    bool autostart = false;
    std::string client_id;
    std::string ical_url;
    bool auto_sync = true;
    int sync_interval_seconds = 60;

    std::string custom_details;
    std::string custom_state;
    std::string custom_time_from;
    std::string custom_time_to;
    std::string custom_time_range;
    std::string custom_large_image;
    std::string custom_small_image;

    std::vector<Prst> presets;

    bool stp_cmplt() const { return setup_completed; }
    bool tstrt() const { return autostart; }
    const std::string& cid() const { return client_id; }
    bool t_sync() const { return auto_sync; }
    int sync_sec() const { return sync_interval_seconds; }
};

class CfgMgr {
public:
    static std::wstring get_pth();
    static AppCfg ld();
    static bool sv(const AppCfg& c);
    static AppCfg crt_dflt();

    static bool chk_tstrt();
    static bool set_tstrt(bool en);

    static std::wstring get_config_path() { return get_pth(); }
    static AppCfg load() { return ld(); }
    static bool save(const AppCfg& c) { return sv(c); }
    static AppCfg create_default() { return crt_dflt(); }
    static bool is_autostart_enabled() { return chk_tstrt(); }
    static bool set_autostart(bool en) { return set_tstrt(en); }
};

} // namespace cfg

namespace config {
    using Preset = cfg::Prst;
    using AppConfig = cfg::AppCfg;
    using ConfigManager = cfg::CfgMgr;
}
