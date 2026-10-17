#include "calendar_sync.hpp"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

namespace cal {

struct WnHttmpHndl {
    HINTERNET h = nullptr;
    ~WnHttmpHndl() {
        if (h) {
            WinHttpCloseHandle(h);
            h = nullptr;
        }
    }
};

CalClnt::CalClnt() = default;
CalClnt::~CalClnt() = default;

std::string CalClnt::unfld(const std::string& raw) {
    std::string res;
    res.reserve(raw.size());

    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r' && i + 2 < raw.size() && raw[i + 1] == '\n' && (raw[i + 2] == ' ' || raw[i + 2] == '\t')) {
            i += 2;
            continue;
        }
        if (raw[i] == '\n' && i + 1 < raw.size() && (raw[i + 1] == ' ' || raw[i + 1] == '\t')) {
            i += 1;
            continue;
        }
        res.push_back(raw[i]);
    }
    return res;
}

std::time_t CalClnt::prs_ts(const std::string& s_dt) {
    std::string dgts;
    bool utc = (!s_dt.empty() && s_dt.back() == 'Z');

    for (char c : s_dt) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            dgts.push_back(c);
        }
    }

    if (dgts.size() < 8) {
        return 0;
    }

    struct tm tm_inf = {};
    try {
        tm_inf.tm_year = std::stoi(dgts.substr(0, 4)) - 1900;
        tm_inf.tm_mon  = std::stoi(dgts.substr(4, 2)) - 1;
        tm_inf.tm_mday = std::stoi(dgts.substr(6, 2));

        if (dgts.size() >= 14) {
            tm_inf.tm_hour = std::stoi(dgts.substr(8, 2));
            tm_inf.tm_min  = std::stoi(dgts.substr(10, 2));
            tm_inf.tm_sec  = std::stoi(dgts.substr(12, 2));
        } else {
            tm_inf.tm_hour = 0;
            tm_inf.tm_min  = 0;
            tm_inf.tm_sec  = 0;
        }
    } catch (...) {
        return 0;
    }

    if (utc) {
        return _mkgmtime(&tm_inf);
    } else {
        tm_inf.tm_isdst = -1;
        return std::mktime(&tm_inf);
    }
}

bool CalClnt::ftch_cal(const std::string& url, std::string& out_cnt, std::string& out_err) {
    out_cnt.clear();
    out_err.clear();

    if (url.empty()) {
        out_err = "URL d'agenda vide.";
        return false;
    }

    int w_len = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (w_len <= 0) {
        out_err = "Conversion URL invalide.";
        return false;
    }

    std::wstring w_url(w_len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &w_url[0], w_len);

    URL_COMPONENTS cmp = {};
    cmp.dwStructSize = sizeof(cmp);

    wchar_t hst[256] = {};
    wchar_t pth[2048] = {};

    cmp.lpszHostName = hst;
    cmp.dwHostNameLength = ARRAYSIZE(hst);
    cmp.lpszUrlPath = pth;
    cmp.dwUrlPathLength = ARRAYSIZE(pth);

    if (!WinHttpCrackUrl(w_url.c_str(), 0, 0, &cmp)) {
        out_err = "Format d'URL HTTP/HTTPS non valide.";
        return false;
    }

    WnHttmpHndl ssn;
    ssn.h = WinHttpOpen(
        L"At-Work/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!ssn.h) {
        out_err = "Echec initialisation session WinHTTP.";
        return false;
    }

    WnHttmpHndl cn;
    cn.h = WinHttpConnect(ssn.h, hst, cmp.nPort, 0);
    if (!cn.h) {
        out_err = "Echec connexion au serveur distant.";
        return false;
    }

    DWORD flgs = (cmp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    WnHttmpHndl rq;
    rq.h = WinHttpOpenRequest(
        cn.h,
        L"GET",
        pth,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flgs
    );

    if (!rq.h) {
        out_err = "Echec creation requete HTTP.";
        return false;
    }

    DWORD rdr = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(rq.h, WINHTTP_OPTION_REDIRECT_POLICY, &rdr, sizeof(rdr));

    if (!WinHttpSendRequest(rq.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        out_err = "Echec envoi de la requete HTTP.";
        return false;
    }

    if (!WinHttpReceiveResponse(rq.h, nullptr)) {
        out_err = "Echec reception reponse HTTP.";
        return false;
    }

    DWORD sc = 0;
    DWORD sc_sz = sizeof(sc);
    if (WinHttpQueryHeaders(rq.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sc_sz, WINHTTP_NO_HEADER_INDEX)) {
        if (sc != 200) {
            out_err = "Erreur HTTP statut: " + std::to_string(sc);
            return false;
        }
    }

    DWORD b_avlbl = 0;
    char buf[8192];
    while (WinHttpQueryDataAvailable(rq.h, &b_avlbl) && b_avlbl > 0) {
        DWORD b_rd_req = (std::min)(b_avlbl, static_cast<DWORD>(sizeof(buf)));
        DWORD b_rd = 0;
        if (!WinHttpReadData(rq.h, buf, b_rd_req, &b_rd) || b_rd == 0) {
            break;
        }
        out_cnt.append(buf, b_rd);
    }

    return true;
}

std::vector<CalEvnt> CalClnt::prs_evs(const std::string& raw) {
    std::vector<CalEvnt> evs;
    std::string s_ics = unfld(raw);

    std::istringstream ss(s_ics);
    std::string ln;

    bool in_vnt = false;
    CalEvnt crr;

    while (std::getline(ss, ln)) {
        if (!ln.empty() && ln.back() == '\r') {
            ln.pop_back();
        }

        if (ln == "BEGIN:VEVENT") {
            in_vnt = true;
            crr = CalEvnt{};
            continue;
        }

        if (ln == "END:VEVENT") {
            if (in_vnt && crr.end_time > crr.start_time) {
                evs.push_back(crr);
            }
            in_vnt = false;
            continue;
        }

        if (!in_vnt) continue;

        size_t p_cln = ln.find(':');
        if (p_cln == std::string::npos) continue;

        std::string k_prt = ln.substr(0, p_cln);
        std::string v_prt = ln.substr(p_cln + 1);

        size_t p_smc = k_prt.find(';');
        std::string prp = (p_smc != std::string::npos) ? k_prt.substr(0, p_smc) : k_prt;

        if (prp == "SUMMARY") {
            crr.summary = v_prt;
        } else if (prp == "DESCRIPTION") {
            crr.description = v_prt;
        } else if (prp == "DTSTART") {
            crr.start_time = prs_ts(v_prt);
        } else if (prp == "DTEND") {
            crr.end_time = prs_ts(v_prt);
        }
    }

    return evs;
}

std::optional<CalEvnt> CalClnt::get_crr_ev(const std::vector<CalEvnt>& evs, std::time_t ref_tm) {
    if (ref_tm == 0) {
        ref_tm = std::time(nullptr);
    }

    for (const auto& ev : evs) {
        if (ref_tm >= ev.start_time && ref_tm < ev.end_time) {
            return ev;
        }
    }

    return std::nullopt;
}

} // namespace cal
