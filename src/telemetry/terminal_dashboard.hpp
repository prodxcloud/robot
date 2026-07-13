#pragma once
/// @file terminal_dashboard.hpp
/// @brief nvtop-style terminal dashboard for VA AgentControl.
///        Shows CPU, memory, agent stats, and service health in a live TUI.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace prodxcloud::telemetry {

// ── ANSI helpers ─────────────────────────────────────────────────────────────
namespace ansi {
    inline const char* RESET   = "\033[0m";
    inline const char* BOLD    = "\033[1m";
    inline const char* DIM     = "\033[2m";
    inline const char* CYAN    = "\033[36m";
    inline const char* GREEN   = "\033[32m";
    inline const char* YELLOW  = "\033[33m";
    inline const char* RED     = "\033[31m";
    inline const char* MAGENTA = "\033[35m";
    inline const char* BLUE    = "\033[34m";
    inline const char* WHITE   = "\033[97m";
    inline const char* GRAY    = "\033[90m";
    inline const char* BG_DARK = "\033[40m";
    inline const char* CLEAR   = "\033[2J\033[H";
    inline const char* HIDE_CURSOR = "\033[?25l";
    inline const char* SHOW_CURSOR = "\033[?25h";
    inline std::string move(int row, int col) {
        return "\033[" + std::to_string(row) + ";" + std::to_string(col) + "H";
    }
}

// ── System stats ─────────────────────────────────────────────────────────────
struct SystemStats {
    double cpu_percent   = 0.0;
    double mem_used_gb   = 0.0;
    double mem_total_gb  = 0.0;
    double mem_percent   = 0.0;
    double swap_used_gb  = 0.0;
    double swap_total_gb = 0.0;
    int    cpu_cores     = 1;
    std::vector<double> per_core;  // per-core % (up to 8 shown)
};

struct ServiceStats {
    std::string version      = "1.0.0";
    std::string host         = "0.0.0.0";
    int         port         = 8788;
    std::string slm_url;
    int64_t     requests     = 0;
    int64_t     agents_active = 0;
    int64_t     uptime_secs  = 0;
    bool        slm_reachable = false;
    std::string status       = "running";
};

// ── Bar renderer ─────────────────────────────────────────────────────────────
inline std::string bar(double pct, int width = 30) {
    // Color gradient: green → yellow → red
    const char* color = ansi::GREEN;
    if (pct > 80.0) color = ansi::RED;
    else if (pct > 60.0) color = ansi::YELLOW;

    int filled = static_cast<int>(pct / 100.0 * width);
    filled = std::max(0, std::min(filled, width));

    std::string out;
    out += color;
    for (int i = 0; i < filled; ++i) out += "\u2588";  // █
    out += ansi::GRAY;
    for (int i = filled; i < width; ++i) out += "\u00B7";  // ·
    out += ansi::RESET;
    return out;
}

inline std::string pct_str(double p) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << p << "%";
    return ss.str();
}

inline std::string gb_str(double gb) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << gb << "G";
    return ss.str();
}

inline std::string uptime_str(int64_t secs) {
    int64_t d = secs / 86400, h = (secs % 86400) / 3600,
            m = (secs % 3600) / 60, s = secs % 60;
    std::ostringstream ss;
    if (d > 0) ss << d << "d ";
    ss << std::setw(2) << std::setfill('0') << h << "h "
       << std::setw(2) << std::setfill('0') << m << "m "
       << std::setw(2) << std::setfill('0') << s << "s";
    return ss.str();
}

// ── System stat reader (Linux) ────────────────────────────────────────────────
inline SystemStats read_system_stats() {
    SystemStats s;
#ifdef __linux__
    // CPU cores
    s.cpu_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));

    // Memory via sysinfo
    struct sysinfo si{};
    if (sysinfo(&si) == 0) {
        double unit = si.mem_unit;
        s.mem_total_gb  = (si.totalram  * unit) / 1e9;
        s.mem_used_gb   = ((si.totalram - si.freeram - si.bufferram) * unit) / 1e9;
        s.mem_percent   = s.mem_used_gb / s.mem_total_gb * 100.0;
        s.swap_total_gb = (si.totalswap * unit) / 1e9;
        s.swap_used_gb  = ((si.totalswap - si.freeswap) * unit) / 1e9;
    }

    // CPU % via /proc/stat (two-sample delta)
    static long prev_idle = 0, prev_total = 0;
    static std::vector<long> prev_core_idle, prev_core_total;

    auto parse_stat = [](const std::string& line, long& idle, long& total) {
        long u, n, s, i, iow, irq, sirq, steal;
        if (sscanf(line.c_str(), "%*s %ld %ld %ld %ld %ld %ld %ld %ld",
                   &u, &n, &s, &i, &iow, &irq, &sirq, &steal) == 8) {
            idle  = i + iow;
            total = u + n + s + i + iow + irq + sirq + steal;
        }
    };

    std::ifstream stat("/proc/stat");
    std::string line;
    int core_idx = 0;
    std::vector<long> cur_core_idle, cur_core_total;
    long cur_idle = 0, cur_total = 0;

    while (std::getline(stat, line)) {
        if (line.rfind("cpu ", 0) == 0) {
            parse_stat(line, cur_idle, cur_total);
        } else if (line.rfind("cpu", 0) == 0 && line[3] != ' ') {
            long ci = 0, ct = 0;
            parse_stat(line, ci, ct);
            cur_core_idle.push_back(ci);
            cur_core_total.push_back(ct);
            ++core_idx;
        }
    }

    if (prev_total > 0) {
        long d_idle  = cur_idle  - prev_idle;
        long d_total = cur_total - prev_total;
        s.cpu_percent = d_total > 0 ? (1.0 - static_cast<double>(d_idle) / d_total) * 100.0 : 0.0;

        int n = std::min((int)cur_core_idle.size(), 8);
        s.per_core.resize(n);
        for (int i = 0; i < n; ++i) {
            if (i < (int)prev_core_idle.size()) {
                long di = cur_core_idle[i]  - prev_core_idle[i];
                long dt = cur_core_total[i] - prev_core_total[i];
                s.per_core[i] = dt > 0 ? (1.0 - static_cast<double>(di) / dt) * 100.0 : 0.0;
            }
        }
    }
    prev_idle  = cur_idle;
    prev_total = cur_total;
    prev_core_idle  = cur_core_idle;
    prev_core_total = cur_core_total;
#else
    // Non-Linux stub
    s.cpu_cores    = 4;
    s.mem_total_gb = 8.0;
    s.mem_used_gb  = 2.0;
    s.mem_percent  = 25.0;
#endif
    return s;
}

// ── Dashboard renderer ────────────────────────────────────────────────────────
class TerminalDashboard {
public:
    explicit TerminalDashboard(ServiceStats initial_stats)
        : stats_(std::move(initial_stats)),
          start_time_(std::chrono::steady_clock::now()) {}

    ~TerminalDashboard() { stop(); }

    void update_service_stats(const ServiceStats& s) {
        std::lock_guard lock(mu_);
        stats_ = s;
    }

    void set_requests(int64_t n)      { std::lock_guard lock(mu_); stats_.requests = n; }
    void set_agents(int64_t n)        { std::lock_guard lock(mu_); stats_.agents_active = n; }
    void set_slm_reachable(bool ok)   { std::lock_guard lock(mu_); stats_.slm_reachable = ok; }

    void start(double refresh_secs = 1.0) {
        running_ = true;
        printf("%s%s", ansi::CLEAR, ansi::HIDE_CURSOR);
        fflush(stdout);
        thread_ = std::thread([this, refresh_secs]() {
            while (running_) {
                render();
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>(refresh_secs * 1000)));
            }
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        printf("%s", ansi::SHOW_CURSOR);
        fflush(stdout);
    }

    /// Print a one-shot startup banner (no live refresh).
    static void print_banner(const ServiceStats& s) {
        const int W = 72;
        auto line = [&](const std::string& l = "") {
            printf("  %s│%s %-*s %s│%s\n",
                   ansi::CYAN, ansi::RESET, W - 4, l.c_str(), ansi::CYAN, ansi::RESET);
        };
        auto sep = [&]() {
            printf("  %s├", ansi::CYAN);
            for (int i = 0; i < W - 2; ++i) printf("─");
            printf("┤%s\n", ansi::RESET);
        };

        // Top border
        printf("\n  %s╔", ansi::CYAN);
        for (int i = 0; i < W - 2; ++i) printf("═");
        printf("╗%s\n", ansi::RESET);

        // Title
        std::string title = "  VA AgentControl  ·  v" + s.version;
        int pad = (W - 4 - (int)title.size()) / 2;
        std::string centered(pad, ' ');
        centered += ansi::BOLD; centered += ansi::WHITE; centered += title;
        centered += ansi::RESET;
        line(centered);

        std::string sub = "  Agent Orchestration Platform  ·  No local model execution";
        int pad2 = (W - 4 - (int)sub.size()) / 2;
        std::string centered2(std::max(0, pad2), ' ');
        centered2 += ansi::GRAY; centered2 += sub; centered2 += ansi::RESET;
        line(centered2);

        sep();

        // Config
        auto kv = [&](const std::string& k, const std::string& v, const char* vc = ansi::GREEN) {
            std::string row = "  ";
            row += ansi::CYAN; row += k; row += ansi::RESET;
            row += std::string(18 - k.size(), ' ');
            row += vc; row += v; row += ansi::RESET;
            line(row);
        };

        kv("REST API",    "http://" + s.host + ":" + std::to_string(s.port));
        kv("gRPC",        s.host + ":50051");
        kv("SLM-Models",  s.slm_url.empty() ? "(not configured)" : s.slm_url,
           s.slm_url.empty() ? ansi::YELLOW : ansi::GREEN);
        kv("Metrics",     "/metrics  (Prometheus)");
        kv("Health",      "/health   /ready");

        sep();
        line(std::string("  ") + ansi::GRAY +
             "q:quit  s:sort  +/-:speed  Ctrl+C to stop" + ansi::RESET);

        // Bottom border
        printf("  %s╚", ansi::CYAN);
        for (int i = 0; i < W - 2; ++i) printf("═");
        printf("╝%s\n\n", ansi::RESET);
        fflush(stdout);
    }

private:
    void render() {
        auto sys = read_system_stats();
        ServiceStats svc;
        {
            std::lock_guard lock(mu_);
            svc = stats_;
            svc.uptime_secs = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time_).count());
        }

        std::ostringstream out;
        out << ansi::CLEAR;

        const int W = 76;
        auto hline = [&](char l, char m, char r) {
            out << "  " << ansi::CYAN << (char)l;
            for (int i = 0; i < W - 2; ++i) out << "\xe2\x94\x80";  // ─
            out << (char)r << ansi::RESET << "\n";
        };

        // ── Header ──────────────────────────────────────────────────────────
        out << "  " << ansi::BOLD << ansi::CYAN
            << "va-agentcontrol" << ansi::RESET
            << "  " << ansi::WHITE << "VA AgentControl Platform" << ansi::RESET
            << "    "
            << ansi::GRAY << "up " << uptime_str(svc.uptime_secs) << ansi::RESET
            << "    "
            << ansi::GRAY << "req " << ansi::GREEN << svc.requests << ansi::RESET
            << "\n\n";

        // ── CPU ─────────────────────────────────────────────────────────────
        out << "  " << ansi::BOLD << ansi::YELLOW << "CPU" << ansi::RESET
            << "  " << sys.cpu_cores << " cores"
            << "    Overall: " << bar(sys.cpu_percent, 32)
            << "  " << ansi::WHITE << std::fixed << std::setprecision(1)
            << sys.cpu_percent << "%" << ansi::RESET << "\n";

        // Per-core bars (up to 8, two columns)
        int n = std::min((int)sys.per_core.size(), 8);
        int half = (n + 1) / 2;
        for (int i = 0; i < half; ++i) {
            // left core
            out << "  " << ansi::GRAY << std::setw(2) << i << ansi::RESET << "  "
                << bar(sys.per_core[i], 20)
                << "  " << ansi::WHITE << std::setw(5) << pct_str(sys.per_core[i]) << ansi::RESET;
            // right core
            int j = i + half;
            if (j < n) {
                out << "    " << ansi::GRAY << std::setw(2) << j << ansi::RESET << "  "
                    << bar(sys.per_core[j], 20)
                    << "  " << ansi::WHITE << std::setw(5) << pct_str(sys.per_core[j]) << ansi::RESET;
            }
            out << "\n";
        }
        out << "\n";

        // ── MEM ─────────────────────────────────────────────────────────────
        out << "  " << ansi::BOLD << ansi::BLUE << "MEM" << ansi::RESET
            << "  " << ansi::GREEN << gb_str(sys.mem_used_gb) << " used" << ansi::RESET
            << " / " << ansi::GRAY << gb_str(sys.mem_total_gb) << ansi::RESET << "\n"
            << "  " << bar(sys.mem_percent, 50)
            << "  " << ansi::WHITE << pct_str(sys.mem_percent) << ansi::RESET << "\n";

        if (sys.swap_total_gb > 0.1) {
            double swap_pct = sys.swap_used_gb / sys.swap_total_gb * 100.0;
            out << "  " << ansi::BOLD << ansi::BLUE << "SWP" << ansi::RESET
                << "  " << gb_str(sys.swap_used_gb) << " / " << gb_str(sys.swap_total_gb) << "\n"
                << "  " << bar(swap_pct, 50)
                << "  " << ansi::WHITE << pct_str(swap_pct) << ansi::RESET << "\n";
        }
        out << "\n";

        // ── Separator ───────────────────────────────────────────────────────
        out << "  " << ansi::GRAY;
        for (int i = 0; i < W - 2; ++i) out << "─";
        out << ansi::RESET << "\n\n";

        // ── Service ─────────────────────────────────────────────────────────
        out << "  " << ansi::BOLD << ansi::MAGENTA << "SERVICE" << ansi::RESET
            << "  " << ansi::WHITE << "VA AgentControl" << ansi::RESET
            << "  " << ansi::GRAY << "v" << svc.version << ansi::RESET
            << "  port:" << ansi::GREEN << svc.port << ansi::RESET
            << "\n\n";

        // Stats table header
        out << "  " << ansi::BOLD << ansi::GRAY
            << std::left << std::setw(20) << "COMPONENT"
            << std::setw(20) << "STATUS"
            << std::setw(20) << "VALUE"
            << "INFO" << ansi::RESET << "\n";
        out << "  " << ansi::GRAY;
        for (int i = 0; i < W - 2; ++i) out << "·";
        out << ansi::RESET << "\n";

        auto row = [&](const std::string& comp, bool ok,
                       const std::string& val, const std::string& info) {
            out << "  " << ansi::CYAN << std::left << std::setw(20) << comp << ansi::RESET
                << (ok ? ansi::GREEN : ansi::YELLOW)
                << std::setw(20) << (ok ? "● running" : "○ pending")
                << ansi::RESET
                << ansi::WHITE << std::setw(20) << val << ansi::RESET
                << ansi::GRAY << info << ansi::RESET << "\n";
        };

        row("REST API",    true,  ":" + std::to_string(svc.port),  "http://" + svc.host);
        row("gRPC",        true,  ":50051",                         svc.host);
        row("Agent Ctrl",  true,  std::to_string(svc.agents_active) + " active", "multi-tenant");
        row("Vector Store",true,  "ready",                          "embeddings / RAG");
        row("SLM-Models",  svc.slm_reachable,
            svc.slm_reachable ? "reachable" : "not reached",
            svc.slm_url.empty() ? "SLM_SERVICE_URL not set" : svc.slm_url);
        row("Metrics",     true,  "/metrics",                       "Prometheus");

        out << "\n";

        // ── Request history sparkline ────────────────────────────────────────
        out << "  " << ansi::BOLD << ansi::CYAN << "Request history" << ansi::RESET << "\n";
        {
            std::lock_guard lock(mu_);
            req_history_.push_back(svc.requests);
            if (req_history_.size() > 40) req_history_.erase(req_history_.begin());
        }
        out << "  " << ansi::GRAY << "100%" << ansi::RESET << "\n  ";
        {
            std::lock_guard lock(mu_);
            int64_t mx = 1;
            for (auto v : req_history_) mx = std::max(mx, v);
            const char* bars[] = {" ", "\u2581", "\u2582", "\u2583", "\u2584",
                                   "\u2585", "\u2586", "\u2587", "\u2588"};
            for (auto v : req_history_) {
                int h = static_cast<int>(v * 8 / mx);
                h = std::max(0, std::min(h, 8));
                out << ansi::CYAN << bars[h] << ansi::RESET;
            }
        }
        out << "\n  " << ansi::GRAY << "  0%" << ansi::RESET << "\n\n";

        // ── Footer ──────────────────────────────────────────────────────────
        out << "  " << ansi::GRAY;
        for (int i = 0; i < W - 2; ++i) out << "─";
        out << ansi::RESET << "\n";
        out << "  " << ansi::GRAY
            << "q:quit  s:sort  +/-:speed  1.0s"
            << std::string(W - 40, ' ')
            << "v" << svc.version
            << ansi::RESET << "\n";

        printf("%s", out.str().c_str());
        fflush(stdout);
    }

    ServiceStats stats_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mu_;
    std::vector<int64_t> req_history_;
};

}  // namespace prodxcloud::telemetry
