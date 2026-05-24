// S-parameter math: 2-port cascade via T-parameter conversion, plus
// insertion/return loss helpers and mixed-mode (differential) extraction
// from a 4-port single-ended block.
//
// Reference: Pozar, Microwave Engineering, Ch. 4. T-parameter cascade is
// the textbook trick: convert each S-matrix to its T (transmission) form,
// multiply T_A · T_B, convert back to S.

#pragma once

#include <complex>
#include <stdexcept>

#include <Eigen/Dense>

#include "touchstone/Touchstone.h"

namespace sikit::sparam {

using Complex = std::complex<double>;

// Convert a 2-port S-matrix to its T (transmission) form. Throws if S21 is
// zero (cascading a perfectly blocking network is undefined).
Eigen::Matrix2cd s_to_t(const Eigen::Matrix2cd& s);

// Inverse of s_to_t. Throws if T22 is zero.
Eigen::Matrix2cd t_to_s(const Eigen::Matrix2cd& t);

// Cascade two 2-port networks A → B at a single frequency point.
//   S_cascade = T_to_S( S_to_T(A) · S_to_T(B) )
Eigen::Matrix2cd cascade(const Eigen::Matrix2cd& a,
                          const Eigen::Matrix2cd& b);

// Cascade two Touchstone files frequency-by-frequency. Both must be 2-port
// and share the same frequency grid.
touchstone::TouchstoneFile cascade(const touchstone::TouchstoneFile& a,
                                    const touchstone::TouchstoneFile& b);

// Extract the differential-mode 2×2 S-matrix Sdd from a 4-port
// single-ended block. Convention: ports 1,3 are the "positive" halves of
// pairs A and B; ports 2,4 are the "negative" halves. The standard mixed-
// mode transformation matrix is applied.
Eigen::Matrix2cd single_ended_to_differential(const Eigen::Matrix4cd& s_se);

// Insertion loss in dB. Positive values = attenuation.
double insertion_loss_db(Complex s21);

// Return loss in dB. Positive values = good match (low reflection).
double return_loss_db(Complex s11);

struct SParamError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace sikit::sparam
