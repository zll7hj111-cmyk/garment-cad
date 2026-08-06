#pragma once

// ---------------------------------------------------------------------------
// Frame performance probe (帧性能探针).
//
// Enable at RUNTIME by setting the environment variable GCAD_PROFILE=1 before
// launching the app (PowerShell: $env:GCAD_PROFILE=1). When disabled (the
// default) every instrumentation point costs a single relaxed bool load —
// effectively zero overhead.
//
// When enabled, timings accumulate into named buckets and a summary line is
// printed to stderr every 120 logical frames (one frame = one resolveAll()).
// Example output while dragging on the working layer:
//
//   [gcad-perf] 120f | resolve 310us(120) | resolve.aux 0us(0)
//     | resolve.work 262us(120) | meas 41us(240) | snap 88us(120)
//     | sync 39us(120) | paint 2us(1560) | rebuild 0us(3)
//
// Reading the line: average microseconds per call, (call count in window).
// "resolve.aux 0us(0)" means the aux layer was frozen (skipped) every frame —
// the layered dirty-marking cache is doing its job.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cad::perf {

/// Accumulates per-bucket timings; prints a summary every N frame ticks.
class Probe
{
public:
    static Probe& get()
    {
        static Probe instance;
        return instance;
    }

    [[nodiscard]] bool enabled() const { return m_enabled; }

    /// Add @p ns nanoseconds to the bucket named @p name (created on first
    /// use). No-op when disabled.
    void add(const char* name, int64_t ns)
    {
        if (!m_enabled) return;
        for (auto& b : m_buckets) {
            if (b.name && std::strcmp(b.name, name) == 0) {
                b.totalNs += ns;
                ++b.count;
                return;
            }
            if (!b.name) {
                b.name = name;
                b.totalNs = ns;
                b.count = 1;
                return;
            }
        }
    }

    /// Record a plain occurrence (no timing) — shows as 0us with a count.
    void countEvent(const char* name) { add(name, 0); }

    /// Advance the frame counter; flushes the summary every flushInterval().
    /// Called once per logical frame (end of ParamDocument::resolveAll()).
    void frameTick()
    {
        if (!m_enabled) return;
        if (++m_frames >= m_flushEvery)
            flush();
    }

    /// Print and reset all buckets (names are kept).
    void flush()
    {
        if (!m_enabled) return;
        std::fprintf(stderr, "[gcad-perf] %df", m_frames);
        for (const auto& b : m_buckets) {
            if (!b.name) break;
            const double avgUs = b.count > 0
                ? static_cast<double>(b.totalNs) / 1000.0 / static_cast<double>(b.count)
                : 0.0;
            std::fprintf(stderr, " | %s %.1fus(%lld)", b.name, avgUs,
                         static_cast<long long>(b.count));
        }
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
        for (auto& b : m_buckets) {
            b.totalNs = 0;
            b.count = 0;
        }
        m_frames = 0;
    }

private:
    Probe()
    {
        const char* v = std::getenv("GCAD_PROFILE");
        m_enabled = (v && *v && *v != '0');
        if (m_enabled)
            std::fprintf(stderr, "[gcad-perf] profiling enabled (flush every %d frames)\n",
                         m_flushEvery);
    }

    ~Probe()
    {
        // Flush the partial window so short sessions still produce output.
        if (m_enabled && m_frames > 0)
            flush();
    }

    struct Bucket {
        const char* name = nullptr;  ///< Static string literal — never freed.
        int64_t totalNs = 0;
        int64_t count   = 0;
    };

    static constexpr int kMaxBuckets = 64;  // was 16 — instrumentation points
                                            // exceeded it, silently dropping
                                            // snap.segment & friends from reports.
    Bucket m_buckets[kMaxBuckets]{};
    bool m_enabled   = false;
    int  m_frames    = 0;
    int  m_flushEvery = 120;
};

/// RAII scoped timer: measures the enclosing block when profiling is enabled.
class ScopeTimer
{
public:
    explicit ScopeTimer(const char* name)
        : m_name(name)
        , m_active(Probe::get().enabled())
    {
        if (m_active)
            m_start = std::chrono::steady_clock::now();
    }

    ~ScopeTimer()
    {
        if (m_active) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - m_start)
                                .count();
            Probe::get().add(m_name, ns);
        }
    }

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    const char* m_name;
    bool m_active;
    std::chrono::steady_clock::time_point m_start{};
};

} // namespace cad::perf

// Two-level concat so __LINE__ expands before token pasting.
#define GCAD_PERF_CONCAT_(a, b) a##b
#define GCAD_PERF_CONCAT(a, b) GCAD_PERF_CONCAT_(a, b)

/// Time the enclosing scope into bucket @p name (a string literal).
#define GCAD_PERF_SCOPE(name) \
    ::cad::perf::ScopeTimer GCAD_PERF_CONCAT(gcad_perf_scope_, __LINE__)(name)

/// Record a plain event count into bucket @p name.
#define GCAD_PERF_EVENT(name) ::cad::perf::Probe::get().countEvent(name)
