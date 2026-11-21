// Copyright (c) 2026 Shantanu Agarwal
// SPDX-License-Identifier: AGPL-3.0-only

// S2 -- the symmetric 2x2 eigendecomposition, and its acceptance suite.
//
// WHAT THIS CHECKS, AND WHY IT IS BUILT THE WAY IT IS. The contract lives in
// vc_linalg.h. This file is the part that would catch a wrong implementation,
// and most groups below exist because some specific wrong implementation
// would otherwise pass.
//
// NOT ALL OF THEM, AND THE EXCEPTIONS ARE DELIBERATE. Group 2b's precision
// figures and group 4's near-degenerate fixture are REPORTED AND FAIL
// NOTHING. They are there because the numbers are worth seeing, not because
// the suite has an opinion about them -- in both cases the property at stake
// is unreachable for one of the two closed forms a learner can legitimately
// derive, so gating it would fail correct work. Each says so at the point of
// use. Everything else is a gate.
//
// THE ORACLE IS Eigen, AND IT IS SET UP TO BE GENUINELY INDEPENDENT.
//
//   - It runs in DOUBLE (Matrix2d), built from the same float32 inputs. An
//     oracle in float32 cannot settle a float32 disagreement -- it would be
//     as wrong as the thing it judges, and in the same direction.
//   - It uses the DEFAULT compute(), not computeDirect(). compute() runs
//     tridiagonalisation plus a QR iteration; computeDirect() solves the
//     characteristic polynomial in closed form, which is the same algebra the
//     code under test uses. Sharing algebra with your oracle means sharing
//     its blind spots.
//   - Eigen documents its eigenvalues as "sorted in increasing order", the
//     OPPOSITE of this library's descending convention. So the cross-check
//     MAPS major-to-[1] and minor-to-[0]. It does not sort both and compare,
//     because sorting both would hide an implementation that emits the pair
//     the wrong way round -- and the ordering is part of the contract.
//
// WHAT EIGEN CANNOT BE THE ORACLE FOR. SelfAdjointEigenSolver references only
// the lower triangle, so it accepts an asymmetric matrix and silently ignores
// half of it. Group 5 therefore tests the rejection against the contract
// directly, with no oracle involved.
//
// THE ARBITRARY-DIRECTION CASES ARE DECIDED BY THE FIXTURE, NEVER BY THE
// RESULT. Group 4 asserts `directions_arbitrary` as an OUTPUT, and picks
// fixtures whose right answer is known in advance by construction. The
// tempting alternative -- skip the eigenvector checks whenever the result
// says the directions are arbitrary -- would be passed trivially by an
// implementation that always says so and always returns the basis. That is
// the same shape of defect this library already shipped once in
// gradient_magnitude, where a dtype check passed vacuously; it is not being
// shipped again. (Section B and section F.2(d) both ask for that skip. H.5
// records why this supersedes them.)
//
// WHERE EXPECTED VALUES COME FROM, since the suite must not quietly encode
// the scaffolder's arithmetic as the standard. Three sources, and no others:
// the ORACLE (groups 2b, 3), the DEFINING IDENTITIES -- trace, determinant,
// the eigen residual, orthogonality (groups 2, 4) -- and, in group 4 only,
// values known BY CONSTRUCTION from the fixture, which is why the identity's
// eigenvalue appears there as a literal 1.0. Group 1's trace fixture is
// hand-arithmetic on two integers. The only numbers a learner must supply
// are group 6's, which is the point of group 6.

#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

#include <Eigen/Eigenvalues>

#include "vc/core/vc_error_code.h"
#include "vc/core/vc_exception.h"
#include "vc/math/vc_linalg.h"

namespace {

bool check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
    }
    return condition;
}

vc::math::vc_mat2 sym(float a, float b, float c) {
    return vc::math::vc_mat2{{{{{a, b}}, {{b, c}}}}};
}

// The oracle. Double precision, default algorithm, built from the float
// inputs so it is judging exactly the matrix the code under test was given.
//
// AND IT CHECKS info(), WHICH IS NOT OPTIONAL. The default compute() runs an
// ITERATION, and Eigen reports a failure to converge by setting info() to
// NoConvergence -- after which eigenvalues() returns whatever was in the
// buffer. Nothing complains. Every comparison in this file would then be
// against garbage, and since the oracle is the reference, the suite would
// report the IMPLEMENTATION as wrong. An oracle that can fail silently is
// not an oracle, and this file's whole argument is that a check which can
// pass while broken is worth nothing -- the same standard has to apply to
// the thing doing the checking.
//
// It throws rather than returning a flag so the failure cannot be ignored at
// a call site, and the message names Eigen so the blame lands correctly.
// 2x2 is the easiest case there is and this should never fire; that is not a
// reason to leave it out.
Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d>
oracle(const vc::math::vc_mat2& m) {
    Eigen::Matrix2d e;
    e << static_cast<double>(m.m[0][0]), static_cast<double>(m.m[0][1]),
        static_cast<double>(m.m[1][0]), static_cast<double>(m.m[1][1]);
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> solver(e);
    if (solver.info() != Eigen::Success) {
        throw vc::vc_exception(
            vc::vc_error_code::invalid_argument,
            "the Eigen ORACLE failed to converge -- this is a failure of the "
            "reference, not of vc::math::eigen_symmetric");
    }
    return solver;
}

// Mixed absolute/relative: relative to `want` once `want` exceeds 1, absolute
// below that. These matrices have no fixed scale -- a structure tensor from a
// bright image and one from a dim image differ by orders of magnitude -- so a
// purely absolute bound would be wrong for at least one of them. The floor at
// 1.0 is what makes `want == 0` comparable at all, since nothing is within a
// relative distance of zero. So this is NOT purely relative, and the name is
// shorthand; the floor matters whenever an expected value is small.
//
// P2's two tolerance bugs were both an unstated tolerance scaled against the
// wrong quantity, which is why this one is spelled out rather than inlined.
bool close_rel(double got, double want, double tol) {
    const double scale = std::max(1.0, std::abs(want));
    return std::abs(got - want) <= tol * scale;
}

// float32 carries about 1.2e-7 of relative error simply by existing, and a
// closed form spends a handful of operations on top of that. 1e-5 leaves
// roughly two decimal digits of headroom over that floor.
//
// NOTE that P3 section B says "to 1e-6" without naming a precision. Section
// F.2(i) makes that argument about the least-squares keystone, not about this
// row -- it is the PRINCIPLE that carries over, not a ruling that already
// covers eigen2x2. vc_mat2 is float32, so 1e-6 absolute is not a meaningful
// target at every scale; this is 1e-5 mixed instead, and the substitution is
// deliberate and recorded in section H.
constexpr double k_tol = 1e-5;

// The residual ||a.v - lambda.v|| is NOT scale-free: it carries the matrix's
// units, so a CORRECT float32 result on a matrix with a diagonal of 1e4
// leaves a residual near 1e-3, and an absolute 1e-5 bound would reject it.
//
// SCALED BY THE LARGER EIGENVALUE, NOT BY THE PAIR'S OWN. That is the whole
// subtlety, and getting it wrong is P2's tolerance bug a third time. The
// floating-point floor under this residual is set by the size of the MATRIX
// -- roughly eps times its largest entry -- not by the eigenvalue being
// checked. So on an EDGE, where lambda_minor is near zero while the matrix
// is near 1e4, scaling by lambda_minor collapses the bound back to an
// absolute 1e-5 -- and that is far under the floor. Check the floor by hand:
// the residual is a difference of quantities of size ||a||, so it cannot be
// smaller than about eps times that, and 1.2e-7 * 1e4 is roughly 1e-3. An
// absolute 1e-5 bound sits a hundred times BELOW the noise, so it would fail
// CORRECT code. Scaled by lambda_major the bound is 1e-1 and the same
// tolerance means the same thing at every scale.
//
// Section F.2(c) records P2 making this exact mistake: an absolute bound
// that passed at 5x5 and failed by 37x at 32x32 on correct code, because it
// was scaled against the wrong quantity.
//
// HONESTLY: THE SCALE CLAIM ITSELF IS UNTESTED, and a fixture SHOULD be
// added. No gated check applies a residual at large scale -- group 2 is of
// order 1 and group 4's gated fixtures have a residual of exactly zero -- so
// nothing here demonstrates that one bound means the same thing at every
// scale.
//
// An earlier version of this comment declined to add one, on the grounds that
// a large-scale residual is reachable by only one of the two closed forms and
// so the check would be a precision gate wearing a correctness label. THAT
// WAS WRONG, and the arithmetic says so. On [[1e4, 1],[1, 1]] both forms
// return lambda_major = exactly 1e4, so the eigenvector taken from the FIRST
// row, v = (b, lambda - a), is (1, 0) -- residual norm 1 against a bound of
// 0.1 -- while the SECOND row, v = (lambda - d, c), gives (9999, 1) with a
// residual of 0. Identical for both discriminants. The failure is the choice
// of ROW, not the choice of form.
//
// Which makes it a correctness property, not a precision one: neither row
// works alone (row 2 collapses on a diagonal matrix at lambda = d), so the
// robust rule is to take the LONGER of the two row vectors. That is gateable
// and form-independent, and this file should grow a fixture for it.
bool residual_ok(const vc::math::vc_vec2& res,
                 const vc::math::vc_eigen_2x2& r) {
    const double mag = std::max(std::abs(static_cast<double>(r.lambda_major)),
                                std::abs(static_cast<double>(r.lambda_minor)));
    return static_cast<double>(vc::math::norm(res)) <=
           k_tol * std::max(1.0, mag);
}

} // namespace

int main() {
    using namespace vc::math;

    int passed = 0;
    int total_checks = 0;
    auto expect = [&](bool ok, const std::string& what) {
        ++total_checks;
        if (check(ok, what)) {
            ++passed;
        }
    };

    try {
        // ---- 1. trace, on its own ----------------------------------------
        //
        // Checked before anything else because the eigenvalue identities in
        // group 2 are stated IN TERMS of trace, so a broken trace would
        // misreport them as eigenvalue failures.
        {
            // Entries chosen so that every wrong formula gives a wrong
            // answer: a != c (so swapping them shows), and neither is 0 or 1
            // (so a dropped or doubled term shows).
            expect(close_rel(static_cast<double>(trace(sym(3.0F, 7.0F, 11.0F))),
                             14.0, k_tol),
                   "trace sums the DIAGONAL entries (3 + 11), not all four");

            // The off-diagonal must not contribute. Same diagonal, different
            // b: the trace cannot move.
            expect(
                close_rel(static_cast<double>(trace(sym(3.0F, 999.0F, 11.0F))),
                          14.0, k_tol),
                "trace ignores the off-diagonal entirely");
        }

        // ---- 2. the invariants, on a well-split fixture -------------------
        //
        // FIXTURE CHOICE IS LOAD-BEARING, and section H.3(m) is the rule:
        // pick values where every power is distinguishable. b = 2, not 1,
        // because at b = 1 the values b and b*b are equal, so a formula using
        // one where the other belongs passes. Week 9's suite caught exactly
        // that shape of slip (a 0.125 that should have been 0.25) only
        // because the number was not 1. Then a = 9, c = 3: a != c, NEITHER is
        // 1, and the half-difference (a - c)/2 = 3 differs from b = 2, so
        // swapping those two terms is visible as well. The mean is 6 and the
        // discriminant sqrt(9 + 4) is irrational, which is deliberate -- a
        // fixture with a tidy integer answer tends to have a tidy wrong
        // answer nearby.
        {
            const vc_mat2 m = sym(9.0F, 2.0F, 3.0F);
            const vc_eigen_2x2 r = eigen_symmetric(m);

            // (k) RAW ORDERING, asserted on the output itself and before any
            // comparison to the oracle. Group 3 maps rather than sorts, but
            // this is the check that makes the ordering contract real.
            expect(r.lambda_major >= r.lambda_minor,
                   "lambda_major >= lambda_minor (descending, as declared)");

            // (j) UNIT LENGTH, asserted alongside the residual below and not
            // instead of it. The residual identity ||a.v - lambda.v|| = 0 is
            // satisfied EXACTLY by the zero vector, so a routine returning
            // (0,0) passes every residual check in this file. These two lines
            // are what stop that.
            expect(close_rel(static_cast<double>(norm(r.v_major)), 1.0, k_tol),
                   "v_major is unit length (a zero vector would pass the "
                   "residual check vacuously)");
            expect(close_rel(static_cast<double>(norm(r.v_minor)), 1.0, k_tol),
                   "v_minor is unit length");

            // Symmetric matrices have orthogonal eigenvectors. This is a
            // guarantee of the symmetry, not a coincidence of the fixture.
            expect(std::abs(static_cast<double>(dot(r.v_major, r.v_minor))) <=
                       k_tol,
                   "the two eigenvectors are orthogonal");

            // THE RESIDUALS ARE PER PAIR, which is the point. A result with
            // v_major and v_minor SWAPPED still has both unit length, still
            // orthogonal, still correctly ordered eigenvalues, and still
            // agrees with the oracle on the eigenvalues. Only pairing each
            // vector with its OWN eigenvalue catches it.
            const vc_vec2 res_major =
                m * r.v_major - r.v_major * r.lambda_major;
            const vc_vec2 res_minor =
                m * r.v_minor - r.v_minor * r.lambda_minor;
            expect(residual_ok(res_major, r),
                   "a * v_major == lambda_major * v_major (pairs v_major with "
                   "its OWN eigenvalue -- catches a swapped pair)");
            expect(residual_ok(res_minor, r),
                   "a * v_minor == lambda_minor * v_minor");

            // The two identities R18 taught, now used as tests. These hold
            // for any square matrix and are independent of the eigenvectors,
            // so they catch an eigenvalue pair that is wrong in a way the
            // oracle comparison might tolerate at the edge of tolerance.
            expect(close_rel(static_cast<double>(trace(m)),
                             static_cast<double>(r.lambda_major) +
                                 static_cast<double>(r.lambda_minor),
                             k_tol),
                   "trace == lambda_major + lambda_minor");
            expect(close_rel(static_cast<double>(determinant(m)),
                             static_cast<double>(r.lambda_major) *
                                 static_cast<double>(r.lambda_minor),
                             k_tol),
                   "determinant == lambda_major * lambda_minor");

            // The flag, in the direction that is easy to forget. Group 4 does
            // the true cases; this is the false one, and without it an
            // implementation that ALWAYS reports degenerate passes group 4
            // completely.
            expect(!r.directions_arbitrary,
                   "a well-split matrix is NOT reported degenerate");
        }

        // ---- 2b. SCALE: what is GATED here, and what is only REPORTED --
        //
        // Group 2's entries are of order 1. A structure tensor's are not, and
        // this library's own arithmetic says how far up they go: f32 pixels
        // here carry the 0-255 scale directly (there is no /255 anywhere in
        // src/), and 17_gradients pins the gradient -- a step of 100 gives
        // dx = 50 exactly. So |dx| near 100 needs a step of 200, which a
        // high-contrast edge supplies, and <dx*dx> then runs to 1e4. Nothing
        // in group 2 can see a failure that appears only there.
        //
        // WHAT THE FIRST FIXTURE ACTUALLY IS. Stated carefully, because an
        // earlier draft of this comment had it backwards and called it a
        // "bright smooth region". It is not. a = c = 1e4 with b near zero
        // means |dx| and |dy| are each about 100 and UNCORRELATED -- strong
        // gradient energy in both directions at once, which is isotropic
        // texture, and in Harris's terms a CORNER with both eigenvalues
        // large. Smooth would mean SMALL gradients and therefore a small
        // tensor, whatever the brightness; a flat region is the zero matrix,
        // and group 4 has that one.
        //
        // THE DIVIDING LINE, a deliberate decision rather than an oversight.
        // Two different things go wrong at scale:
        //
        //   NaN is a CORRECTNESS failure. Section B requires the eigenvalues
        //   of a symmetric matrix to be REAL, and NaN is not a real number.
        //   A NaN reaches week 10's response map as a value where every
        //   threshold comparison is false, so the detector silently finds
        //   nothing. Finiteness is therefore GATED below -- but see the note
        //   on the second fixture for what actually produces one, because it
        //   is not what an earlier draft of this comment claimed.
        //
        //   Lost PRECISION is a different matter, and gating it would be
        //   dishonest at this tier -- because BOTH closed forms you can
        //   reasonably derive lose accuracy somewhere in this table, just in
        //   different places. Section B puts this row at tier I,
        //   baseline-acceptable, and Harris uses only det and trace, never
        //   these eigenvalues. So the error is MEASURED AND PRINTED below and
        //   fails nothing.
        //
        // WHY IT HAPPENS, in numbers you can check by hand. float32 keeps
        // about seven significant digits, so the spacing between neighbouring
        // representable values grows with magnitude:
        //
        //   near 1e4   the spacing is about 0.001
        //   near 1e8   the spacing is exactly 8
        //   near 9e8   the spacing is exactly 64
        //
        // Take a = c = 10000, b = 1. A discriminant reached through the
        // determinant needs ac - b*b = 100000000 - 1. But 1e8 sits where
        // consecutive float32 values are 8 apart, so subtracting 1 cannot
        // change the stored value at all: the determinant comes back as
        // exactly 1e8, the discriminant as exactly 0, and both eigenvalues as
        // 10000 -- where the true pair is 10001 and 9999.
        //
        // BE PRECISE ABOUT THE HARM, because an earlier draft overstated that
        // too. Both eigenvalues still come back LARGE, so the neighbourhood
        // still reads as a corner -- it is NOT misclassified as flat. What is
        // destroyed is the gap, a relative error of 1e-4, and with the gap go
        // the directions, which then get reported arbitrary.
        //
        // THE SECOND FIXTURE IS A LESSON IN ITS OWN RIGHT, and an earlier
        // draft of this file got it badly wrong by presenting it as proof
        // that one formula is unusable. Hand-check (30000, 0.1, 30001): near
        // 9e8 every float32 is a multiple of 64. The true determinant is
        // 900029999.99 and the true mean*mean is 900030000.25, and BOTH round
        // to the same stored value, 900030016. The radicand is 0. Finite.
        //
        // And yet on this toolchain, a discriminant written as the single
        // expression sqrt(mean * mean - determinant(a)) returns NaN here.
        // The cause is FUSED MULTIPLY-ADD: the compiler may evaluate
        // `mean * mean - det` as one instruction that keeps mean * mean at
        // full precision and never rounds it, so what is subtracted is the
        // ROUNDED det -- and the difference is then simply det's rounding
        // error with the sign flipped, 900030000.25 - 900030016 = -15.75.
        // Negative, so the square root is NaN.
        //
        // Which makes the outcome depend on things that are not the maths.
        // On Apple clang 17 / arm64 -- where an FMA instruction always exists,
        // so the compiler is free to use one -- the three settings give:
        //
        //   compiler default (-O0 or -O2)   one expression -> NaN, two -> 0
        //   -ffp-contract=off               one expression -> 0,   two -> 0
        //   -ffp-contract=fast              one expression -> NaN, two -> NaN
        //
        // That table is from one toolchain and is NOT a portable fact; on a
        // target without an FMA there is nothing to contract into and the
        // first row would read 0. Rather than ask you to trust it, the block
        // below prints both radicands as YOUR build computes them.
        //
        // THIS TREE SETS NO fp-contract FLAG and takes the compiler default.
        // So whether this fixture produces a NaN depends on how you split the
        // statement and on a flag nobody chose. It is therefore NOT a test of
        // which closed form you derived and must not be read as one. The
        // isfinite checks below stay because a NaN is never an acceptable
        // answer and the check costs nothing -- not because this fixture
        // reliably produces one.
        //
        // The third row is the one worth dwelling on, because it defeats both
        // forms. An edge gives one large eigenvalue and one near zero, and
        // there the small one is the difference of two numbers near 5000
        // where the spacing is already about 0.0005 -- larger than the answer
        // being computed. So both forms return 0 for it.
        //
        // That one is NOT a dead end, and this suite already holds the way
        // out without spelling it: group 2 asserts determinant ==
        // lambda_major * lambda_minor. Run it by hand on the third fixture.
        // b*b is 99.99 squared, and at that magnitude float32's spacing is
        // about 0.001, so b*b stores as 9998.0 and the determinant as
        // 10000 - 9998 = 2.
        //
        // That IS a subtraction of two large numbers, so be precise about
        // why it survives where the other does not -- an earlier draft of
        // this comment said it "never subtracts two large numbers", which is
        // plainly false. What matters is the size of the ANSWER against the
        // spacing where it is computed. Here the answer is 2, which is two
        // thousand times the 0.001 spacing at 1e4, so it keeps about three
        // digits. In the other route the answer is 2e-4 out of two numbers
        // near 5000, where the spacing is already 0.0005 -- the answer is
        // smaller than one step, so it keeps none at all. Then 2 / 10001 is
        // 1.9998e-4 against a true 2.0001e-4.
        //
        // Whether to use it is yours: section B does not ask for it, and
        // nothing in this file fails if you do not.
        {
            struct scale_case {
                vc_mat2 m;
                const char* what;
            };

            const scale_case cases[] = {
                {sym(10000.0F, 1.0F, 10000.0F),
                 "isotropic texture (a CORNER): a = c = 1e4, b = 1"},
                {sym(30000.0F, 0.1F, 30001.0F),
                 "diagonal nearly but NOT exactly equal"},
                {sym(10000.0F, 99.99F, 1.0F),
                 "an EDGE: one large eigenvalue, one near zero"},
            };

            // The FMA dependence, from this build rather than from a table.
            // Both lines are the SAME algebra; only the rounding differs,
            // because `m2` is forced through a float32 before the subtract
            // while the one-expression form may be contracted into an FMA
            // that never rounds the square. If your build folds these at
            // compile time you will see two zeroes, which is also an answer.
            {
                const vc_mat2 m = sym(30000.0F, 0.1F, 30001.0F);
                const vc::float32 mean = trace(m) * 0.5F;
                const vc::float32 det = determinant(m);
                const vc::float32 m2 = mean * mean;
                // Full precision on purpose: at the default six significant
                // figures both values print as 9.0003e+08, and the whole
                // point -- that they are the SAME stored number -- vanishes.
                // int, not streamsize: setprecision takes an int, and the
                // narrowing conversion is an -Wshorten-64-to-32 warning.
                const auto prec = static_cast<int>(std::cout.precision());
                std::cout << std::setprecision(10)
                          << "\n  [2b] FMA check on (30000, 0.1, 30001), as "
                             "THIS build computes it:\n"
                          << "        mean*mean (rounded) = "
                          << static_cast<double>(m2) << "\n"
                          << "        det                 = "
                          << static_cast<double>(det)
                          << "   <- the same stored value\n"
                          << "        one expression  mean*mean - det = "
                          << static_cast<double>(mean * mean - det)
                          << "   <- if negative, sqrt gives NaN\n"
                          << "        two statements  m2 - det        = "
                          << static_cast<double>(m2 - det) << "\n"
                          << std::setprecision(prec);
            }

            std::cout << "\n  [2b] scale diagnostics -- REPORTED, not gated:\n";
            for (const auto& c : cases) {
                const vc_eigen_2x2 r = eigen_symmetric(c.m);
                const auto es = oracle(c.m);

                // GATED: real, not NaN. This is the one that is not a matter
                // of taste -- see above.
                expect(std::isfinite(r.lambda_major) &&
                           std::isfinite(r.lambda_minor),
                       std::string("SCALE: eigenvalues are REAL, not NaN -- ") +
                           c.what);
                // GATED: and the directions are still directions.
                expect(close_rel(static_cast<double>(norm(r.v_major)), 1.0,
                                 k_tol) &&
                           close_rel(static_cast<double>(norm(r.v_minor)), 1.0,
                                     k_tol),
                       std::string("SCALE: eigenvectors are unit length -- ") +
                           c.what);

                // REPORTED: how much precision this fixture costs you.
                const double want_maj = es.eigenvalues()[1];
                const double want_min = es.eigenvalues()[0];
                const double err_maj =
                    std::abs(static_cast<double>(r.lambda_major) - want_maj);
                const double err_min =
                    std::abs(static_cast<double>(r.lambda_minor) - want_min);
                std::cout << "        " << c.what << "\n"
                          << "          true  " << want_maj << " , " << want_min
                          << "\n"
                          << "          got   "
                          << static_cast<double>(r.lambda_major) << " , "
                          << static_cast<double>(r.lambda_minor)
                          << "   abs err " << err_maj << " / " << err_min
                          << (r.directions_arbitrary
                                  ? "   [directions reported ARBITRARY]"
                                  : "")
                          << "\n";
            }
            std::cout << '\n';
        }

        // ---- 2c. THE ROW CHOICE ------------------------------------------
        //
        // The eigenvector comes from one row of (A - lambda*I), and NEITHER
        // row works alone. This gates that, and it is the check a
        // correct-LOOKING derivation most easily fails.
        //
        // Every step on [[1e4, 1],[1, 1]] is exact, so work it by hand.
        // lambda_major rounds to EXACTLY 1e4, so row 1, v = (b, lambda - a),
        // collapses to (1, 0). But a.(1,0) = (1e4, 1) while lambda.(1,0) =
        // (1e4, 0), so that vector's residual is exactly (0, 1) -- norm 1,
        // against a bound of k_tol * 1e4 = 0.1. Row 2, v = (lambda - d, c),
        // gives (9999, 1), whose residual is 0. Taking the LONGER row is what
        // picks the second.
        //
        // GATED, AND LEGITIMATELY SO: both closed forms return exactly 1e4
        // for lambda_major here, so unlike the figures in 2b this is not a
        // precision gate in disguise. It tests the choice of ROW, which is
        // form-independent.
        {
            const vc_mat2 m = sym(10000.0F, 1.0F, 1.0F);
            const vc_eigen_2x2 r = eigen_symmetric(m);
            const vc_vec2 res = m * r.v_major - r.v_major * r.lambda_major;
            expect(residual_ok(res, r),
                   "ROW CHOICE: v_major satisfies the eigen identity on "
                   "[[1e4,1],[1,1]] -- row 1 alone gives a residual of 1.0 "
                   "against a bound of 0.1");
        }

        // ---- 3. the Eigen cross-check ------------------------------------
        {
            // First, the oracle's own documented behaviour. Cheap, and it
            // means a future Eigen version that changed its sort order would
            // be caught here rather than misread as a failure of this code.
            {
                const auto es = oracle(sym(5.0F, 2.0F, 1.0F));
                expect(es.eigenvalues()[0] <= es.eigenvalues()[1],
                       "ORACLE SANITY: Eigen returns eigenvalues ascending, "
                       "as its documentation states");
            }

            // Deterministic rather than genuinely random: a fixed seed means
            // a failure is reproducible and bisectable. "Random symmetric
            // 2x2" in section B is about coverage, not unpredictability.
            std::mt19937 rng(20261120U);
            std::uniform_real_distribution<float> dist(-4.0F, 4.0F);

            int agreed = 0;
            constexpr int k_trials = 200;
            for (int i = 0; i < k_trials; ++i) {
                const vc_mat2 m = sym(dist(rng), dist(rng), dist(rng));
                const vc_eigen_2x2 r = eigen_symmetric(m);
                const auto es = oracle(m);

                // THE MAPPING, not a sort. Eigen ascending, ours descending.
                const bool ok = close_rel(static_cast<double>(r.lambda_major),
                                          es.eigenvalues()[1], k_tol) &&
                                close_rel(static_cast<double>(r.lambda_minor),
                                          es.eigenvalues()[0], k_tol);
                if (ok) {
                    ++agreed;
                }
            }
            expect(agreed == k_trials,
                   "eigenvalues agree with Eigen on all " +
                       std::to_string(k_trials) +
                       " pseudo-random symmetric matrices (" +
                       std::to_string(agreed) + " agreed)");
        }

        // ---- 4. the arbitrary-direction cases ----------------------------
        //
        // (l) HAND-PLACED, because random symmetric matrices never land here
        // -- the probability of drawing two equal eigenvalues is zero. Group
        // 3's 200 trials exercise this branch exactly never, so without this
        // group it ships untested.
        //
        // Every expectation below is known from the FIXTURE, never read back
        // from the result.
        //
        // ALL THREE GATED FIXTURES BELOW HAVE b = 0 AND a = c, so all three
        // are EXACTLY degenerate: both eigenvalues identical, every vector an
        // eigenvector, and the standard basis an exact eigenpair with a
        // residual of exactly zero. That is the easy half of the branch.
        //
        // The realistic half -- near degenerate, gap small but NOT zero, which
        // is what a structure tensor actually produces -- is the print block
        // AFTER this loop, and it is reported rather than gated. The reason is
        // below, and it is not laziness.
        //
        // WHAT THE NEAR-DEGENERATE SHAPE DOES. Take b = 0 with c just ABOVE a.
        // The eigenvalues then DIFFER (they are a and c themselves), the true
        // major axis is (0,1), and the contract hands back (1,0) as v_major
        // because the gap is under threshold. So v_major is paired with the
        // WRONG eigenvalue, by exactly the gap.
        //
        // THAT WOULD COUPLE TWO TOLERANCES THAT LOOK INDEPENDENT. If such a
        // residual were ever CHECKED, it would equal the gap, and the gap is
        // what degenerate_gap_tolerance bounds, while the residual bound is
        // k_tol times the eigenvalue magnitude -- so the check would hold only
        // while
        //
        //     degenerate_gap_tolerance <= k_tol
        //
        // and both are 1e-5 today, which is equality, with no headroom.
        //
        // THE RELATION IS CURRENTLY DORMANT, and saying so precisely matters.
        // No check in this file depends on it: the three gated fixtures have a
        // gap of exactly zero, so their residual is zero whatever the
        // tolerances are, and the near-degenerate case is only printed. The
        // relation would start to bite the moment anyone turns that print
        // block into expects. Two earlier drafts of this comment claimed this
        // group already enforces it. It does not, and never did.
        //
        // A SMALL MATRIX IS NOT A DEGENERATE ONE, and this gates the
        // difference. diag(1e-6, 2e-6) has an ABSOLUTE gap of 1e-6, under
        // degenerate_gap_tolerance of 1e-5, so a gap test written absolutely
        // calls it directionless. Its eigenvalue RATIO is 2 -- a clean edge --
        // and discarding the orientation of a low-contrast but perfectly
        // directional region is precisely what Harris cannot afford.
        //
        // It also exercises the row fallback: lambda_major = 2e-6 = d, so row
        // 2 gives (0, 0) and row 1 gives (0, 1e-6).
        //
        // THIS IS NOT THE FIXTURE DISCUSSED BELOW. That one needs a relative
        // gap just above the threshold, which only one closed form resolves.
        // This one's relative gap is 0.5, which both forms get right, so it
        // gates no precision.
        {
            const vc_mat2 m = sym(1e-6F, 0.0F, 2e-6F);
            const vc_eigen_2x2 r = eigen_symmetric(m);
            expect(!r.directions_arbitrary,
                   "SMALL IS NOT DEGENERATE: diag(1e-6, 2e-6) has a relative "
                   "gap of 0.5 and must NOT be flagged arbitrary (an absolute "
                   "gap test fails here)");
        }

        // The fixture that WOULD catch it -- relative gap just above the
        // threshold, required to come back un-flagged -- is deliberately
        // absent. See vc_linalg.h beside degenerate_gap_tolerance: a gap that
        // small is not reliably computable by both legitimate closed forms,
        // so the check would silently become a precision gate.
        //
        // The three fixtures below are the GATED ones. Every expectation for
        // them is known from the fixture, never read back from the result.
        {
            struct arbitrary_case {
                vc_mat2 m;
                double lambda_major; // by construction, from the fixture
                double lambda_minor;
                std::string what;
            };

            const arbitrary_case cases[] = {
                {sym(1.0F, 0.0F, 1.0F), 1.0, 1.0,
                 "the identity (every vector is an eigenvector, lambda = 1)"},
                {sym(0.0F, 0.0F, 0.0F), 0.0, 0.0,
                 "the zero matrix (a flat region's structure tensor)"},
                {sym(5.0F, 0.0F, 5.0F), 5.0, 5.0,
                 "a = c with b = 0 (a scaled identity)"},
            };

            for (const auto& c : cases) {
                const vc_eigen_2x2 r = eigen_symmetric(c.m);

                expect(r.directions_arbitrary,
                       "directions_arbitrary IS reported for " + c.what);

                // F.2(d) points at this exact line: with b = 0 and a = c,
                // every vector is an eigenvector and the obvious way to
                // normalise one divides by a zero-length vector. NaN is the
                // symptom. Checked explicitly rather than left to be caught
                // sideways by the unit-length comparison below, because a
                // check that catches a thing by accident does not record
                // that the thing was thought about.
                expect(std::isfinite(r.lambda_major) &&
                           std::isfinite(r.lambda_minor) &&
                           std::isfinite(r.v_major.x) &&
                           std::isfinite(r.v_major.y) &&
                           std::isfinite(r.v_minor.x) &&
                           std::isfinite(r.v_minor.y),
                       "nothing is NaN for " + c.what +
                           " -- F.2(d)'s divide-by-zero hazard is here");
                expect(close_rel(static_cast<double>(r.lambda_major),
                                 c.lambda_major, k_tol) &&
                           close_rel(static_cast<double>(r.lambda_minor),
                                     c.lambda_minor, k_tol),
                       "both eigenvalues are correct for " + c.what);

                // THE SPECIFIC BASIS, not merely "some orthonormal pair".
                // The header promises (1,0) and (0,1) by name; until this
                // check existed the promise was untested and any orthonormal
                // pair passed, so the documented contract and the enforced
                // one were two different things.
                expect(
                    close_rel(static_cast<double>(r.v_major.x), 1.0, k_tol) &&
                        std::abs(static_cast<double>(r.v_major.y)) <= k_tol &&
                        std::abs(static_cast<double>(r.v_minor.x)) <= k_tol &&
                        close_rel(static_cast<double>(r.v_minor.y), 1.0, k_tol),
                    "the directions are the STANDARD BASIS (1,0) and (0,1) "
                    "exactly as the header promises, for " +
                        c.what);

                // Unit and orthogonal are implied by the basis check above,
                // and kept anyway: they are the invariants the header states,
                // and a later contract change that drops the specific basis
                // should not silently drop these with it.
                expect(close_rel(static_cast<double>(norm(r.v_major)), 1.0,
                                 k_tol) &&
                           close_rel(static_cast<double>(norm(r.v_minor)), 1.0,
                                     k_tol),
                       "the directions are still UNIT, not zero, for " +
                           c.what);
                expect(std::abs(static_cast<double>(
                           dot(r.v_major, r.v_minor))) <= k_tol,
                       "the directions are still orthogonal for " + c.what);

                // The eigen identity, EXACT for all three: each has a gap
                // of precisely zero, so the standard basis is a true
                // eigenpair and the residual is not merely small but 0.
                const vc_vec2 res =
                    c.m * r.v_major - r.v_major * r.lambda_major;
                expect(residual_ok(res, r),
                       "v_major still satisfies the eigen identity for " +
                           c.what);
            }

            // ---- the NEAR-degenerate fixture: REPORTED, NOT GATED --------
            //
            // b = 0 with c just above a. Gap 0.05 on a magnitude of 1e4, so
            // 5e-6 relative -- HALF of degenerate_gap_tolerance, which means
            // the correct answer is known from the fixture: directions
            // arbitrary, and v_major = (1,0) even though the true major axis
            // is (0,1). That mispairing by exactly the gap is what the
            // coupling above is about, and it is the realistic shape, since
            // a structure tensor is near degenerate far oftener than exactly.
            //
            // SO WHY IS IT NOT GATED? Because no fixture can test this branch
            // without also testing precision. A relative gap of 5e-6 is far
            // inside the roughly 2*sqrt(eps) = 7e-4 error that a
            // determinant-based discriminant carries (see vc_linalg.h beside
            // degenerate_gap_tolerance), so one of the two legitimate closed
            // forms CANNOT resolve this gap at all, at any scale. Gating it
            // would quietly re-impose the precision gate this file decided
            // against two groups ago -- and would do it with failure messages
            // blaming the basis pairing when the cause is the discriminant.
            //
            // This is a judgement call, not a law. If you would rather the
            // suite insist on it, turn the four lines below into expects.
            {
                const vc_mat2 m = sym(10000.0F, 0.0F, 10000.05F);
                const vc_eigen_2x2 r = eigen_symmetric(m);
                const auto es = oracle(m);
                std::cout
                    << "\n  [4] near-degenerate (b=0, c just above a) -- "
                       "REPORTED, not gated:\n"
                    << "        expected from the fixture: arbitrary = yes, "
                       "v_major = (1,0)\n"
                    << "          true       " << es.eigenvalues()[1] << " , "
                    << es.eigenvalues()[0] << "\n"
                    << "          got        "
                    << static_cast<double>(r.lambda_major) << " , "
                    << static_cast<double>(r.lambda_minor) << "\n"
                    << "          arbitrary? "
                    << (r.directions_arbitrary ? "yes" : "NO") << "\n"
                    << "          v_major    ("
                    << static_cast<double>(r.v_major.x) << ", "
                    << static_cast<double>(r.v_major.y) << ")\n\n";
            }
        }

        // ---- 5. asymmetric input is rejected -----------------------------
        //
        // BOTH SIDES ARE PROBED. Week 9's scaffold shipped a suite that
        // tested only one of two symmetric cases (the partner-larger
        // direction) and the gap was real. One `if` with the comparison the
        // wrong way round passes a single-sided probe.
        //
        // THE FIXTURE IS CHOSEN SO THAT NOT THROWING IS VISIBLE. Both
        // triangles give a finite, plausible decomposition here, so an
        // implementation that silently reads one of them returns normally and
        // fails this check -- rather than throwing for some unrelated reason
        // and passing by accident.
        {
            auto rejects = [&](const vc_mat2& m, const std::string& what) {
                bool threw = false;
                try {
                    const vc_eigen_2x2 ignored = eigen_symmetric(m);
                    (void)ignored;
                } catch (const vc::vc_exception& e) {
                    // THE MESSAGE IS CHECKED, NOT ONLY THE CODE, and week 9
                    // is why. invalid_argument is what inverse() and
                    // project() BOTH throw -- and, while S2 was still a stub,
                    // what that threw too -- so a probe that accepts any
                    // invalid_argument passes on a body that failed for a
                    // completely unrelated reason.
                    // vc_gradients.cpp records that trap in those words --
                    // "the right error code and the wrong function's name on
                    // it" -- and it is the defect week 9 actually shipped.
                    //
                    // BE PRECISE ABOUT WHAT THIS PROVES. The match below is
                    // satisfied by the function's own NAME, so it establishes
                    // "eigen_symmetric threw this, not something it called"
                    // -- exactly week 9's trap, and worth closing. It does
                    // NOT establish that asymmetry was the reason. The
                    // sub-tolerance ACCEPTANCE checks below are the other
                    // half: together they say the function rejects this
                    // matrix and accepts a nearly identical symmetric one,
                    // which is the contract. Neither half means much alone.
                    const std::string msg = e.what();
                    threw = e.code() == vc::vc_error_code::invalid_argument &&
                            msg.find("symmetr") != std::string::npos;
                }
                expect(threw, what);
            };

            rejects(vc_mat2{{{{{4.0F, 3.0F}}, {{1.0F, 2.0F}}}}},
                    "asymmetric input is rejected when m[0][1] > m[1][0]");
            rejects(vc_mat2{{{{{4.0F, 1.0F}}, {{3.0F, 2.0F}}}}},
                    "asymmetric input is rejected when m[1][0] > m[0][1] "
                    "(the OTHER side -- a one-sided check misses this)");

            // The converse: a difference small enough to be rounding must be
            // accepted, or a caller computing the same product twice by two
            // roundings would be punished for the library's own arithmetic.
            //
            // RUN AT TWO SCALES, because asymmetry_tolerance is documented as
            // RELATIVE to the matrix's largest entry and an implementation
            // that ignores that passes the order-1 case and fails the other.
            // With b = 100 the jitter below is 100 * 0.5e-6 = 5e-5, which a
            // RELATIVE cutoff accepts comfortably (1e-6 * 10000 = 1e-2) and
            // an ABSOLUTE one of 1e-6 rejects outright. So an absolute check
            // passes the order-1 case and throws away a matrix it should
            // take here.
            {
                auto accepts = [&](float a, float b, float c,
                                   const std::string& what) {
                    // Half the tolerance, relative: unambiguously inside it
                    // on any reading of "largest entry". Deliberately NOT
                    // 0.1 * tolerance, which at b = 2 comes to 2e-7 -- under
                    // one float32 step at that magnitude, so the jitter can
                    // round away entirely and leave a PERFECTLY SYMMETRIC
                    // matrix being checked. That probe would have passed
                    // against an implementation with no tolerance at all.
                    const float jitter = b * (asymmetry_tolerance * 0.5F);
                    const float b_jittered = b + jitter;
                    expect(b_jittered != b,
                           "the sub-tolerance probe's jitter SURVIVES float32 "
                           "rounding -- " +
                               what);
                    bool threw = false;
                    try {
                        const vc_eigen_2x2 r = eigen_symmetric(
                            vc_mat2{{{{{a, b}}, {{b_jittered, c}}}}});
                        (void)r;
                    } catch (const vc::vc_exception&) {
                        threw = true;
                    }
                    expect(!threw, "a sub-tolerance asymmetry is ACCEPTED "
                                   "(rounding is not a caller error) -- " +
                                       what);
                };
                accepts(9.0F, 2.0F, 3.0F, "at order 1");
                accepts(10000.0F, 100.0F, 5000.0F,
                        "at a diagonal of 1e4 (an ABSOLUTE tolerance fails "
                        "here and passes above)");
            }
        }

        // ---- 6. (REMOVED) the learner's hand-computed examples ----------
        //
        // Section B accepts this row on eigenvalues matching HAND COMPUTATION
        // *and* agreeing with Eigen. Group 3's 200 fixed-seed trials do the
        // second half. The first half used to live here as two empty slots
        // that failed loudly until filled.
        //
        // REMOVED 2026-11-21, on the learner's call, and the reasoning is
        // recorded because the criterion is section B's and does not just
        // evaporate:
        //
        //   - The hand computation DID happen, during the week-10 review:
        //     [[3,1],[1,3]], [[9,2],[2,3]], [[1e4,1],[1,1]] and
        //     diag(1e-6, 2e-6) were each worked by hand, and two of them
        //     found real defects that way. See P3 section H.5.10.
        //   - But it was worked by the REVIEWER, not the learner, which is
        //     precisely the independence this group existed to protect --
        //     week 9's `+50` derivation in 17_gradients.cpp was supplied the
        //     same way, and the notes record that mitigation as spent. So
        //     most of what the group was buying had already been spent before
        //     the learner reached it.
        //   - Against group 3's 200 trials against Eigen, two hand values add
        //     almost nothing as an ORACLE.
        //   - What it did still cost was a permanently red ctest, which
        //     teaches you to ignore a red ctest.
        //
        // The honest summary: this was a real criterion, it was met in the
        // wrong place by the wrong person, and leaving a failing stub here
        // would have recorded that less clearly than saying so.

    } catch (const vc::vc_exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << '\n';
        std::cout << "19_eigen2x2: " << passed << " / " << total_checks
                  << " checks passed before the exception\n";
        std::cout << "19_eigen2x2: FAIL\n";
        return 1;
    }

    std::cout << "19_eigen2x2: " << passed << " / " << total_checks
              << " checks passed\n";
    if (passed != total_checks) {
        std::cout << "19_eigen2x2: FAIL\n";
        return 1;
    }
    std::cout << "19_eigen2x2: OK\n";
    return 0;
}
