#include "discord_ipc.hpp"
#include <vector>
#include <sstream>
#include <cstdio>

namespace dsc {

RpcClnt::RpcClnt()
    : m_pp(INVALID_HANDLE_VALUE)
    , m_is_cnnctd(false)
    , m_nnc(1)
{
}

RpcClnt::~RpcClnt() {
    dscnct();
}

std::string RpcClnt::esc_json(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\b': r += "\\b";  break;
            case '\f': r += "\\f";  break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    r += buf;
                } else {
                    r += c;
                }
                break;
        }
    }
    return r;
}

bool RpcClnt::opn_pp() {
    char pp_nm[64];
    for (int i = 0; i < 10; ++i) {
        std::snprintf(pp_nm, sizeof(pp_nm), "\\\\.\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileA(
            pp_nm,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (h != INVALID_HANDLE_VALUE) {
            m_pp = h;
            m_pp_pth = pp_nm;
            return true;
        }
    }
    m_pp_pth.clear();
    return false;
}

bool RpcClnt::wrt_frm(Opcd op, const std::string& pyld) {
    if (m_pp == INVALID_HANDLE_VALUE) return false;

    std::vector<char> frm(sizeof(uint32_t) * 2 + pyld.size());
    uint32_t* hdr = reinterpret_cast<uint32_t*>(frm.data());
    hdr[0] = static_cast<uint32_t>(op);
    hdr[1] = static_cast<uint32_t>(pyld.size());

    if (!pyld.empty()) {
        std::memcpy(frm.data() + sizeof(uint32_t) * 2, pyld.data(), pyld.size());
    }

    DWORD wrt_sz = 0;
    if (!WriteFile(m_pp, frm.data(), static_cast<DWORD>(frm.size()), &wrt_sz, nullptr) ||
        wrt_sz != frm.size()) {
        return false;
    }

    return true;
}

bool RpcClnt::rd_frm(Opcd& out_op, std::string& out_pyld) {
    out_pyld.clear();
    if (m_pp == INVALID_HANDLE_VALUE) return false;

    uint32_t hdr[2] = {0, 0};
    DWORD rd_sz = 0;

    if (!ReadFile(m_pp, hdr, sizeof(hdr), &rd_sz, nullptr) || rd_sz != sizeof(hdr)) {
        return false;
    }

    out_op = static_cast<Opcd>(hdr[0]);
    uint32_t ln = hdr[1];

    if (ln == 0) {
        return true;
    }

    if (ln > 65536) {
        return false;
    }

    std::vector<char> buf(ln);
    DWORD ttl_rd = 0;
    while (ttl_rd < ln) {
        if (!ReadFile(m_pp, buf.data() + ttl_rd, ln - ttl_rd, &rd_sz, nullptr) || rd_sz == 0) {
            return false;
        }
        ttl_rd += rd_sz;
    }

    out_pyld.assign(buf.data(), ln);
    return true;
}

std::string ext_json_fld(const std::string& j_str, const std::string& fld) {
    std::string srch = "\"" + fld + "\"";
    size_t pos = j_str.find(srch);
    if (pos == std::string::npos) return "";
    pos += srch.size();
    while (pos < j_str.size() && (j_str[pos] == ' ' || j_str[pos] == '\t')) pos++;
    if (pos >= j_str.size() || j_str[pos] != ':') return "";
    pos++;
    while (pos < j_str.size() && (j_str[pos] == ' ' || j_str[pos] == '\t')) pos++;
    if (pos >= j_str.size()) return "";

    if (j_str[pos] == '"') {
        pos++;
        size_t p_end = j_str.find('"', pos);
        if (p_end == std::string::npos) return "";
        return j_str.substr(pos, p_end - pos);
    } else if (j_str.compare(pos, 4, "null") == 0) {
        return "";
    } else {
        size_t p_end = pos;
        while (p_end < j_str.size() && j_str[p_end] != ',' && j_str[p_end] != '}' && j_str[p_end] != ']' &&
               j_str[p_end] != ' ' && j_str[p_end] != '\r' && j_str[p_end] != '\n') {
            p_end++;
        }
        return j_str.substr(pos, p_end - pos);
    }
}

bool RpcClnt::cnnct(const std::string& cid) {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);

    if (m_is_cnnctd) {
        dscnct();
    }

    m_cid = cid.empty() ? DFLT_CID : cid;

    if (!opn_pp()) {
        m_lst_err = "Impossible d'ouvrir le pipe Discord (Discord Desktop est-il lance ?)";
        return false;
    }

    std::ostringstream ss;
    ss << "{\"v\":1,\"client_id\":\"" << esc_json(m_cid) << "\"}";
    std::string hndshk = ss.str();

    if (!wrt_frm(OP_HNDSHK, hndshk)) {
        m_lst_err = "Echec de l'envoi du handshake au pipe.";
        CloseHandle(m_pp);
        m_pp = INVALID_HANDLE_VALUE;
        m_pp_pth.clear();
        return false;
    }

    Opcd r_op;
    std::string r_dt;
    if (!rd_frm(r_op, r_dt)) {
        m_lst_err = "Echec de reception de la confirmation du handshake.";
        CloseHandle(m_pp);
        m_pp = INVALID_HANDLE_VALUE;
        m_pp_pth.clear();
        return false;
    }

    m_prfl = {};
    m_prfl.id = ext_json_fld(r_dt, "id");
    m_prfl.username = ext_json_fld(r_dt, "username");
    m_prfl.global_name = ext_json_fld(r_dt, "global_name");
    m_prfl.discriminator = ext_json_fld(r_dt, "discriminator");
    m_prfl.avatar_hash = ext_json_fld(r_dt, "avatar");
    m_prfl.banner_color_hex = ext_json_fld(r_dt, "banner_color");

    if (!m_prfl.banner_color_hex.empty() && m_prfl.banner_color_hex[0] == '#') {
        try {
            unsigned long v = std::stoul(m_prfl.banner_color_hex.substr(1), nullptr, 16);
            m_prfl.banner_color = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
            m_prfl.has_banner_color = true;
        } catch (...) {}
    }

    std::string s_acc = ext_json_fld(r_dt, "accent_color");
    if (!m_prfl.has_banner_color && !s_acc.empty()) {
        try {
            unsigned long v = std::stoul(s_acc);
            m_prfl.banner_color = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
            m_prfl.has_banner_color = true;
        } catch (...) {}
    }

    size_t p_app = r_dt.find("\"application\":");
    if (p_app != std::string::npos) {
        std::string j_app = r_dt.substr(p_app);
        m_prfl.app_name = ext_json_fld(j_app, "name");
    }

    if (!m_prfl.avatar_hash.empty() && m_prfl.avatar_hash != "null") {
        m_prfl.avatar_url = "https://cdn.discordapp.com/avatars/" + m_prfl.id + "/" + m_prfl.avatar_hash + ".png?size=128";
    } else if (!m_prfl.id.empty()) {
        try {
            uint64_t uid = std::stoull(m_prfl.id);
            int idx = (!m_prfl.discriminator.empty() && m_prfl.discriminator != "0")
                      ? (std::stoi(m_prfl.discriminator) % 5)
                      : static_cast<int>((uid >> 22) % 6);
            m_prfl.avatar_url = "https://cdn.discordapp.com/embed/avatars/" + std::to_string(idx) + ".png";
        } catch (...) {
            m_prfl.avatar_url = "https://cdn.discordapp.com/embed/avatars/0.png";
        }
    }

    size_t p_dcr = r_dt.find("\"avatar_decoration_data\":");
    if (p_dcr != std::string::npos) {
        std::string j_dcr = r_dt.substr(p_dcr);
        m_prfl.avatar_decoration_asset = ext_json_fld(j_dcr, "asset");
        if (!m_prfl.avatar_decoration_asset.empty() && m_prfl.avatar_decoration_asset != "null") {
            m_prfl.avatar_decoration_url = "https://cdn.discordapp.com/avatar-decoration-presets/" + m_prfl.avatar_decoration_asset + ".png?size=96";
        }
    }

    m_prfl.banner_hash = ext_json_fld(r_dt, "banner");
    if (!m_prfl.banner_hash.empty() && m_prfl.banner_hash != "null") {
        std::string ext = (m_prfl.banner_hash.rfind("a_", 0) == 0) ? ".gif" : ".png";
        m_prfl.banner_url = "https://cdn.discordapp.com/banners/" + m_prfl.id + "/" + m_prfl.banner_hash + ext + "?size=480";
    }

    if (!m_prfl.global_name.empty() && !m_prfl.username.empty() && m_prfl.global_name != m_prfl.username) {
        m_cnnctd_usr = m_prfl.global_name + " (@" + m_prfl.username + ")";
    } else if (!m_prfl.username.empty()) {
        m_cnnctd_usr = "@" + m_prfl.username;
    } else {
        m_cnnctd_usr = "Discord Desktop";
    }

    m_is_cnnctd = true;
    m_lst_err.clear();
    return true;
}

std::string RpcClnt::get_usr() const {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    return m_cnnctd_usr;
}

UsrPrfl RpcClnt::get_prfl() const {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    return m_prfl;
}

std::string RpcClnt::get_avt_url() const {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    return m_prfl.avatar_url;
}

std::string RpcClnt::get_bnr_url() const {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    return m_prfl.banner_url;
}

void RpcClnt::set_bnr_inf(const std::string& hsh, const std::string& url, COLORREF clr, bool has_clr) {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    if (!hsh.empty()) m_prfl.banner_hash = hsh;
    if (!url.empty()) m_prfl.banner_url = url;
    if (has_clr) {
        m_prfl.banner_color = clr;
        m_prfl.has_banner_color = true;
    }
}

std::string RpcClnt::get_pp_pth() const {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    return m_pp_pth;
}

std::string RpcClnt::get_cid() const {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    return m_cid;
}

void RpcClnt::dscnct() {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    m_cnnctd_usr.clear();
    m_prfl = {};
    m_pp_pth.clear();
    if (m_pp != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pp);
        m_pp = INVALID_HANDLE_VALUE;
    }
    m_is_cnnctd = false;
}

bool RpcClnt::is_cnnctd() const {
    return m_is_cnnctd;
}

bool RpcClnt::set_actvty(const Actvty& a) {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    if (!m_is_cnnctd || m_pp == INVALID_HANDLE_VALUE) {
        m_lst_err = "Non connecte a Discord.";
        return false;
    }

    DWORD pid = GetCurrentProcessId();
    uint32_t nnc = m_nnc++;

    std::ostringstream ss;
    ss << "{"
       << "\"cmd\":\"SET_ACTIVITY\","
       << "\"args\":{"
       << "\"pid\":" << pid << ","
       << "\"activity\":{";

    bool has_fld = false;

    if (!a.details.empty()) {
        ss << "\"details\":\"" << esc_json(a.details) << "\"";
        has_fld = true;
    }

    if (!a.state.empty()) {
        if (has_fld) ss << ",";
        ss << "\"state\":\"" << esc_json(a.state) << "\"";
        has_fld = true;
    }

    if (!a.large_image.empty() || !a.small_image.empty()) {
        if (has_fld) ss << ",";
        ss << "\"assets\":{";
        bool has_asst = false;
        if (!a.large_image.empty()) {
            ss << "\"large_image\":\"" << esc_json(a.large_image) << "\"";
            if (!a.large_text.empty()) {
                ss << ",\"large_text\":\"" << esc_json(a.large_text) << "\"";
            }
            has_asst = true;
        }
        if (!a.small_image.empty()) {
            if (has_asst) ss << ",";
            ss << "\"small_image\":\"" << esc_json(a.small_image) << "\"";
            if (!a.small_text.empty()) {
                ss << ",\"small_text\":\"" << esc_json(a.small_text) << "\"";
            }
        }
        ss << "}";
        has_fld = true;
    }

    if (a.start_time > 0 || a.end_time > 0) {
        if (has_fld) ss << ",";
        ss << "\"timestamps\":{";
        bool has_tm = false;
        if (a.start_time > 0) {
            ss << "\"start\":" << a.start_time;
            has_tm = true;
        }
        if (a.end_time > 0) {
            if (has_tm) ss << ",";
            ss << "\"end\":" << a.end_time;
        }
        ss << "}";
    }

    ss << "}},\"nonce\":\"" << nnc << "\"}";

    std::string pyld = ss.str();
    if (!wrt_frm(OP_FRM, pyld)) {
        m_lst_err = "Echec de l'envoi de la trame d'activite.";
        m_is_cnnctd = false;
        CloseHandle(m_pp);
        m_pp = INVALID_HANDLE_VALUE;
        return false;
    }

    return true;
}

bool RpcClnt::clr_actvty() {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    if (!m_is_cnnctd || m_pp == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD pid = GetCurrentProcessId();
    uint32_t nnc = m_nnc++;

    std::ostringstream ss;
    ss << "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" << pid << ",\"activity\":null},\"nonce\":\"" << nnc << "\"}";

    std::string pyld = ss.str();
    if (!wrt_frm(OP_FRM, pyld)) {
        m_lst_err = "Echec de l'envoi de la requete de reinitialisation.";
        m_is_cnnctd = false;
        CloseHandle(m_pp);
        m_pp = INVALID_HANDLE_VALUE;
        return false;
    }

    return true;
}

std::string RpcClnt::get_lst_err() const {
    std::lock_guard<std::recursive_mutex> lck(m_mtx);
    return m_lst_err;
}

} // namespace dsc
