#include <cmath>

#include "cas/expr.h"

namespace cas {

using C = std::complex<double>;

namespace {

C cgammaLanczos(C z) {
    if (z.real() < 0.5) return M_PI / (std::sin(M_PI * z) * cgammaLanczos(1.0 - z));
    z -= 1.0;
    static const double p[] = {0.99999999999980993,  676.5203681218851,     -1259.1392167224028,
                               771.32342877765313,   -176.61502916214059,   12.507343278686905,
                               -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7};
    C x = p[0];
    for (int i = 1; i < 9; i++) x += p[i] / (z + static_cast<double>(i));
    C t = z + 7.5;
    return std::sqrt(2 * M_PI) * std::pow(t, z + 0.5) * std::exp(-t) * x;
}

bool isReal(const C& z) { return z.imag() == 0; }

}  // namespace

bool evalFnComplex(const std::string& name, const std::vector<C>& args, C& out) {
    if (args.size() != 1) return false;
    const C& z = args[0];
    double x = z.real();
    bool real = isReal(z);
    if (name == "sin") out = real ? C(std::sin(x)) : std::sin(z);
    else if (name == "cos") out = real ? C(std::cos(x)) : std::cos(z);
    else if (name == "tan") out = real ? C(std::tan(x)) : std::tan(z);
    else if (name == "asin") out = (real && std::fabs(x) <= 1) ? C(std::asin(x)) : std::asin(z);
    else if (name == "acos") out = (real && std::fabs(x) <= 1) ? C(std::acos(x)) : std::acos(z);
    else if (name == "atan") out = real ? C(std::atan(x)) : std::atan(z);
    else if (name == "sinh") out = real ? C(std::sinh(x)) : std::sinh(z);
    else if (name == "cosh") out = real ? C(std::cosh(x)) : std::cosh(z);
    else if (name == "tanh") out = real ? C(std::tanh(x)) : std::tanh(z);
    else if (name == "asinh") out = real ? C(std::asinh(x)) : std::asinh(z);
    else if (name == "acosh") out = (real && x >= 1) ? C(std::acosh(x)) : std::acosh(z);
    else if (name == "atanh") out = (real && std::fabs(x) < 1) ? C(std::atanh(x)) : std::atanh(z);
    else if (name == "ln") out = (real && x >= 0) ? C(std::log(x)) : std::log(z);
    else if (name == "exp") out = std::exp(z);
    else if (name == "sqrt") out = (real && x >= 0) ? C(std::sqrt(x)) : std::sqrt(z);
    else if (name == "abs") out = std::abs(z);
    else if (name == "floor" && real) out = std::floor(x);
    else if (name == "ceil" && real) out = std::ceil(x);
    else if (name == "sign" && real) out = x > 0 ? 1 : (x < 0 ? -1 : 0);
    else if (name == "gamma") out = real ? C(std::tgamma(x)) : cgammaLanczos(z);
    else if (name == "factorial") out = real ? C(std::tgamma(x + 1)) : cgammaLanczos(z + 1.0);
    else if (name == "erf" && real) out = std::erf(x);
    else if (name == "erfc" && real) out = std::erfc(x);
    else if (name == "re") out = z.real();
    else if (name == "im") out = z.imag();
    else if (name == "conj") out = std::conj(z);
    else if (name == "arg") out = std::arg(z);
    else return false;
    return true;
}

bool evalComplex(const E& e, const Env& env, C& out) {
    switch (e->kind) {
        case Kind::Num: out = numValue(e); return true;
        case Kind::Sym:
            if (e->name == PI_NAME) out = M_PI;
            else if (e->name == E_NAME) out = M_E;
            else if (e->name == I_NAME) out = C(0, 1);
            else if (!env || !env(e->name, out)) return false;
            return true;
        case Kind::Add: {
            C s = 0;
            for (const auto& a : e->args) {
                C v;
                if (!evalComplex(a, env, v)) return false;
                s += v;
            }
            out = s;
            return true;
        }
        case Kind::Mul: {
            C p = 1;
            for (const auto& a : e->args) {
                C v;
                if (!evalComplex(a, env, v)) return false;
                p *= v;
            }
            out = p;
            return true;
        }
        case Kind::Pow: {
            C b, x;
            if (!evalComplex(e->args[0], env, b) || !evalComplex(e->args[1], env, x)) return false;
            if (isReal(b) && isReal(x)) {
                double br = b.real(), xr = x.real();
                if (br >= 0 || std::floor(xr) == xr) {
                    out = std::pow(br, xr);
                    return true;
                }
                // Real-root convention for exact exponents with an odd denominator: cbrt(-8) = -2.
                const E& ex = e->args[1];
                if (isRationalNum(ex) && !ex->q.den.isEven()) {
                    double m = std::pow(-br, xr);
                    out = ex->q.num.isEven() ? m : -m;
                    return true;
                }
            }
            if (b == C(0) ) {
                out = x.real() > 0 ? C(0) : C(INFINITY);
                return true;
            }
            out = std::pow(b, x);
            return true;
        }
        case Kind::Fn: {
            std::vector<C> args;
            for (const auto& a : e->args) {
                C v;
                if (!evalComplex(a, env, v)) return false;
                args.push_back(v);
            }
            return evalFnComplex(e->name, args, out);
        }
        default: return false;
    }
}

namespace {

E complexToExpr(const C& z) {
    if (z.imag() == 0) return real(z.real());
    if (z.real() == 0) return mul(real(z.imag()), imagUnit());
    return add(real(z.real()), mul(real(z.imag()), imagUnit()));
}

}  // namespace

E evalf(const E& e) {
    if (isNum(e)) return e->exact ? real(numValue(e)) : e;
    C z;
    if (evalComplex(e, nullptr, z)) {
        if (std::fabs(z.imag()) <= 1e-15 * std::abs(z)) z = C(z.real(), 0);
        return complexToExpr(z);
    }
    if (e->args.empty()) return e;
    std::vector<E> args;
    for (const auto& a : e->args) args.push_back(evalf(a));
    return rebuild(e, args);
}

std::function<double(double)> compileReal(const E& e, const std::string& var) {
    return [e, var](double x) -> double {
        C z;
        Env env = [&](const std::string& name, C& out) {
            if (name != var) return false;
            out = x;
            return true;
        };
        if (!evalComplex(e, env, z)) throw CasError("cannot evaluate " + toString(e) + " numerically");
        if (std::fabs(z.imag()) > 1e-12 * std::max(1.0, std::abs(z))) return NAN;
        return z.real();
    };
}

}  // namespace cas
