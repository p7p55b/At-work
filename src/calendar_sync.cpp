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

static std::string b64_dec(const std::string& in) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,62,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c >= 128 || T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string url_enc(const std::string& s) {
    std::string r;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            r.push_back(static_cast<char>(c));
        } else {
            r.push_back('%');
            r.push_back(hex[(c >> 4) & 0xF]);
            r.push_back(hex[c & 0xF]);
        }
    }
    return r;
}

bool CalClnt::ftch_cal(const std::string& raw_url, std::string& out_cnt, std::string& out_err) {
    out_cnt.clear();
    out_err.clear();

    if (raw_url.empty()) {
        out_err = "URL d'agenda vide.";
        return false;
    }

    std::string url = raw_url;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        url = "https://" + url;
    }

    // Conversion automatique si l'utilisateur a colle un lien ?cid= ou ?src= au lieu d'un .ics
    size_t cid_pos = url.find("cid=");
    if (cid_pos != std::string::npos && url.find(".ics") == std::string::npos) {
        std::string cid = url.substr(cid_pos + 4);
        size_t amp = cid.find('&');
        if (amp != std::string::npos) cid = cid.substr(0, amp);
        std::string dec = b64_dec(cid);
        if (!dec.empty() && dec.find('@') != std::string::npos) {
            url = "https://calendar.google.com/calendar/ical/" + url_enc(dec) + "/public/basic.ics";
        }
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
        if (sc == 404 && raw_url.find("cid=") != std::string::npos) {
            out_err = "Agenda prive : collez 'Adresse secrete au format iCal' (.ics) des parametres Google Calendar";
            return false;
        }
        if (sc != 200) {
            out_err = "Erreur HTTP statut: " + std::to_string(sc) + " (verifiez l'Adresse secrete au format iCal .ics)";
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

    if (out_cnt.find("BEGIN:VCALENDAR") == std::string::npos) {
        out_err = "Lien non-iCal : collez 'Adresse secrete au format iCal' (.ics) des parametres Google Calendar";
        return false;
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
    std::string rrule;
    std::time_t now = std::time(nullptr);

    while (std::getline(ss, ln)) {
        if (!ln.empty() && ln.back() == '\r') {
            ln.pop_back();
        }

        if (ln == "BEGIN:VEVENT") {
            in_vnt = true;
            crr = CalEvnt{};
            rrule.clear();
            continue;
        }

        if (ln == "END:VEVENT") {
            if (in_vnt && crr.end_time > crr.start_time) {
                if (crr.summary.empty()) {
                    crr.summary = "(Sans titre)";
                }
                evs.push_back(crr);

                // Expansion basique des evenements recurrents (FREQ=DAILY / FREQ=WEEKLY)
                if (!rrule.empty()) {
                    int step_days = 0;
                    if (rrule.find("FREQ=DAILY") != std::string::npos) step_days = 1;
                    else if (rrule.find("FREQ=WEEKLY") != std::string::npos) step_days = 7;

                    if (step_days > 0) {
                        std::time_t until_ts = now + 14 * 24 * 3600;
                        size_t u_pos = rrule.find("UNTIL=");
                        if (u_pos != std::string::npos) {
                            std::string u_str = rrule.substr(u_pos + 6);
                            size_t sc_pos = u_str.find(';');
                            if (sc_pos != std::string::npos) u_str = u_str.substr(0, sc_pos);
                            std::time_t parsed_u = prs_ts(u_str);
                            if (parsed_u > 0 && parsed_u < until_ts) until_ts = parsed_u;
                        }
                        std::time_t dur = crr.end_time - crr.start_time;
                        std::tm tm_iter = {};
                        localtime_s(&tm_iter, &crr.start_time);
                        for (int k = 1; k <= 520; ++k) {
                            tm_iter.tm_mday += step_days;
                            tm_iter.tm_isdst = -1;
                            std::time_t occ_s = std::mktime(&tm_iter);
                            if (occ_s <= 0 || occ_s > until_ts) break;
                            std::time_t occ_e = occ_s + dur;
                            if (occ_e >= now - 24 * 3600) {
                                CalEvnt occ = crr;
                                occ.start_time = occ_s;
                                occ.end_time = occ_e;
                                evs.push_back(occ);
                            }
                        }
                    }
                }
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
        } else if (prp == "RRULE") {
            rrule = v_prt;
        }
    }

    std::sort(evs.begin(), evs.end(), [](const CalEvnt& a, const CalEvnt& b) {
        return a.start_time < b.start_time;
    });

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

std::vector<CalEvnt> CalClnt::get_upcm_evs(const std::vector<CalEvnt>& evs, size_t max_cnt, std::time_t ref_tm) {
    if (ref_tm == 0) {
        ref_tm = std::time(nullptr);
    }

    std::vector<CalEvnt> out;
    for (const auto& ev : evs) {
        if (ev.start_time > ref_tm) {
            out.push_back(ev);
        }
    }

    std::sort(out.begin(), out.end(), [](const CalEvnt& a, const CalEvnt& b) {
        return a.start_time < b.start_time;
    });

    if (out.size() > max_cnt) {
        out.resize(max_cnt);
    }
    return out;
}

} // namespace cal
