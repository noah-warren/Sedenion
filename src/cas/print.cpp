#include <algorithm>
#include <cctype>
#include <map>

#include "cas/expr.h"
#include "numeric/ops.h"

namespace cas {

namespace {

enum Prec { P_EQ = 0, P_ADD = 1, P_MUL = 2, P_NEG = 3, P_POW = 4, P_ATOM = 5 };

std::string str(const E& e, int ctx);

std::string numStr(const E& e) {
    if (e->exact) return e->q.toString();
    return formatDouble(e->d, true);
}

int precOf(const E& e) {
    switch (e->kind) {
        case Kind::Num:
            if (isNegativeNum(e)) return P_NEG;
            if (e->exact && !e->q.isInteger()) return P_MUL;
            return P_ATOM;
        case Kind::Sym:
        case Kind::Fn: return P_ATOM;
        case Kind::Add: return P_ADD;
        case Kind::Mul: return isNegativeTerm(e) ? P_NEG : P_MUL;
        case Kind::Pow: {
            const E& x = e->args[1];
            if (isNegativeTerm(x)) return P_MUL;
            if (x == half() || (isRationalNum(x) && x->q == Rational(BigInt(1), BigInt(3)))) return P_ATOM;
            return P_POW;
        }
        case Kind::Eq: return P_EQ;
    }
    return P_ATOM;
}

std::string wrap(const E& e, int ctx) {
    std::string s = str(e, 0);
    if (precOf(e) < ctx) return "(" + s + ")";
    return s;
}

// Total degree in the free symbols, used to print polynomials highest degree first.
double degreeOf(const E& t) {
    switch (t->kind) {
        case Kind::Num: return -1;
        case Kind::Sym: return isConstantSymbol(t) ? 0 : 1;
        case Kind::Pow:
            if (isNum(t->args[1])) return degreeOf(t->args[0]) * std::max(0.0, numValue(t->args[1]));
            return 0.5;
        case Kind::Mul: {
            double d = 0;
            for (const auto& a : t->args) d += std::max(0.0, degreeOf(a));
            return d;
        }
        default: return 0;
    }
}

double degreeIn(const E& t, const E& x) {
    if (t == x) return 1;
    if (t->kind == Kind::Pow && t->args[0] == x && isNum(t->args[1])) return numValue(t->args[1]);
    if (t->kind == Kind::Mul) {
        double d = 0;
        for (const auto& a : t->args) d += degreeIn(a, x);
        return d;
    }
    return 0;
}

std::string addStr(const E& e) {
    std::vector<E> terms = e->args;
    std::vector<E> syms = freeSymbols(e);
    std::stable_sort(terms.begin(), terms.end(), [&](const E& a, const E& b) {
        double da = degreeOf(a), db = degreeOf(b);
        if (da != db) return da > db;
        for (const auto& x : syms) {
            double xa = degreeIn(a, x), xb = degreeIn(b, x);
            if (xa != xb) return xa > xb;
        }
        return false;
    });
    std::string s;
    for (size_t k = 0; k < terms.size(); k++) {
        const E& t = terms[k];
        if (k == 0) {
            s = wrap(t, P_ADD);
            continue;
        }
        if (isNegativeTerm(t)) {
            s += " - " + wrap(neg(t), P_MUL);
        } else {
            s += " + " + wrap(t, P_ADD);
        }
    }
    return s;
}

std::string powStr(const E& base, const E& ex) {
    if (ex == half()) return "sqrt(" + str(base, 0) + ")";
    if (isRationalNum(ex) && ex->q == Rational(BigInt(1), BigInt(3))) return "cbrt(" + str(base, 0) + ")";
    std::string b = wrap(base, P_ATOM);
    if (isNum(base) && !isNegativeNum(base) && (base->exact ? base->q.isInteger() : true)) b = numStr(base);
    std::string x;
    if ((isNum(ex) && !isNegativeNum(ex) && (!ex->exact || ex->q.isInteger())) || ex->kind == Kind::Sym ||
        ex->kind == Kind::Fn)
        x = str(ex, 0);
    else
        x = "(" + str(ex, 0) + ")";
    return b + "^" + x;
}

bool startsWithDigit(const std::string& s) { return !s.empty() && std::isdigit(static_cast<unsigned char>(s[0])); }

// Joins factors: a numeric coefficient is juxtaposed with what follows (3x, 2(x + 1),
// 2sqrt(3)); other factors are separated by '*'.
std::string joinFactors(const std::string& coeff, const std::vector<std::string>& parts) {
    std::string s = coeff;
    for (size_t k = 0; k < parts.size(); k++) {
        const std::string& p = parts[k];
        if (s.empty()) {
            s = p;
        } else if (k == 0 && !startsWithDigit(p) && p[0] != '-' && p[0] != '.') {
            s += p;
        } else {
            s += "*" + p;
        }
    }
    return s;
}

std::string mulStr(const E& e) {
    std::vector<E> factors = e->kind == Kind::Mul ? e->args : std::vector<E>{e};
    E coeff = one();
    if (isNum(factors[0])) {
        coeff = factors[0];
        factors.erase(factors.begin());
    }
    bool negative = isNegativeNum(coeff);
    if (negative) coeff = coeff->exact ? num(-coeff->q) : real(-coeff->d);
    std::vector<E> top, bottom;
    for (const auto& f : factors) {
        if (f->kind == Kind::Pow && isNegativeTerm(f->args[1]))
            bottom.push_back(pow(f->args[0], neg(f->args[1])));
        else
            top.push_back(f);
    }
    // Display order: i, then powers of plain symbols, then everything else (2x*sin(x), x*e^x).
    auto isMonomial = [](const E& f) {
        E b = f->kind == Kind::Pow && isNum(f->args[1]) ? f->args[0] : f;
        return isSymbol(b) && !isConstantSymbol(b);
    };
    for (auto* list : {&top, &bottom}) {
        auto mid = std::stable_partition(list->begin(), list->end(), [](const E& f) { return f == imagUnit(); });
        std::stable_partition(mid, list->end(), isMonomial);
    }
    std::string numCoeff, denCoeff;
    if (coeff->exact) {
        if (!coeff->q.num.isOne() || top.empty()) numCoeff = coeff->q.num.toString();
        if (!coeff->q.den.isOne()) denCoeff = coeff->q.den.toString();
    } else if (!(coeff->d == 1 && !top.empty())) {
        numCoeff = numStr(coeff);
    }
    std::vector<std::string> topParts, bottomParts;
    for (const auto& f : top) {
        if (f->kind == Kind::Pow)
            topParts.push_back(precOf(f) >= P_POW ? powStr(f->args[0], f->args[1]) : "(" + str(f, 0) + ")");
        else
            topParts.push_back(wrap(f, P_POW));
    }
    for (const auto& f : bottom) {
        if (f->kind == Kind::Pow)
            bottomParts.push_back(powStr(f->args[0], f->args[1]));
        else
            bottomParts.push_back(wrap(f, P_POW));
    }
    if (!numCoeff.empty() && topParts.size() == 1 && numCoeff == "1" && !top.empty()) numCoeff.clear();
    std::string numerator = joinFactors(numCoeff, topParts);
    if (numerator.empty()) numerator = "1";
    std::string s = numerator;
    if (!bottomParts.empty() || !denCoeff.empty()) {
        std::string denominator = joinFactors(denCoeff, bottomParts);
        bool needParens = bottomParts.size() + (denCoeff.empty() ? 0 : 1) > 1;
        if (bottomParts.size() == 1 && denCoeff.empty() && bottom[0]->kind == Kind::Add) needParens = false;
        if (bottomParts.size() == 1 && denCoeff.empty() && bottom[0]->kind == Kind::Add)
            denominator = "(" + str(bottom[0], 0) + ")";
        if (needParens) denominator = "(" + denominator + ")";
        if (top.size() == 1 && top[0]->kind == Kind::Add && numCoeff.empty()) s = "(" + str(top[0], 0) + ")";
        s += "/" + denominator;
    }
    return negative ? "-" + s : s;
}

std::string str(const E& e, int ctx) {
    (void)ctx;
    switch (e->kind) {
        case Kind::Num: return numStr(e);
        case Kind::Sym: return e->name;
        case Kind::Add: return addStr(e);
        case Kind::Mul: return mulStr(e);
        case Kind::Pow:
            if (isNegativeTerm(e->args[1])) return mulStr(e);
            return powStr(e->args[0], e->args[1]);
        case Kind::Fn: {
            std::string s = e->name + "(";
            for (size_t k = 0; k < e->args.size(); k++) {
                if (k) s += ", ";
                s += str(e->args[k], 0);
            }
            return s + ")";
        }
        case Kind::Eq: return str(e->args[0], 0) + " == " + str(e->args[1], 0);
    }
    return "?";
}

}  // namespace

std::string toString(const E& e) { return str(e, 0); }

}  // namespace cas
