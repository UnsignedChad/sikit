#include "eye/Eye.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sikit::eye {

int EyeGrid::max_count() const {
    int m = 0;
    for (int c : counts) {
        if (c > m) m = c;
    }
    return m;
}

std::vector<double> nrz_waveform(const std::vector<int>& bits,
                                  int samples_per_ui) {
    std::vector<double> out;
    out.reserve(bits.size() * static_cast<std::size_t>(samples_per_ui));
    for (int bit : bits) {
        const double level = bit ? 1.0 : -1.0;
        for (int s = 0; s < samples_per_ui; ++s) out.push_back(level);
    }
    return out;
}

std::vector<int> prbs7(int num_bits) {
    // x^7 + x^6 + 1, 7-bit LFSR. Period = 127.
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(num_bits));
    unsigned int lfsr = 0x7F;  // 7 bits, all ones — any non-zero seed works.
    for (int i = 0; i < num_bits; ++i) {
        const unsigned int bit = ((lfsr >> 6) ^ (lfsr >> 5)) & 1u;
        lfsr = ((lfsr << 1) | bit) & 0x7Fu;
        out.push_back(static_cast<int>(bit));
    }
    return out;
}

std::vector<double> rc_lowpass(const std::vector<double>& x,
                                double dt,
                                double cutoff_hz) {
    std::vector<double> y(x.size(), 0.0);
    if (x.empty()) return y;
    const double alpha = std::exp(-2.0 * std::numbers::pi * cutoff_hz * dt);
    const double beta = 1.0 - alpha;
    y[0] = beta * x[0];
    for (std::size_t i = 1; i < x.size(); ++i) {
        y[i] = alpha * y[i - 1] + beta * x[i];
    }
    return y;
}

EyeGrid build_eye(const std::vector<double>& y,
                  int samples_per_ui,
                  int time_bins,
                  int volt_bins,
                  int warmup_uis) {
    EyeGrid g;
    g.time_bins = time_bins;
    g.volt_bins = volt_bins;
    g.counts.assign(static_cast<std::size_t>(time_bins) *
                    static_cast<std::size_t>(volt_bins), 0);

    if (y.empty() || samples_per_ui <= 0 ||
        time_bins <= 0 || volt_bins <= 0) {
        return g;
    }

    // Voltage range from the data, with a small margin so the extremes
    // don't fall outside the bin grid.
    double y_min = *std::min_element(y.begin(), y.end());
    double y_max = *std::max_element(y.begin(), y.end());
    if (y_min == y_max) {
        y_min -= 0.5;
        y_max += 0.5;
    } else {
        const double margin = 0.05 * (y_max - y_min);
        y_min -= margin;
        y_max += margin;
    }
    g.v_min = y_min;
    g.v_max = y_max;

    const std::size_t start = static_cast<std::size_t>(warmup_uis) *
                              static_cast<std::size_t>(samples_per_ui);
    if (start >= y.size()) return g;

    const double v_span = y_max - y_min;

    for (std::size_t i = start; i < y.size(); ++i) {
        // Time bin: which fraction of the UI does this sample occupy?
        const int t_in_ui = static_cast<int>(i % static_cast<std::size_t>(samples_per_ui));
        int t_bin = (t_in_ui * time_bins) / samples_per_ui;
        if (t_bin < 0) t_bin = 0;
        if (t_bin >= time_bins) t_bin = time_bins - 1;

        // Voltage bin.
        int v_bin = static_cast<int>((y[i] - y_min) / v_span * volt_bins);
        if (v_bin < 0) v_bin = 0;
        if (v_bin >= volt_bins) v_bin = volt_bins - 1;

        ++g.counts[static_cast<std::size_t>(t_bin) +
                   static_cast<std::size_t>(v_bin) *
                       static_cast<std::size_t>(time_bins)];
    }
    return g;
}

}  // namespace sikit::eye
