#include "cas/poly.h"

#include <algorithm>
#include <cmath>

namespace cas {

Poly Poly::monomial(const Rational& r, size_t k) {
    std::vector<Rational> c(k + 1, Rational(0));
    c[k] = r;
    return Poly(std::move(c));
}

void Poly::trim() {
    while (!c.empty() && c.back().isZero()) c.pop_back();
}

Rational Poly::eval(const Rational& x) const {
    Rational r(0);
    for (size_t k = c.size(); k-- > 0;) r = r * x + c[k];
    return r;
}

Poly Poly::derivative() const {
    std::vector<Rational> d;
    for (size_t k = 1; k < c.size(); k++) d.push_back(c[k] * Rational(static_cast<int64_t>(k)));
    return Poly(std::move(d));
}

Poly Poly::monic() const {
    if (isZero()) return *this;
    Rational l = lead();
    std::vector<Rational> d;
    for (const auto& x : c) d.push_back(x / l);
    return Poly(std::move(d));
}

Rational Poly::content() const {
    if (isZero()) return Rational(1);
    BigInt g(0), l(1);
    for (const auto& x : c) {
        g = BigInt::gcd(g, x.num);
        l = l / BigInt::gcd(l, x.den) * x.den;
    }
    Rational r(g, l);
    return lead().sign() < 0 ? -r : r;
}

Poly operator+(const Poly& a, const Poly& b) {
    std::vector<Rational> c(std::max(a.c.size(), b.c.size()), Rational(0));
    for (size_t k = 0; k < a.c.size(); k++) c[k] = c[k] + a.c[k];
    for (size_t k = 0; k < b.c.size(); k++) c[k] = c[k] + b.c[k];
    return Poly(std::move(c));
}

Poly operator-(const Poly& a, const Poly& b) {
    std::vector<Rational> c(std::max(a.c.size(), b.c.size()), Rational(0));
    for (size_t k = 0; k < a.c.size(); k++) c[k] = c[k] + a.c[k];
    for (size_t k = 0; k < b.c.size(); k++) c[k] = c[k] - b.c[k];
    return Poly(std::move(c));
}

Poly operator*(const Poly& a, const Poly& b) {
    if (a.isZero() || b.isZero()) return Poly();
    std::vector<Rational> c(a.c.size() + b.c.size() - 1, Rational(0));
    for (size_t i = 0; i < a.c.size(); i++) {
        if (a.c[i].isZero()) continue;
        for (size_t j = 0; j < b.c.size(); j++) c[i + j] = c[i + j] + a.c[i] * b.c[j];
    }
    return Poly(std::move(c));
}

Poly operator*(const Poly& a, const Rational& r) {
    std::vector<Rational> c;
    for (const auto& x : a.c) c.push_back(x * r);
    return Poly(std::move(c));
}

void Poly::divMod(const Poly& a, const Poly& b, Poly& q, Poly& r) {
    if (b.isZero()) throw CasError("polynomial division by zero");
    r = a;
    std::vector<Rational> qc(a.c.size() >= b.c.size() ? a.c.size() - b.c.size() + 1 : 0, Rational(0));
    while (!r.isZero() && r.degree() >= b.degree()) {
        size_t shift = static_cast<size_t>(r.degree() - b.degree());
        Rational f = r.lead() / b.lead();
        qc[shift] = f;
        for (size_t k = 0; k < b.c.size(); k++) r.c[k + shift] = r.c[k + shift] - f * b.c[k];
        r.trim();
    }
    q = Poly(std::move(qc));
}

Poly Poly::gcd(Poly a, Poly b) {
    while (!b.isZero()) {
        Poly q, r;
        divMod(a, b, q, r);
        a = std::move(b);
        b = std::move(r);
    }
    return a.monic();
}

// ---------------------------------------------------------------- conversion

bool toPolyE(const E& e, const E& x, std::vector<E>& coeffs) {
    coeffs.clear();
    E ex = expand(e);
    std::vector<E> terms = ex->kind == Kind::Add ? ex->args : std::vector<E>{ex};
    for (const auto& t : terms) {
        int64_t deg = 0;
        std::vector<E> rest;
        std::vector<E> factors = t->kind == Kind::Mul ? t->args : std::vector<E>{t};
        for (const auto& f : factors) {
            if (freeOf(f, x)) {
                rest.push_back(f);
            } else if (f == x) {
                deg += 1;
            } else if (f->kind == Kind::Pow && f->args[0] == x && isInteger(f->args[1]) && f->args[1]->q.sign() > 0 &&
                       f->args[1]->q.num.fitsInt64()) {
                deg += f->args[1]->q.num.toInt64();
            } else {
                return false;
            }
        }
        if (deg > 100000) return false;
        if (coeffs.size() <= static_cast<size_t>(deg)) coeffs.resize(static_cast<size_t>(deg) + 1, zero());
        coeffs[static_cast<size_t>(deg)] = add(coeffs[static_cast<size_t>(deg)], mul(rest));
    }
    while (!coeffs.empty() && isZero(coeffs.back())) coeffs.pop_back();
    return true;
}

E fromPolyE(const std::vector<E>& coeffs, const E& x) {
    std::vector<E> terms;
    for (size_t k = 0; k < coeffs.size(); k++) terms.push_back(mul(coeffs[k], pow(x, num(static_cast<int64_t>(k)))));
    return add(terms);
}

bool toPoly(const E& e, const E& x, Poly& out) {
    std::vector<E> ce;
    if (!toPolyE(e, x, ce)) return false;
    std::vector<Rational> c;
    for (const auto& k : ce) {
        if (!isRationalNum(k)) return false;
        c.push_back(k->q);
    }
    out = Poly(std::move(c));
    return true;
}

E fromPoly(const Poly& p, const E& x) {
    std::vector<E> terms;
    for (size_t k = 0; k < p.c.size(); k++)
        if (!p.c[k].isZero()) terms.push_back(mul(num(p.c[k]), pow(x, num(static_cast<int64_t>(k)))));
    return add(terms);
}

// ---------------------------------------------------------------- numeric roots

std::vector<std::complex<double>> numericRoots(const std::vector<std::complex<double>>& coeffsIn) {
    using C = std::complex<double>;
    std::vector<C> c = coeffsIn;
    while (!c.empty() && std::abs(c.back()) == 0) c.pop_back();
    std::vector<C> roots;
    size_t lowZeros = 0;
    while (lowZeros < c.size() && std::abs(c[lowZeros]) == 0) lowZeros++;
    for (size_t k = 0; k < lowZeros; k++) roots.push_back(0);
    c.erase(c.begin(), c.begin() + static_cast<long>(lowZeros));
    if (c.size() <= 1) return roots;
    size_t n = c.size() - 1;
    C lead = c.back();
    for (auto& v : c) v /= lead;
    auto evalP = [&](C z) {
        C r = 0;
        for (size_t k = c.size(); k-- > 0;) r = r * z + c[k];
        return r;
    };
    double radius = 0;
    for (size_t k = 0; k < n; k++) radius = std::max(radius, std::pow(std::abs(c[k]), 1.0 / static_cast<double>(n - k)));
    radius = std::max(radius, 1e-3);
    std::vector<C> z(n);
    for (size_t k = 0; k < n; k++) z[k] = radius * std::polar(1.0, 2 * M_PI * static_cast<double>(k) / static_cast<double>(n) + 0.4);
    for (int iter = 0; iter < 2000; iter++) {
        double change = 0;
        for (size_t i = 0; i < n; i++) {
            C denom = 1;
            for (size_t j = 0; j < n; j++)
                if (j != i) denom *= (z[i] - z[j]);
            if (std::abs(denom) == 0) denom = 1e-12;
            C delta = evalP(z[i]) / denom;
            z[i] -= delta;
            change = std::max(change, std::abs(delta) / std::max(1.0, std::abs(z[i])));
        }
        if (change < 1e-15) break;
    }
    // Newton polish.
    std::vector<C> dc;
    for (size_t k = 1; k < c.size(); k++) dc.push_back(c[k] * static_cast<double>(k));
    for (auto& r : z) {
        for (int it = 0; it < 5; it++) {
            C d = 0;
            for (size_t k = dc.size(); k-- > 0;) d = d * r + dc[k];
            if (std::abs(d) == 0) break;
            r -= evalP(r) / d;
        }
        if (std::fabs(r.imag()) <= 1e-10 * std::max(1.0, std::abs(r))) r = C(r.real(), 0);
        if (std::fabs(r.real()) <= 1e-14 * std::max(1.0, std::abs(r))) r = C(0, r.imag());
        roots.push_back(r);
    }
    std::sort(roots.begin(), roots.end(), [](const C& a, const C& b) {
        if (a.real() != b.real()) return a.real() < b.real();
        return a.imag() < b.imag();
    });
    return roots;
}

}  // namespace cas
