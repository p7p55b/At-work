#pragma once

#include <windows.h>
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>

namespace dsc {

struct Actvty {
    std::string details;
    std::string state;
    std::string large_image;
    std::string large_text;
    std::string small_image;
    std::string small_text;
    int64_t start_time = 0;
    int64_t end_time = 0;

    const std::string& dtls() const { return details; }
    const std::string& st() const { return state; }
    const std::string& lrg_img() const { return large_image; }
    const std::string& lrg_txt() const { return large_text; }
    const std::string& sml_img() const { return small_image; }
    const std::string& sml_txt() const { return small_text; }
    int64_t tm_s() const { return start_time; }
    int64_t tm_e() const { return end_time; }
};

std::string ext_json_fld(const std::string& j_str, const std::string& fld);

struct UsrPrfl {
    std::string id;
    std::string username;
    std::string global_name;
    std::string discriminator;
    std::string avatar_hash;
    std::string avatar_url;
    std::string avatar_decoration_asset;
    std::string avatar_decoration_url;
    std::string banner_hash;
    std::string banner_url;
    std::string banner_color_hex;
    COLORREF banner_color = RGB(40, 43, 48);
    bool has_banner_color = false;
    std::string app_name;

    const std::string& usr() const { return username; }
    const std::string& glbl() const { return global_name; }
    const std::string& avt() const { return avatar_url; }
    const std::string& bnr() const { return banner_url; }
};

class RpcClnt {
public:
    RpcClnt();
    ~RpcClnt();

    RpcClnt(const RpcClnt&) = delete;
    RpcClnt& operator=(const RpcClnt&) = delete;

    static constexpr const char* DFLT_CID = "1559498097938407464";

    bool cnnct(const std::string& cid = "");
    void dscnct();
    bool is_cnnctd() const;

    std::string get_usr() const;
    UsrPrfl get_prfl() const;
    std::string get_avt_url() const;
    std::string get_bnr_url() const;
    void set_bnr_inf(const std::string& hsh, const std::string& url, COLORREF clr, bool has_clr);
    std::string get_lst_err() const;
    std::string get_pp_pth() const;
    std::string get_cid() const;

    bool set_actvty(const Actvty& a);
    bool clr_actvty();

    bool connect(const std::string& cid = "") { return cnnct(cid); }
    void disconnect() { dscnct(); }
    bool is_connected() const { return is_cnnctd(); }
    std::string get_connected_user() const { return get_usr(); }
    UsrPrfl get_user_profile() const { return get_prfl(); }
    std::string get_avatar_url() const { return get_avt_url(); }
    std::string get_banner_url() const { return get_bnr_url(); }
    void set_banner_info(const std::string& hsh, const std::string& url, COLORREF clr, bool has_clr) { set_bnr_inf(hsh, url, clr, has_clr); }
    std::string get_last_error() const { return get_lst_err(); }
    std::string get_active_pipe_path() const { return get_pp_pth(); }
    std::string get_client_id() const { return get_cid(); }
    bool set_activity(const Actvty& a) { return set_actvty(a); }
    bool clear_activity() { return clr_actvty(); }

private:
    enum Opcd : uint32_t {
        OP_HNDSHK = 0,
        OP_FRM    = 1,
        OP_CLS    = 2,
        OP_PNG    = 3,
        OP_PNG2   = 4
    };

    bool opn_pp();
    bool wrt_frm(Opcd op, const std::string& pyld);
    bool rd_frm(Opcd& out_op, std::string& out_pyld);

    static std::string esc_json(const std::string& s);

    HANDLE m_pp;
    std::string m_pp_pth;
    std::string m_cid;
    std::string m_cnnctd_usr;
    UsrPrfl m_prfl;
    std::atomic<bool> m_is_cnnctd;
    mutable std::recursive_mutex m_mtx;
    std::string m_lst_err;
    uint32_t m_nnc;
};

} // namespace dsc

namespace discord {
    using Activity = dsc::Actvty;
    using Actvty = dsc::Actvty;
    using UserProfile = dsc::UsrPrfl;
    using UsrPrfl = dsc::UsrPrfl;
    using RpcClient = dsc::RpcClnt;
    using RpcClnt = dsc::RpcClnt;
    inline std::string ext_json_fld(const std::string& j_str, const std::string& fld) {
        return dsc::ext_json_fld(j_str, fld);
    }
    inline std::string extract_json_field(const std::string& j_str, const std::string& fld) {
        return dsc::ext_json_fld(j_str, fld);
    }
}
