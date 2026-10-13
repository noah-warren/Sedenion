#pragma once

#include <vector>

#include "cas/expr.h"

namespace cas {

// Dense univariate polynomial with exact rational coefficients; c[k] multiplies x^k.
struct Poly {
    std::vector<Rational> c;

    Poly() = default;
    explicit Poly(std::vector<Rational> coeffs) : c(std::move(coeffs)) { trim(); }
    static Poly constant(const Rational& r) { return Poly(std::vector<Rational>{r}); }
    static Poly monomial(const Rational& r, size_t k);

    void trim();
    int degree() const { return static_cast<int>(c.size()) - 1; }  // -1 for the zero polynomial
    bool isZero() const { return c.empty(); }
    const Rational& lead() const { return c.back(); }
    Rational eval(const Rational& x) const;
    Poly derivative() const;
    Poly monic() const;
    // Content (gcd of numerators / lcm of denominators, sign of the leading coefficient) and the
    // primitive integer polynomial with p = content * primitive.
    Rational content() const;

    friend Poly operator+(const Poly& a, const Poly& b);
    friend Poly operator-(const Poly& a, const Poly& b);
    friend Poly operator*(const Poly& a, const Poly& b);
    friend Poly operator*(const Poly& a, const Rational& r);
    friend bool operator==(const Poly& a, const Poly& b) { return a.c == b.c; }
    static void divMod(const Poly& a, const Poly& b, Poly& q, Poly& r);
    static Poly gcd(Poly a, Poly b);  // monic
};

// Converts e (expanded) to a polynomial in x with rational coefficients; false when it is not one.
bool toPoly(const E& e, const E& x, Poly& out);
E fromPoly(const Poly& p, const E& x);

// Polynomial in x whose coefficients are expressions free of x.
bool toPolyE(const E& e, const E& x, std::vector<E>& coeffs);
E fromPolyE(const std::vector<E>& coeffs, const E& x);

// Irreducible factors over Q of a square-free-decomposed primitive polynomial, with multiplicities.
struct PolyFactor {
    Poly p;
    int mult;
};
std::vector<PolyFactor> factorPoly(const Poly& p, Rational& unit);

// Durand-Kerner: all complex roots of a polynomial given by real or complex coefficients.
std::vector<std::complex<double>> numericRoots(const std::vector<std::complex<double>>& coeffs);

}  // namespace cas
