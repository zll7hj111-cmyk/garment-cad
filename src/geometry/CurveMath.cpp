#include "CurveMath.h"

#include <QPainterPath>

#include <algorithm>
#include <array>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <cmath>

namespace cad::geo {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// 5-point Gauss-Legendre nodes and weights on [-1, 1].
constexpr int GL_N = 5;
constexpr std::array<double, GL_N> GL_NODES = {
    -0.9061798459386640,
    -0.5384693101056831,
     0.0,
     0.5384693101056831,
     0.9061798459386640
};
constexpr std::array<double, GL_N> GL_WEIGHTS = {
    0.2369268850561891,
    0.4786286704993665,
    0.5688888888888889,
    0.4786286704993665,
    0.2369268850561891
};

/// Centripetal knot interval: |P1 - P0|^0.5 (avoids cusps for uneven spacing).
inline double knotInterval(const Vec2& a, const Vec2& b)
{
    double d = a.distanceTo(b);
    return std::sqrt(std::max(d, 1e-9));
}

/// Solve a cubic equation: t³ + a*t² + b*t + c = 0.
/// Returns all real roots in [0, 1].
std::vector<double> solveCubicInUnit(double a, double b, double c)
{
    // Depress: t = u - a/3  →  u³ + p*u + q = 0
    double p = b - a * a / 3.0;
    double q = 2.0 * a * a * a / 27.0 - a * b / 3.0 + c;

    double disc = q * q / 4.0 + p * p * p / 27.0;
    std::vector<double> roots;

    auto addIfInUnit = [&](double t) {
        if (t >= -1e-9 && t <= 1.0 + 1e-9)
            roots.push_back(std::clamp(t, 0.0, 1.0));
    };

    if (disc > 1e-12) {
        // One real root
        double sq = std::sqrt(disc);
        double u = std::cbrt(-q / 2.0 + sq) + std::cbrt(-q / 2.0 - sq);
        addIfInUnit(u - a / 3.0);
    } else if (disc < -1e-12) {
        // Three real roots (casus irreducibilis)
        double r = std::sqrt(-p * p * p / 27.0);
        double theta = std::acos(std::clamp(-q / (2.0 * r), -1.0, 1.0));
        double m = 2.0 * std::cbrt(r);
        addIfInUnit(m * std::cos(theta / 3.0) - a / 3.0);
        addIfInUnit(m * std::cos((theta + 2.0 * M_PI) / 3.0) - a / 3.0);
        addIfInUnit(m * std::cos((theta + 4.0 * M_PI) / 3.0) - a / 3.0);
    } else {
        // Repeated root
        if (std::abs(p) < 1e-12) {
            addIfInUnit(-a / 3.0);
        } else {
            double u1 = 3.0 * q / p;
            double u2 = -u1 / 2.0;
            addIfInUnit(u1 - a / 3.0);
            addIfInUnit(u2 - a / 3.0);
        }
    }
    return roots;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

Vec2 catmullRomTangent(const std::vector<Vec2>& points, int index, double tension)
{
    const int n = static_cast<int>(points.size());
    if (n < 2) return Vec2::zero();

    // Clamp index
    index = std::clamp(index, 0, n - 1);

    // Determine neighbors (phantom points for endpoints: reflect)
    Vec2 prev = (index > 0) ? points[index - 1]
                            : points[0] * 2.0 - points[1];
    Vec2 next = (index < n - 1) ? points[index + 1]
                                : points[n - 1] * 2.0 - points[n - 2];

    // Centripetal parameterization
    double t0 = 0.0;
    double t1 = t0 + knotInterval(prev, points[index]);
    double t2 = t1 + knotInterval(points[index], next);

    if (t2 - t0 < 1e-9) return Vec2::zero();

    // Barry-Goldman tangent at P_index
    Vec2 tangent = (next - prev) / (t2 - t0) * (t1 - t0);

    // Apply tension: scale factor = (1 - tension) clamped to [0.1, 3]
    double scale = std::clamp(1.0 - tension, 0.1, 3.0);
    return tangent * scale;
}

namespace {

/// Solve a tridiagonal linear system  a[k]*x[k-1] + b[k]*x[k] + c[k]*x[k+1] = d[k]
/// (k = 0..m-1; a[0] and c[m-1] are ignored) via the Thomas algorithm. The
/// unknowns are vectors solved component-wise with shared scalar coefficients.
/// The system is strictly diagonally dominant here, so no pivoting is needed.
std::vector<Vec2> solveTridiagonal(std::vector<double> a, std::vector<double> b,
                                   std::vector<double> c, std::vector<Vec2> d)
{
    const int m = static_cast<int>(b.size());
    if (m == 0) return {};
    for (int k = 1; k < m; ++k) {           // forward elimination
        const double w = a[k] / b[k - 1];
        b[k] -= w * c[k - 1];
        d[k] = d[k] - d[k - 1] * w;
    }
    std::vector<Vec2> x(m);                 // back substitution
    x[m - 1] = d[m - 1] / b[m - 1];
    for (int k = m - 2; k >= 0; --k)
        x[k] = (d[k] - c[k] * x[k + 1]) / b[k];
    return x;
}

/// Solve a SCALAR tridiagonal system (used by the Hobby angle solve).
std::vector<double> solveTridiagonalScalar(std::vector<double> a, std::vector<double> b,
                                           std::vector<double> c, std::vector<double> d)
{
    const int m = static_cast<int>(b.size());
    if (m == 0) return {};
    for (int k = 1; k < m; ++k) {
        const double w = a[k] / b[k - 1];
        b[k] -= w * c[k - 1];
        d[k] -= d[k - 1] * w;
    }
    std::vector<double> x(m);
    x[m - 1] = d[m - 1] / b[m - 1];
    for (int k = m - 2; k >= 0; --k)
        x[k] = (d[k] - c[k] * x[k + 1]) / b[k];
    return x;
}

/// Normalize an angle to (-π, π].
double normAnglePi(double a)
{
    while (a > M_PI) a -= 2.0 * M_PI;
    while (a <= -M_PI) a += 2.0 * M_PI;
    return a;
}

/// Hobby velocity: the handle-length multiplier for a chord of unit length,
/// given the tangent's turn from the chord at the START (theta) and the turn
/// at the END (phi). Classic formula from Hobby 1986, matching Seamly2D's
/// HobbyHandleLengths (vabstractcubicbezier.cpp).
double hobbyVelocity(double theta, double phi)
{
    const double sq2   = std::sqrt(2.0);
    const double sqrt5 = std::sqrt(5.0);
    const double cA    = 0.5 * (sqrt5 - 1.0);
    const double cB    = 0.5 * (3.0 - sqrt5);

    const double num = 2.0 + sq2 * (std::sin(theta) - std::sin(phi) / 16.0)
                                * (std::sin(phi) - std::sin(theta) / 16.0)
                                * (std::cos(theta) - std::cos(phi));
    const double den = 3.0 * (1.0 + cA * std::cos(theta) + cB * std::cos(phi));
    return (std::abs(den) > 1e-9) ? (num / den) : (1.0 / 3.0);
}

} // namespace

std::vector<Vec2> solveC2Tangents(const std::vector<Vec2>& points,
                                  const std::vector<bool>& isAuto,
                                  const std::vector<Vec2>& manualTanIn,
                                  const std::vector<Vec2>& manualTanOut)
{
    const int n = static_cast<int>(points.size());
    std::vector<Vec2> result(n, Vec2::zero());
    if (n < 2) return result;

    auto autoAt = [&](int i) -> bool {
        return (i >= 0 && i < static_cast<int>(isAuto.size())) ? isAuto[i] : true;
    };
    auto tanInAt = [&](int i) -> Vec2 {
        return (i >= 0 && i < static_cast<int>(manualTanIn.size())) ? manualTanIn[i] : Vec2::zero();
    };
    auto tanOutAt = [&](int i) -> Vec2 {
        return (i >= 0 && i < static_cast<int>(manualTanOut.size())) ? manualTanOut[i] : Vec2::zero();
    };

    // Chord lengths h[i] = |P[i+1] - P[i]|.
    std::vector<double> h(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        h[i] = points[i].distanceTo(points[i + 1]);
        if (h[i] < 1e-9) h[i] = 1e-9;
    }

    // Solve each maximal contiguous run of AUTO points as a tridiagonal system.
    // The unknowns are the Bézier-form tangents T[p] (the same representation
    // stored on manual points and consumed by buildBezierSpans: ctrl = P ± T/3).
    // Interior C2 equation (curvature continuity across point p):
    //   T[p-1]/h²[p-1] + 2(1/h²[p-1] + 1/h²[p]) T[p] + T[p+1]/h²[p]
    //       = 3(P[p+1]-P[p])/h²[p] + 3(P[p]-P[p-1])/h²[p-1]
    // Natural boundary (zero curvature) at an auto curve endpoint:
    //   start: 2 T[0] + T[1] = 3 (P[1]-P[0]);   end: T[n-2] + 2 T[n-1] = 3 (P[n-1]-P[n-2])
    // A manual neighbour contributes its stored tangent as a known value (moved
    // to the RHS), so user-set handles act as fixed constraints.
    int i = 0;
    while (i < n) {
        if (!autoAt(i)) { ++i; continue; }
        const int L = i;
        while (i < n && autoAt(i)) ++i;
        const int R = i - 1;                 // inclusive end of the run
        const int m = R - L + 1;

        std::vector<double> a(m, 0.0), b(m, 0.0), c(m, 0.0);
        std::vector<Vec2> d(m, Vec2::zero());

        for (int k = 0; k < m; ++k) {
            const int p = L + k;
            if (p == 0) {                    // natural boundary at curve start
                b[k] = 2.0;
                d[k] = (points[1] - points[0]) * 3.0;
                if (R >= 1) {
                    c[k] = 1.0;              // T[1] is an unknown in this run
                } else {
                    d[k] = d[k] - tanInAt(1);   // point 1 manual → known in-tangent
                }
            } else if (p == n - 1) {         // natural boundary at curve end
                b[k] = 2.0;
                d[k] = (points[n - 1] - points[n - 2]) * 3.0;
                if (L <= n - 2) {
                    a[k] = 1.0;              // T[n-2] is an unknown in this run
                } else {
                    d[k] = d[k] - tanOutAt(n - 2); // point n-2 manual → known out-tangent
                }
            } else {                         // interior C2 equation
                const double hp = h[p];      // |P[p+1] - P[p]|
                const double hm = h[p - 1];  // |P[p] - P[p-1]|
                const double ip = 1.0 / (hp * hp);
                const double im = 1.0 / (hm * hm);
                a[k] = im;
                b[k] = 2.0 * (im + ip);
                c[k] = ip;
                d[k] = (points[p + 1] - points[p]) * (3.0 * ip)
                     + (points[p] - points[p - 1]) * (3.0 * im);
                if (p == L && !autoAt(p - 1)) {   // left neighbour manual
                    d[k] = d[k] - tanOutAt(p - 1) * a[k];
                    a[k] = 0.0;
                }
                if (p == R && !autoAt(p + 1)) {   // right neighbour manual
                    d[k] = d[k] - tanInAt(p + 1) * c[k];
                    c[k] = 0.0;
                }
            }
        }

        const std::vector<Vec2> sol = solveTridiagonal(a, b, c, d);
        for (int k = 0; k < m; ++k)
            result[L + k] = sol[k];
    }
    return result;
}

std::pair<std::vector<Vec2>, std::vector<Vec2>> solveHobbyTangents(
    const std::vector<Vec2>& points,
    const std::vector<bool>& isAuto,
    const std::vector<Vec2>& manualTanIn,
    const std::vector<Vec2>& manualTanOut,
    double tension)
{
    const int n = static_cast<int>(points.size());
    std::vector<Vec2> tIn(n), tOut(n);
    if (n < 2) return {tIn, tOut};

    // Chord geometry (local).
    std::vector<double> chordAng(n - 1), chordLen(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        const Vec2 c = points[i + 1] - points[i];
        chordLen[i] = std::max(c.length(), 1e-9);
        chordAng[i] = std::atan2(c.y, c.x);
    }

    const auto autoAt = [&](int k) -> bool {
        return (k >= 0 && k < n) ? isAuto[k] : true;
    };
    // Reference angle of point p: its outgoing chord (last point: incoming).
    const auto refAng = [&](int p) -> double {
        return (p < n - 1) ? chordAng[p] : chordAng[n - 2];
    };

    // ── 1) Tangent direction θ per point ──
    // Solve the Hobby angle system (Hobby 1986, eq. 4, chord-length weighted):
    //   h_i·θ_{i-1} + 2(h_{i-1}+h_i)·θ_i + h_{i-1}·θ_{i+1}
    //       = 3·h_i·ψ_{i-1} + 3·h_{i-1}·ψ_i
    // in terms of small deviations δ_i = θ_i − refAngle(i), so the system is
    // numerically clean (no angle wrap). Auto curve endpoints follow their
    // adjacent chord (δ = 0): the curve leaves the endpoint along the chord,
    // the natural garment-CAD behaviour — no forced zero curvature.
    std::vector<double> theta(n, 0.0);
    int i = 0;
    while (i < n) {
        if (!autoAt(i)) {
            theta[i] = std::atan2(manualTanOut[i].y, manualTanOut[i].x);
            ++i;
            continue;
        }
        const int L = i;
        while (i < n && autoAt(i)) ++i;
        const int R = i - 1;
        const int m = R - L + 1;

        if (m == 1) {
            // Isolated auto point between manual neighbours: bisect the two
            // neighbour angles (or follow the single adjacent chord at an end).
            const double a = (L > 0) ? theta[L - 1] : chordAng[0];
            const double b = (R < n - 1) ? theta[R + 1] : chordAng[n - 2];
            theta[L] = a + normAnglePi(b - a) * 0.5;
            continue;
        }

        std::vector<double> a(m, 0.0), b(m, 0.0), c(m, 0.0), rhs(m, 0.0);
        for (int k = 0; k < m; ++k) {
            const int p = L + k;
            if (p == 0 || p == n - 1) {
                b[k] = 1.0;      // δ = 0 at auto curve endpoints
                rhs[k] = 0.0;
                continue;
            }
            const double hp = chordLen[p];      // outgoing chord
            const double hm = chordLen[p - 1];  // incoming chord
            const double psiP = chordAng[p];    // outgoing chord angle
            const double psiM = chordAng[p - 1];// incoming chord angle

            b[k] = 2.0 * (hm + hp);
            // RHS = 3·hp·ψ_{p-1} + 3·hm·ψ_p − hp·ref(p−1) − 2(hm+hp)·ref(p) − hm·ref(p+1)
            rhs[k] = 3.0 * hp * psiM + 3.0 * hm * psiP
                   - hp * refAng(p - 1) - 2.0 * (hm + hp) * refAng(p)
                   - hm * refAng(p + 1);

            if (k == 0) {
                // Left neighbour is outside the run: its θ is KNOWN.
                rhs[k] -= hp * theta[L - 1];
            } else {
                a[k] = hp;
            }
            if (k == m - 1) {
                rhs[k] -= hm * theta[R + 1];
            } else {
                c[k] = hm;
            }
        }

        const std::vector<double> delta = solveTridiagonalScalar(a, b, c, rhs);
        for (int k = 0; k < m; ++k)
            theta[L + k] = refAng(L + k) + delta[k];
    }

    // ── 2) Handle LENGTHS from the Hobby velocity formula ──
    // For each chord: c1 = velocity(θ_start − ψ, θ_end − (ψ+π)) · h, and the
    // same velocity evaluated with swapped arguments for c2. Manual points
    // keep their stored tangent length (direction still θ — which for a
    // manual point IS its stored direction, so manual tangents are exact).
    // tension==0 (the Segment default) means classic Hobby (tau = 1).
    const double tau = (tension > 1e-6) ? tension : 1.0;
    for (int k = 0; k < n - 1; ++k) {
        const double psi = chordAng[k];
        const double tStart = normAnglePi(theta[k] - psi);
        const double tEnd   = normAnglePi(theta[k + 1] - (psi + M_PI));

        const double cOut = autoAt(k)
            ? hobbyVelocity(tStart, tEnd) * chordLen[k] / tau
            : manualTanOut[k].length();
        const double cIn = autoAt(k + 1)
            ? hobbyVelocity(tEnd, tStart) * chordLen[k] / tau
            : manualTanIn[k + 1].length();

        tOut[k]     = Vec2(std::cos(theta[k]),     std::sin(theta[k]))     * cOut;
        tIn[k + 1]  = Vec2(std::cos(theta[k + 1]), std::sin(theta[k + 1])) * cIn;
    }

    // Auto endpoints mirror their single adjacent handle (start: out only;
    // end: in only), matching the C2 solver's semantics.
    if (autoAt(0))     tIn[0] = tOut[0];
    if (autoAt(n - 1)) tOut[n - 1] = tIn[n - 1];

    // Manual points: stored tangents verbatim.
    for (int k = 0; k < n; ++k) {
        if (!autoAt(k)) {
            tIn[k] = manualTanIn[k];
            tOut[k] = manualTanOut[k];
        }
    }
    return {tIn, tOut};
}

std::vector<BezierSpan> buildBezierSpans(
    const std::vector<Vec2>& points,
    const std::vector<Vec2>& tangentIn,
    const std::vector<Vec2>& tangentOut,
    const std::vector<bool>& autoTangent,
    double tension,
    AutoCurveMode mode)
{
    const int n = static_cast<int>(points.size());
    if (n < 2) return {};

    std::vector<BezierSpan> spans;
    spans.reserve(n - 1);

    // Compute the automatic tangents for the AUTO points — either the C2
    // curvature-continuous global solve or the Hobby angle+velocity solve
    // (Seamly2D). MANUAL points keep their stored tangents and act as fixed
    // constraints. (tension is not applied to the C2 solution: scaling the
    // tangents would break curvature continuity, so C2 curves use their
    // natural tightness.)
    std::vector<Vec2> c2In, c2Out;
    if (mode == AutoCurveMode::Hobby) {
        std::tie(c2In, c2Out) = solveHobbyTangents(
            points, autoTangent, tangentIn, tangentOut, tension);
    } else {
        const std::vector<Vec2> c2 =
            solveC2Tangents(points, autoTangent, tangentIn, tangentOut);
        c2In = c2;
        c2Out = c2;
    }

    // Pre-compute effective tangents for each point
    std::vector<Vec2> effOut(n), effIn(n);
    for (int i = 0; i < n; ++i) {
        bool isAuto = (i < static_cast<int>(autoTangent.size())) ? autoTangent[i] : true;
        if (isAuto) {
            effIn[i] = (i < static_cast<int>(c2In.size())) ? c2In[i] : Vec2::zero();
            effOut[i] = (i < static_cast<int>(c2Out.size())) ? c2Out[i] : Vec2::zero();
        } else {
            effOut[i] = (i < static_cast<int>(tangentOut.size())) ? tangentOut[i] : Vec2::zero();
            effIn[i] = (i < static_cast<int>(tangentIn.size())) ? tangentIn[i] : Vec2::zero();
        }
    }

    for (int i = 0; i < n - 1; ++i) {
        BezierSpan span;
        span.p0 = points[i];
        span.p3 = points[i + 1];
        // Hermite-to-Bézier: ctrl1 = P0 + tangentOut/3, ctrl2 = P1 - tangentIn/3
        span.ctrl1 = span.p0 + effOut[i] / 3.0;
        span.ctrl2 = span.p3 - effIn[i + 1] / 3.0;
        spans.push_back(span);
    }
    return spans;
}

std::vector<BezierSpan> buildCatmullRomSpans(const std::vector<Vec2>& points,
                                             double tension)
{
    std::vector<Vec2> tIn(points.size(), Vec2::zero()), tOut(points.size(), Vec2::zero());
    std::vector<bool> autoTan(points.size(), true);
    // Explicitly C2: this legacy entry point keeps its historical shape.
    return buildBezierSpans(points, tIn, tOut, autoTan, tension, AutoCurveMode::C2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Evaluation
// ─────────────────────────────────────────────────────────────────────────────

Vec2 evalBezier(const BezierSpan& s, double t)
{
    double u = 1.0 - t;
    double u2 = u * u, u3 = u2 * u;
    double t2 = t * t, t3 = t2 * t;
    return s.p0 * u3 + s.ctrl1 * (3.0 * u2 * t) + s.ctrl2 * (3.0 * u * t2) + s.p3 * t3;
}

Vec2 evalBezierDerivative(const BezierSpan& s, double t)
{
    double u = 1.0 - t;
    // B'(t) = 3(1-t)²(C1-P0) + 6(1-t)t(C2-C1) + 3t²(P3-C2)
    Vec2 d0 = s.ctrl1 - s.p0;
    Vec2 d1 = s.ctrl2 - s.ctrl1;
    Vec2 d2 = s.p3 - s.ctrl2;
    return d0 * (3.0 * u * u) + d1 * (6.0 * u * t) + d2 * (3.0 * t * t);
}

std::pair<BezierSpan, BezierSpan> subdivideBezier(const BezierSpan& s, double t)
{
    // de Casteljau subdivision at parameter t.
    // Level 1 midpoints
    Vec2 p01   = s.p0   + (s.ctrl1 - s.p0)   * t;
    Vec2 p12   = s.ctrl1 + (s.ctrl2 - s.ctrl1) * t;
    Vec2 p23   = s.ctrl2 + (s.p3   - s.ctrl2) * t;
    // Level 2 midpoints
    Vec2 p012  = p01 + (p12 - p01) * t;
    Vec2 p123  = p12 + (p23 - p12) * t;
    // Level 3: the point on the curve at t
    Vec2 p0123 = p012 + (p123 - p012) * t;

    BezierSpan left;
    left.p0    = s.p0;
    left.ctrl1 = p01;
    left.ctrl2 = p012;
    left.p3    = p0123;

    BezierSpan right;
    right.p0    = p0123;
    right.ctrl1 = p123;
    right.ctrl2 = p23;
    right.p3    = s.p3;

    return {left, right};
}

Vec2 evalCurve(const std::vector<BezierSpan>& spans, double T)
{
    if (spans.empty()) return Vec2::zero();
    int n = static_cast<int>(spans.size());
    T = std::clamp(T, 0.0, static_cast<double>(n));
    int idx = static_cast<int>(T);
    if (idx >= n) idx = n - 1;
    double local = T - idx;
    return evalBezier(spans[idx], local);
}

Vec2 evalCurveTangent(const std::vector<BezierSpan>& spans, double T)
{
    if (spans.empty()) return Vec2::unitX();
    int n = static_cast<int>(spans.size());
    T = std::clamp(T, 0.0, static_cast<double>(n));
    int idx = static_cast<int>(T);
    if (idx >= n) idx = n - 1;
    double local = T - idx;
    return evalBezierDerivative(spans[idx], local).normalized();
}

Vec2 evalCurveDerivative(const std::vector<BezierSpan>& spans, double T)
{
    if (spans.empty()) return Vec2::zero();
    int n = static_cast<int>(spans.size());
    T = std::clamp(T, 0.0, static_cast<double>(n));
    int idx = static_cast<int>(T);
    if (idx >= n) idx = n - 1;
    double local = T - idx;
    return evalBezierDerivative(spans[idx], local);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arc length
// ─────────────────────────────────────────────────────────────────────────────

double spanArcLength(const BezierSpan& span)
{
    // Gauss-Legendre on [0, 1]: transform nodes from [-1,1] to [0,1]
    double sum = 0.0;
    for (int i = 0; i < GL_N; ++i) {
        double t = 0.5 * (GL_NODES[i] + 1.0);
        double w = 0.5 * GL_WEIGHTS[i];
        Vec2 deriv = evalBezierDerivative(span, t);
        sum += w * deriv.length();
    }
    return sum;
}

double totalArcLength(const std::vector<BezierSpan>& spans)
{
    double total = 0.0;
    for (const auto& s : spans)
        total += spanArcLength(s);
    return total;
}

double arcLengthToParam(const std::vector<BezierSpan>& spans, double targetS)
{
    if (spans.empty()) return 0.0;

    const int n = static_cast<int>(spans.size());
    if (targetS <= 0.0) return 0.0;

    // Pre-compute cumulative span lengths
    std::vector<double> cumLen(n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        cumLen[i + 1] = cumLen[i] + spanArcLength(spans[i]);

    double totalLen = cumLen[n];
    if (targetS >= totalLen) return static_cast<double>(n);

    // Find which span contains the target arc length
    int spanIdx = static_cast<int>(
        std::upper_bound(cumLen.begin(), cumLen.end(), targetS) - cumLen.begin()) - 1;
    spanIdx = std::clamp(spanIdx, 0, n - 1);

    double localTarget = targetS - cumLen[spanIdx];
    double spanLen = cumLen[spanIdx + 1] - cumLen[spanIdx];
    if (spanLen < 1e-9) return static_cast<double>(spanIdx);

    // Bisection within the span to find local t
    const BezierSpan& sp = spans[spanIdx];
    double lo = 0.0, hi = 1.0;
    for (int iter = 0; iter < 40; ++iter) {
        double mid = (lo + hi) * 0.5;
        // Compute arc length from 0 to mid using GL on [0, mid]
        double len = 0.0;
        for (int i = 0; i < GL_N; ++i) {
            double t = mid * 0.5 * (GL_NODES[i] + 1.0);
            double w = mid * 0.5 * GL_WEIGHTS[i];
            len += w * evalBezierDerivative(sp, t).length();
        }
        if (len < localTarget)
            lo = mid;
        else
            hi = mid;
    }
    return spanIdx + (lo + hi) * 0.5;
}

// ─────────────────────────────────────────────────────────────────────────────
// Projection
// ─────────────────────────────────────────────────────────────────────────────

CurveProjection projectPointOnCurve(const Vec2& query,
                                    const std::vector<BezierSpan>& spans)
{
    CurveProjection best;
    if (spans.empty()) return best;

    const int n = static_cast<int>(spans.size());
    double bestDistSq = 1e30;

    // Cumulative arc lengths for computing s
    std::vector<double> cumLen(n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        cumLen[i + 1] = cumLen[i] + spanArcLength(spans[i]);

    for (int i = 0; i < n; ++i) {
        const BezierSpan& sp = spans[i];

        // Coarse sampling (8 subdivisions) to find initial guess
        double bestLocalT = 0.0;
        double bestSpanDistSq = 1e30;
        constexpr int SAMPLES = 8;
        for (int k = 0; k <= SAMPLES; ++k) {
            double t = static_cast<double>(k) / SAMPLES;
            Vec2 pt = evalBezier(sp, t);
            double dSq = query.distanceSquaredTo(pt);
            if (dSq < bestSpanDistSq) {
                bestSpanDistSq = dSq;
                bestLocalT = t;
            }
        }

        // Newton refinement: minimize |B(t) - Q|²
        // f(t) = dot(B(t) - Q, B'(t)) = 0
        for (int iter = 0; iter < 8; ++iter) {
            Vec2 pt = evalBezier(sp, bestLocalT);
            Vec2 deriv = evalBezierDerivative(sp, bestLocalT);
            double f = (pt - query).dot(deriv);

            // f'(t) = |B'(t)|² + dot(B(t)-Q, B''(t))
            // Approximate B''(t) numerically
            double eps = 1e-6;
            Vec2 derivPlus = evalBezierDerivative(sp, std::min(bestLocalT + eps, 1.0));
            Vec2 derivMinus = evalBezierDerivative(sp, std::max(bestLocalT - eps, 0.0));
            Vec2 secondDeriv = (derivPlus - derivMinus) / (2.0 * eps);
            double fPrime = deriv.lengthSquared() + (pt - query).dot(secondDeriv);

            if (std::abs(fPrime) < 1e-12) break;
            double dt = -f / fPrime;
            bestLocalT = std::clamp(bestLocalT + dt, 0.0, 1.0);
            if (std::abs(dt) < 1e-10) break;
        }

        double dSq = query.distanceSquaredTo(evalBezier(sp, bestLocalT));
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            double globalT = i + bestLocalT;
            Vec2 pt = evalBezier(sp, bestLocalT);
            Vec2 tan = evalBezierDerivative(sp, bestLocalT).normalized();

            best.t = globalT;
            best.point = pt;
            best.tangent = tan;
            best.normal = tan.perpendicular();
            best.distance = std::sqrt(dSq);
            best.valid = true;

            // Compute arc-length s
            double localS = 0.0;
            for (int k = 0; k < GL_N; ++k) {
                double tt = bestLocalT * 0.5 * (GL_NODES[k] + 1.0);
                double w = bestLocalT * 0.5 * GL_WEIGHTS[k];
                localS += w * evalBezierDerivative(sp, tt).length();
            }
            best.s = cumLen[i] + localS;
        }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray-Curve Intersection
// ─────────────────────────────────────────────────────────────────────────────

std::vector<CurveHit> rayCurveIntersect(const Vec2& origin, const Vec2& dir,
                                        const std::vector<BezierSpan>& spans,
                                        bool bidirectional)
{
    std::vector<CurveHit> hits;
    if (spans.empty()) return hits;

    Vec2 n = dir.perpendicular().normalized();  // Ray normal
    const int spanCount = static_cast<int>(spans.size());

    for (int i = 0; i < spanCount; ++i) {
        const BezierSpan& sp = spans[i];

        // Signed distance from ray line for each control point
        double d0 = (sp.p0 - origin).dot(n);
        double d1 = (sp.ctrl1 - origin).dot(n);
        double d2 = (sp.ctrl2 - origin).dot(n);
        double d3 = (sp.p3 - origin).dot(n);

        // Bézier in Bernstein basis: B(t) = (1-t)³d0 + 3(1-t)²t·d1 + 3(1-t)t²·d2 + t³·d3
        // Convert to power basis: at³ + bt² + ct + d = 0
        double a = -d0 + 3.0 * d1 - 3.0 * d2 + d3;
        double b = 3.0 * d0 - 6.0 * d1 + 3.0 * d2;
        double c = -3.0 * d0 + 3.0 * d1;
        double d = d0;

        // Normalize: t³ + (b/a)t² + (c/a)t + (d/a) = 0
        std::vector<double> roots;
        if (std::abs(a) < 1e-12) {
            // Degenerate to quadratic
            if (std::abs(b) < 1e-12) {
                // Linear
                if (std::abs(c) > 1e-12) {
                    double t = -d / c;
                    if (t >= -1e-9 && t <= 1.0 + 1e-9)
                        roots.push_back(std::clamp(t, 0.0, 1.0));
                }
            } else {
                double disc = c * c - 4.0 * b * d;
                if (disc >= 0) {
                    double sq = std::sqrt(disc);
                    double t1 = (-c + sq) / (2.0 * b);
                    double t2 = (-c - sq) / (2.0 * b);
                    if (t1 >= -1e-9 && t1 <= 1.0 + 1e-9) roots.push_back(std::clamp(t1, 0.0, 1.0));
                    if (t2 >= -1e-9 && t2 <= 1.0 + 1e-9) roots.push_back(std::clamp(t2, 0.0, 1.0));
                }
            }
        } else {
            roots = solveCubicInUnit(b / a, c / a, d / a);
        }

        // Validate: point must be on the ray (s >= 0)
        for (double t : roots) {
            Vec2 pt = evalBezier(sp, t);
            double s = (pt - origin).dot(dir.normalized());
            if (bidirectional || s >= -1e-6) {
                hits.push_back({i + t, pt});
            }
        }
    }

    // Sort by global parameter
    std::sort(hits.begin(), hits.end(),
              [](const CurveHit& a, const CurveHit& b) { return a.t < b.t; });

    // Deduplicate (same hit from adjacent spans at boundary)
    std::vector<CurveHit> unique;
    for (const auto& h : hits) {
        if (unique.empty() || std::abs(h.t - unique.back().t) > 1e-6)
            unique.push_back(h);
    }
    return unique;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

QPainterPath buildPainterPath(const std::vector<BezierSpan>& spans)
{
    QPainterPath path;
    if (spans.empty()) return path;

    path.moveTo(spans[0].p0.x, spans[0].p0.y);
    for (const auto& s : spans) {
        path.cubicTo(s.ctrl1.x, s.ctrl1.y,
                     s.ctrl2.x, s.ctrl2.y,
                     s.p3.x, s.p3.y);
    }
    return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// Flattening (Seamly2D-style polyline rendering)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Recursively subdivide one Bézier span until the control points deviate
/// from the chord by less than @p tol; append the end point (start is already
/// in the output). Depth-limited so degenerate inputs terminate.
void flattenSpan(const BezierSpan& s, double tol, int depth, std::vector<Vec2>& out)
{
    constexpr int kMaxDepth = 24;

    // Deviation of the control points from the straight chord.
    double maxDev = 0.0;
    const Vec2 chord = s.p3 - s.p0;
    const double len2 = chord.lengthSquared();
    if (len2 > 1e-12) {
        auto dev = [&](const Vec2& p) {
            const double t = std::clamp((p - s.p0).dot(chord) / len2, 0.0, 1.0);
            return (p - (s.p0 + chord * t)).length();
        };
        maxDev = std::max(dev(s.ctrl1), dev(s.ctrl2));
    } else {
        // Degenerate span: measure against the control points directly.
        maxDev = std::max((s.ctrl1 - s.p0).length(), (s.ctrl2 - s.p3).length());
    }

    if (maxDev <= tol || depth >= kMaxDepth) {
        out.push_back(s.p3);
        return;
    }

    const auto [l, r] = subdivideBezier(s, 0.5);
    flattenSpan(l, tol, depth + 1, out);
    flattenSpan(r, tol, depth + 1, out);
}

} // namespace

std::vector<Vec2> flattenBezierSpans(const std::vector<BezierSpan>& spans,
                                     double tolerance)
{
    std::vector<Vec2> pts;
    if (spans.empty()) return pts;

    const double tol = std::max(tolerance, 1e-6);
    pts.reserve(spans.size() * 16 + 4);
    pts.push_back(spans[0].p0);
    for (const auto& s : spans)
        flattenSpan(s, tol, 0, pts);
    return pts;
}

} // namespace cad::geo
