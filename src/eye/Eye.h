// Eye-diagram math: TX waveform generation, simple channel models, and
// UI-aligned folding into a 2D bin grid. Rendering (Qt overlay or popup
// widget) is layered on top in a separate module.
//
// The eye is the canonical SI debug view: every UI-length window of the
// received waveform stacked on top of itself reveals jitter, ringing,
// and inter-symbol interference. The "eye opening" is what the protocol
// spec mask compares against.

#pragma once

#include <cstddef>
#include <vector>

namespace sikit::eye {

// 2D histogram of waveform values folded into one UI (unit interval).
//   counts[t + v*time_bins]   row-major over (volt, time)
// v_min/v_max bracket the data range so an overlay shader can map them
// to colormap intensity.
struct EyeGrid {
    int time_bins = 0;
    int volt_bins = 0;
    std::vector<int> counts;
    double v_min = 0.0;
    double v_max = 0.0;

    int& at(int t_bin, int v_bin) { return counts[t_bin + v_bin * time_bins]; }
    int  at(int t_bin, int v_bin) const { return counts[t_bin + v_bin * time_bins]; }

    // Maximum bin count — useful for normalizing intensity at render time.
    int max_count() const;
};

// Generate an NRZ TX waveform: each bit becomes `samples_per_ui` samples
// at level +1 (logic 1) or -1 (logic 0). Total samples = bits.size() * spu.
std::vector<double> nrz_waveform(const std::vector<int>& bits,
                                  int samples_per_ui);

// Pseudo-random binary sequence (PRBS-7): length 127, polynomial x⁷+x⁶+1.
// Returns `num_bits` bits (the sequence repeats every 127 bits).
std::vector<int> prbs7(int num_bits);

// First-order IIR RC low-pass filter.
//   α = exp(-2π·fc·dt);  y[n] = α·y[n-1] + (1-α)·x[n]
// `dt` is the sample period in seconds; `cutoff_hz` is the -3 dB corner.
std::vector<double> rc_lowpass(const std::vector<double>& x,
                                double dt,
                                double cutoff_hz);

// Fold `y` into UI-aligned bins. Use multiple-UI alignment by repeating
// the window every `samples_per_ui` samples through the waveform. Optionally
// skip the first `warmup_uis` UIs so initial transients don't pollute the eye.
EyeGrid build_eye(const std::vector<double>& y,
                  int samples_per_ui,
                  int time_bins = 128,
                  int volt_bins = 128,
                  int warmup_uis = 4);

}  // namespace sikit::eye
