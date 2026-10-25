#pragma once

#include <string>
#include <ctime>
#include <optional>
#include <vector>

namespace cal {

struct CalEvnt {
    std::string summary;
    std::string description;
    std::time_t start_time = 0;
    std::time_t end_time = 0;

    const std::string& smm() const { return summary; }
    const std::string& dsc() const { return description; }
    std::time_t tm_s() const { return start_time; }
    std::time_t tm_e() const { return end_time; }
};

class CalClnt {
public:
    CalClnt();
    ~CalClnt();

    bool ftch_cal(const std::string& url, std::string& out_cnt, std::string& out_err);
    std::vector<CalEvnt> prs_evs(const std::string& raw);
    std::optional<CalEvnt> get_crr_ev(const std::vector<CalEvnt>& evs, std::time_t ref_tm = 0);
    std::vector<CalEvnt> get_upcm_evs(const std::vector<CalEvnt>& evs, size_t max_cnt = 5, std::time_t ref_tm = 0);

    bool fetch_ical(const std::string& url, std::string& out_cnt, std::string& out_err) { return ftch_cal(url, out_cnt, out_err); }
    std::vector<CalEvnt> parse_events(const std::string& raw) { return prs_evs(raw); }
    std::optional<CalEvnt> get_current_event(const std::vector<CalEvnt>& evs, std::time_t ref_tm = 0) { return get_crr_ev(evs, ref_tm); }
    std::vector<CalEvnt> get_upcoming_events(const std::vector<CalEvnt>& evs, size_t max_cnt = 5, std::time_t ref_tm = 0) { return get_upcm_evs(evs, max_cnt, ref_tm); }

private:
    static std::time_t prs_ts(const std::string& s_dt);
    static std::string unfld(const std::string& raw);
};

} // namespace cal

namespace calendar {
    using CalendarEvent = cal::CalEvnt;
    using CalendarClient = cal::CalClnt;
}
