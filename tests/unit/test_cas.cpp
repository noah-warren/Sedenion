#include <sstream>

#include "cas/expr.h"
#include "cas/poly.h"
#include "framework.h"
#include "interpreter.h"

using namespace cas;

static Poly P(std::vector<int64_t> coeffs) {
    std::vector<Rational> c;
    for (int64_t v : coeffs) c.push_back(Rational(v));
    return Poly(c);
}

TEST(poly_arithmetic) {
    Poly a = P({-1, 0, 1});  // x^2 - 1
    Poly b = P({1, 1});      // x + 1
    Poly q, r;
    Poly::divMod(a, b, q, r);
    CHECK(q == P({-1, 1}));
    CHECK(r.isZero());
    CHECK((a * b).degree() == 3);
    CHECK(Poly::gcd(a, P({1, 2, 1})) == P({1, 1}));
    CHECK(a.derivative() == P({0, 2}));
    CHECK_EQ(a.eval(Rational(3)).toString(), "8");
}

TEST(poly_factor_over_rationals) {
    // 2x^4 - 2 = 2 (x - 1)(x + 1)(x^2 + 1)
    Rational unit;
    std::vector<PolyFactor> fs = factorPoly(P({-2, 0, 0, 0, 2}), unit);
    CHECK_EQ(fs.size(), static_cast<size_t>(3));
    int degreeSum = 0;
    for (const auto& f : fs) degreeSum += f.p.degree() * f.mult;
    CHECK_EQ(degreeSum, 4);
}

TEST(poly_factor_zassenhaus) {
    // (x^6 + 3x^5 - x + 7)(5x^6 - x^4 + 2)(x^6 - 2x^3 - 1): no rational roots, three sextic factors
    Poly a = P({7, -1, 0, 0, 0, 3, 1}), b = P({2, 0, 0, 0, -1, 0, 5}), c = P({-1, 0, 0, -2, 0, 0, 1});
    Rational unit;
    std::vector<PolyFactor> fs = factorPoly(a * b * c, unit);
    CHECK_EQ(fs.size(), static_cast<size_t>(3));
    Poly product = Poly::constant(unit);
    for (const auto& f : fs) {
        CHECK_EQ(f.p.degree(), 6);
        CHECK_EQ(f.mult, 1);
        product = product * f.p;
    }
    CHECK(product == a * b * c);

    // A Swinnerton-Dyer polynomial: irreducible over Q but split modulo every prime
    std::vector<PolyFactor> sd = factorPoly(P({576, 0, -960, 0, 352, 0, -40, 0, 1}), unit);
    CHECK_EQ(sd.size(), static_cast<size_t>(1));
}

TEST(expr_hash_consing_and_canonical_order) {
    E x = sym("x"), y = sym("y");
    CHECK(add(x, y) == add(y, x));
    CHECK(mul(x, x) == pow(x, num(2)));
    CHECK(sub(x, x) == zero());
    CHECK(add(mul(num(2), x), mul(num(3), x)) == mul(num(5), x));
    CHECK_EQ(toString(add(pow(x, num(2)), x)), "x^2 + x");
}

TEST(expr_diff_and_integrate_roundtrip) {
    E x = sym("x");
    E f = mul(pow(x, num(3)), fn("sin", x));
    E F = integrate(diff(f, x), x);
    CHECK(F != nullptr);
    CHECK(isZero(simplify(sub(diff(F, x), diff(f, x)))));
}

TEST(expr_solve_quadratic) {
    E x = sym("x");
    std::vector<E> roots = solve(eq(add({pow(x, num(2)), mul(num(-5), x), num(6)}), zero()), x);
    CHECK_EQ(roots.size(), static_cast<size_t>(2));
    CHECK(roots[0] == num(2));
    CHECK(roots[1] == num(3));
}

static std::string run(const std::string& src) {
    std::ostringstream out, err;
    Interpreter interp;
    interp.setOutput(out);
    interp.setErrorStream(err);
    interp.run(src);
    std::string s = out.str() + err.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

TEST(interpreter_end_to_end) {
    CHECK_EQ(run("1 + 1"), "2");
    CHECK_EQ(run("sym x ; factor(x^2 - 1)"), "(x - 1)*(x + 1)");
    CHECK_EQ(run("det([1, 2; 3, 4])"), "-2");
    CHECK_EQ(run("1[km] to [m]"), "1000 m");
    CHECK(run("1 +").find("syntax error") != std::string::npos);
}
