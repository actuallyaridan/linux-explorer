#pragma once

#include <QString>
#include <vector>
#include <algorithm>
#include <cmath>

// Reproduces WinSAT's Windows Experience Index: each component's raw metric is
// mapped through a piecewise-linear curve (Microsoft's own curves are
// undisclosed) to a subscore, and the base score is the MINIMUM of the five
// subscores, not the average.
//
// Scale is 1.0-9.9 (current WinSAT, Win8-Win11), clamped and rounded to 0.1.
//
// Calibration is fitted to one machine (AMD Ryzen, RX 6700 XT, DDR4-3200,
// SATA 870 EVO / NVMe 980) against a real `winsat formal` run: Disk and Memory
// are anchored to measured throughput matching WinSAT's own scores. Graphics
// and Gaming have no ground truth since modern WinSAT no longer runs the D3D
// test, so those curves are plausible shapes anchored where this GPU
// saturates. More `winsat` samples, especially weaker hardware, would help
// pin down the curve slopes.

namespace Wei {

inline constexpr double kMinScore = 1.0;
inline constexpr double kMaxScore = 9.9;   // current WinSAT ceiling

struct Anchor { double raw; double score; };

// Interpolates `raw` between surrounding anchors, clamps to [kMinScore,
// kMaxScore], and rounds to one decimal (WEI granularity).
inline double scoreFromTable(double raw, const std::vector<Anchor> &t)
{
    if (raw <= 0.0 || t.empty())
        return 0.0; // not assessed
    if (raw <= t.front().raw) return kMinScore;
    if (raw >= t.back().raw)  return kMaxScore;
    for (size_t i = 1; i < t.size(); ++i) {
        if (raw <= t[i].raw) {
            const Anchor &a = t[i - 1], &b = t[i];
            const double f = (raw - a.raw) / (b.raw - a.raw);
            double s = a.score + f * (b.score - a.score);
            s = std::clamp(s, kMinScore, kMaxScore);
            return std::round(s * 10.0) / 10.0;
        }
    }
    return kMaxScore;
}

// Memory: one-direction streaming-copy bandwidth in MiB/s (WinSAT convention).
// Our AVX2 kernel measures ~23,700 MiB/s on the reference machine where
// WinSAT's own kernel reports 34,503; anchored to our kernel's output so the
// score still lands at WinSAT's 9.2 despite the ~1.45x raw difference.
inline const std::vector<Anchor> kMemory = {
    {  3000, 2.0 }, {  6000, 3.5 }, { 10000, 5.0 }, { 14000, 6.3 },
    { 18000, 7.5 }, { 23700, 9.2 }, { 30000, 9.9 },
};

// CPU: aggregate compression + encryption throughput across all cores, MB/s.
// Calibrated point: 9,476 -> 9.2.
inline const std::vector<Anchor> kCpu = {
    {   300, 2.0 }, {   800, 3.0 }, {  1600, 4.0 }, {  2800, 5.0 },
    {  4200, 6.0 }, {  6000, 7.0 }, {  9476, 9.2 }, { 13000, 9.9 },
};

// Disk: sequential read in MB/s. Calibrated point: 566 -> 8.2 (SATA SSD);
// NVMe-class throughput tops the curve.
inline const std::vector<Anchor> kDisk = {
    {    30, 2.0 }, {    60, 3.0 }, {   110, 4.0 }, {   160, 5.0 },
    {   300, 6.5 }, {   450, 7.5 }, {   566, 8.2 }, {  1200, 9.5 },
    {  2000, 9.9 },
};

// Graphics (Aero / 2D compositing): alpha-blended fill rate in fps. Not
// calibrated to real fps since modern WinSAT caps DWM graphics at 9.9; top
// anchored where a high-end GPU saturates.
inline const std::vector<Anchor> kGraphics = {
    {    15, 2.0 }, {    35, 3.5 }, {    60, 5.0 }, {   100, 6.5 },
    {   150, 8.0 }, {   222, 9.9 },
};

// Gaming (3D): scene throughput in fps. Not calibrated: modern WinSAT no
// longer runs the D3D test (returns a hardcoded sentinel), so there is no
// ground truth.
inline const std::vector<Anchor> kGaming = {
    {   200, 2.0 }, {   600, 3.5 }, {  1200, 5.0 }, {  2200, 6.5 },
    {  3500, 8.0 }, {  6049, 9.9 },
};

} // namespace Wei
