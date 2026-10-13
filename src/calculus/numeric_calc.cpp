#include "calculus/numeric_calc.h"

#include <algorithm>
#include <cmath>
#include <queue>

#include "cas/bridge.h"
#include "cas/expr.h"
#include "common/error.h"
#include "numeric/ops.h"
#include "vm/vm.h"

namespace {

double realResult(const Value& r, const char* fname) {
    if (isRealNumber(r)) return toDouble(r);
    if (isNumber(r)) {
        std::complex<double> z = toComplex(r);
        if (std::fabs(z.imag()) <= 1e-12 * std::max(1.0, std::fabs(z.real()))) return z.real();
        fail(std::string(fname) + ": the function returned a complex value " + toString(r));
    }
    double d;
    if (isSym(r) && symToDouble(r, d)) return d;
    if (isQuantity(r)) fail(std::string(fname) + ": the function returned a quantity with units; use a plain number");
    fail(std::string(fname) + ": the function must return a real number, got " + typeName(r));
}

double realArg(const Value& v, const char* fname) {
    if (isRealNumber(v)) return toDouble(v);
    double d;
    if (isSym(v) && symToDouble(v, d)) return d;
    fail(std::string(fname) + ": expected a real number, got " + toString(v));
}

// Splits the common argument forms: f(callable, ...), f(expr, var, ...), f(expr with one symbol, ...).
struct FnArgs {
    std::function<double(double)> f;
    int rest;  // index of the first argument after the function (and variable)
};

bool isVariable(const Value& v) {
    if (!isSym(v)) return false;
    cas::E e = toExpr(v);
    return cas::isSymbol(e) && !cas::isConstantSymbol(e);
}

FnArgs functionArgs(VM& vm, int argc, Value* args) {
    if (argc >= 2 && !isCallable(args[0]) && isVariable(args[1])) return {makeRealFunction(vm, args[0], &args[1]), 2};
    return {makeRealFunction(vm, args[0], nullptr), 1};
}

// ---------------------------------------------------------------- differentiation

double ridders(const std::function<double(double)>& f, double x, double h) {
    const int ntab = 10;
    const double con = 1.4, con2 = con * con, safe = 2.0;
    double a[ntab][ntab];
    double hh = h, err = 1e300, ans = 0;
    a[0][0] = (f(x + hh) - f(x - hh)) / (2 * hh);
    ans = a[0][0];
    for (int i = 1; i < ntab; i++) {
        hh /= con;
        a[0][i] = (f(x + hh) - f(x - hh)) / (2 * hh);
        double fac = con2;
        for (int j = 1; j <= i; j++) {
            a[j][i] = (a[j - 1][i] * fac - a[j - 1][i - 1]) / (fac - 1);
            fac *= con2;
            double errt = std::max(std::fabs(a[j][i] - a[j - 1][i]), std::fabs(a[j][i] - a[j - 1][i - 1]));
            if (errt <= err) {
                err = errt;
                ans = a[j][i];
            }
        }
        if (std::fabs(a[i][i] - a[i - 1][i - 1]) >= safe * err) break;
    }
    return ans;
}

double derivative(const std::function<double(double)>& f, double x, int order) {
    double h = 0.1 * std::max(1.0, std::fabs(x));
    if (order == 1) return ridders(f, x, h);
    std::function<double(double)> lower = [&](double t) { return derivative(f, t, order - 1); };
    return ridders(lower, x, h);
}

// ---------------------------------------------------------------- integration

const double xgk[8] = {0.991455371120812639206854697526329, 0.949107912342758524526189684047851,
                       0.864864423359769072789712788640926, 0.741531185599394439863864773280788,
                       0.586087235467691130294144845693013, 0.405845151377397166906606412076961,
                       0.207784955007898467600689403773245, 0.0};
const double wgk[8] = {0.022935322010529224963732008058970, 0.063092092629978553290700663189204,
                       0.104790010322250183839876322541518, 0.140653259715525918745189590510238,
                       0.169004726639267902826583426598550, 0.190350578064785409913256402421014,
                       0.204432940075298892414161999234649, 0.209482141084727828012999174891714};
const double wg[4] = {0.129484966168869693270611432679082, 0.279705391489276667901467771423780,
                      0.381830050505118944950369775488975, 0.417959183673469387755102040816327};

struct Segment {
    double a, b, value, error;
    bool operator<(const Segment& o) const { return error < o.error; }
};

Segment gk15(const std::function<double(double)>& f, double a, double b) {
    double c = 0.5 * (a + b), h = 0.5 * (b - a);
    double fc = f(c);
    double kronrod = fc * wgk[7], gauss = fc * wg[3];
    for (int j = 0; j < 7; j++) {
        double dx = h * xgk[j];
        double f1 = f(c - dx), f2 = f(c + dx);
        kronrod += wgk[j] * (f1 + f2);
        if (j % 2 == 1) gauss += wg[j / 2] * (f1 + f2);
    }
    return {a, b, kronrod * h, std::fabs((kronrod - gauss) * h)};
}

double adaptive(const std::function<double(double)>& f, double a, double b) {
    std::priority_queue<Segment> heap;
    Segment first = gk15(f, a, b);
    heap.push(first);
    double total = first.value, error = first.error;
    for (int iter = 0; iter < 5000; iter++) {
        if (error <= std::max(1e-13, 1e-11 * std::fabs(total))) break;
        Segment s = heap.top();
        heap.pop();
        double m = 0.5 * (s.a + s.b);
        if (m <= s.a || m >= s.b) {
            heap.push(s);
            break;
        }
        Segment l = gk15(f, s.a, m), r = gk15(f, m, s.b);
        total += l.value + r.value - s.value;
        error += l.error + r.error - s.error;
        heap.push(l);
        heap.push(r);
    }
    double sum = 0;
    while (!heap.empty()) {
        sum += heap.top().value;
        heap.pop();
    }
    if (!std::isfinite(sum)) fail("nintegrate: the integral does not converge");
    return sum;
}

}  // namespace

double integrateNumeric(const std::function<double(double)>& f, double a, double b) {
    if (a == b) return 0;
    if (a > b) return -integrateNumeric(f, b, a);
    bool ia = std::isinf(a), ib = std::isinf(b);
    if (ia && ib) {
        return adaptive(
            [&](double t) {
                double d = 1 - t * t;
                return f(t / d) * (1 + t * t) / (d * d);
            },
            -1, 1);
    }
    if (ib) {
        return adaptive(
            [&](double t) {
                double d = 1 - t;
                return f(a + t / d) / (d * d);
            },
            0, 1);
    }
    if (ia) {
        return adaptive([&](double t) { return f(b - (1 - t) / t) / (t * t); }, 0, 1);
    }
    return adaptive(f, a, b);
}

std::function<double(double)> makeRealFunction(VM& vm, const Value& f, const Value* var) {
    if (isCallable(f)) {
        Value fn = f;
        VM* vmp = &vm;
        return [vmp, fn](double x) { return realResult(vmp->call(fn, {Value::real(x)}), "function"); };
    }
    if (isSym(f)) {
        cas::E e = toExpr(f);
        std::string name;
        if (var) {
            cas::E v = toExpr(*var);
            if (!cas::isSymbol(v)) fail("expected a variable symbol, got " + toString(*var));
            name = v->name;
        } else {
            std::vector<cas::E> syms = cas::freeSymbols(e);
            if (syms.size() > 1)
                fail("the expression " + toString(f) + " has several variables; pass the variable explicitly");
            if (syms.size() == 1) name = syms[0]->name;
        }
        return cas::compileReal(e, name);
    }
    if (isRealNumber(f)) {
        double c = toDouble(f);
        return [c](double) { return c; };
    }
    fail("expected a function or an expression, got " + typeName(f));
}

namespace {

Value n_nderiv(VM& vm, int argc, Value* args) {
    FnArgs fa = functionArgs(vm, argc, args);
    if (fa.rest >= argc) fail("nderiv(f, x0) needs a point");
    double x0 = realArg(args[fa.rest], "nderiv");
    int order = fa.rest + 1 < argc ? static_cast<int>(toInt64(args[fa.rest + 1], "a derivative order")) : 1;
    if (order < 1 || order > 4) fail("nderiv: order must be between 1 and 4");
    return Value::real(derivative(fa.f, x0, order));
}

Value n_nintegrate(VM& vm, int argc, Value* args) {
    FnArgs fa = functionArgs(vm, argc, args);
    if (fa.rest + 2 > argc) fail("nintegrate(f, a, b) needs two bounds");
    double a = realArg(args[fa.rest], "nintegrate"), b = realArg(args[fa.rest + 1], "nintegrate");
    return Value::real(integrateNumeric(fa.f, a, b));
}

double brent(const std::function<double(double)>& f, double a, double b) {
    double fa = f(a), fb = f(b);
    if (fa == 0) return a;
    if (fb == 0) return b;
    if ((fa > 0) == (fb > 0)) fail("root: f(a) and f(b) must have opposite signs");
    double c = b, fc = fb, d = 0, e = 0;
    for (int iter = 0; iter < 200; iter++) {
        if ((fb > 0) == (fc > 0)) {
            c = a;
            fc = fa;
            e = d = b - a;
        }
        if (std::fabs(fc) < std::fabs(fb)) {
            a = b;
            b = c;
            c = a;
            fa = fb;
            fb = fc;
            fc = fa;
        }
        double tol = 2 * 1e-16 * std::fabs(b) + 0.5e-15;
        double xm = 0.5 * (c - b);
        if (std::fabs(xm) <= tol || fb == 0) return b;
        if (std::fabs(e) >= tol && std::fabs(fa) > std::fabs(fb)) {
            double s = fb / fa, p, q;
            if (a == c) {
                p = 2 * xm * s;
                q = 1 - s;
            } else {
                double qq = fa / fc, r = fb / fc;
                p = s * (2 * xm * qq * (qq - r) - (b - a) * (r - 1));
                q = (qq - 1) * (r - 1) * (s - 1);
            }
            if (p > 0) q = -q;
            p = std::fabs(p);
            if (2 * p < std::min(3 * xm * q - std::fabs(tol * q), std::fabs(e * q))) {
                e = d;
                d = p / q;
            } else {
                d = xm;
                e = d;
            }
        } else {
            d = xm;
            e = d;
        }
        a = b;
        fa = fb;
        b += std::fabs(d) > tol ? d : (xm > 0 ? tol : -tol);
        fb = f(b);
    }
    return b;
}

double secant(const std::function<double(double)>& f, double x0) {
    double x1 = x0 + 1e-4 * std::max(1.0, std::fabs(x0));
    double f0 = f(x0), f1 = f(x1);
    for (int iter = 0; iter < 200; iter++) {
        if (f1 == 0) return x1;
        double denom = f1 - f0;
        if (denom == 0) break;
        double x2 = x1 - f1 * (x1 - x0) / denom;
        if (!std::isfinite(x2)) break;
        x0 = x1;
        f0 = f1;
        x1 = x2;
        f1 = f(x1);
        if (std::fabs(x1 - x0) <= 1e-15 * std::max(1.0, std::fabs(x1))) return x1;
    }
    if (std::fabs(f1) < 1e-9) return x1;
    fail("root: the iteration did not converge; try root(f, a, b) with a bracketing interval");
}

Value n_root(VM& vm, int argc, Value* args) {
    FnArgs fa = functionArgs(vm, argc, args);
    if (fa.rest >= argc) fail("root(f, a, b) or root(f, x0)");
    double a = realArg(args[fa.rest], "root");
    if (fa.rest + 1 < argc) return Value::real(brent(fa.f, a, realArg(args[fa.rest + 1], "root")));
    return Value::real(secant(fa.f, a));
}

Value n_newton(VM& vm, int argc, Value* args) {
    std::function<double(double)> f, df;
    int rest;
    if (isSym(args[0])) {
        const Value* var = (argc >= 3 && isSym(args[1])) ? &args[1] : nullptr;
        f = makeRealFunction(vm, args[0], var);
        cas::E e = toExpr(args[0]);
        cas::E x;
        if (var) {
            x = toExpr(*var);
        } else {
            std::vector<cas::E> syms = cas::freeSymbols(e);
            if (syms.size() != 1) fail("newton: pass the variable explicitly");
            x = syms[0];
        }
        df = cas::compileReal(cas::diff(e, x), x->name);
        rest = var ? 2 : 1;
    } else {
        f = makeRealFunction(vm, args[0], nullptr);
        rest = 1;
        if (argc > 2 && isCallable(args[2])) {
            df = makeRealFunction(vm, args[2], nullptr);
        } else {
            df = [&f](double x) { return ridders(f, x, 1e-3 * std::max(1.0, std::fabs(x))); };
        }
    }
    if (rest >= argc) fail("newton(f, x0) needs a starting point");
    double x = realArg(args[rest], "newton");
    for (int iter = 0; iter < 100; iter++) {
        double fx = f(x), d = df(x);
        if (fx == 0) return Value::real(x);
        if (d == 0 || !std::isfinite(d)) fail("newton: zero derivative at x = " + formatDouble(x, true));
        double step = fx / d;
        x -= step;
        if (std::fabs(step) <= 1e-15 * std::max(1.0, std::fabs(x))) return Value::real(x);
    }
    if (std::fabs(f(x)) < 1e-9) return Value::real(x);
    fail("newton: the iteration did not converge");
}

Value n_minimize(VM& vm, int argc, Value* args) {
    FnArgs fa = functionArgs(vm, argc, args);
    if (fa.rest + 2 > argc) fail("minimize(f, a, b) needs an interval");
    double a = realArg(args[fa.rest], "minimize"), b = realArg(args[fa.rest + 1], "minimize");
    const double g = (std::sqrt(5.0) - 1) / 2;
    double c = b - g * (b - a), d = a + g * (b - a);
    double fc = fa.f(c), fd = fa.f(d);
    for (int iter = 0; iter < 200 && std::fabs(b - a) > 1e-12 * std::max(1.0, std::fabs(a) + std::fabs(b)); iter++) {
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - g * (b - a);
            fc = fa.f(c);
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + g * (b - a);
            fd = fa.f(d);
        }
    }
    return Value::real(0.5 * (a + b));
}

// ---------------------------------------------------------------- ODEs

using Vec = std::vector<double>;

struct OdeSystem {
    VM& vm;
    Value f;
    bool scalar;
    Vec operator()(double t, const Vec& y) const {
        Value yv;
        if (scalar) {
            yv = Value::real(y[0]);
        } else {
            std::vector<Value> items;
            for (double v : y) items.push_back(Value::real(v));
            yv = newList(std::move(items));
        }
        Value r = vm.call(f, {Value::real(t), yv});
        Vec out;
        if (isList(r)) {
            for (const auto& v : as<ObjList>(r)->items) out.push_back(realResult(v, "ode"));
        } else {
            out.push_back(realResult(r, "ode"));
        }
        if (out.size() != y.size()) fail("ode: f(t, y) returned a different number of components than y");
        return out;
    }
};

Vec axpy(const Vec& y, double h, std::initializer_list<std::pair<double, const Vec*>> terms) {
    Vec r = y;
    for (auto& t : terms)
        for (size_t i = 0; i < r.size(); i++) r[i] += h * t.first * (*t.second)[i];
    return r;
}

// Dormand-Prince 5(4) from t0 to t1 with adaptive steps.
Vec dopri(const OdeSystem& f, double t0, double t1, Vec y) {
    double t = t0, dir = t1 > t0 ? 1 : -1;
    double h = (t1 - t0) / 100;
    const double rtol = 1e-10, atol = 1e-12;
    Vec k1 = f(t, y);
    for (int steps = 0; steps < 1000000; steps++) {
        if ((t1 - t) * dir <= 0) return y;
        if ((t + h - t1) * dir > 0) h = t1 - t;
        Vec k2 = f(t + h / 5, axpy(y, h, {{1.0 / 5, &k1}}));
        Vec k3 = f(t + 3 * h / 10, axpy(y, h, {{3.0 / 40, &k1}, {9.0 / 40, &k2}}));
        Vec k4 = f(t + 4 * h / 5, axpy(y, h, {{44.0 / 45, &k1}, {-56.0 / 15, &k2}, {32.0 / 9, &k3}}));
        Vec k5 = f(t + 8 * h / 9,
                   axpy(y, h, {{19372.0 / 6561, &k1}, {-25360.0 / 2187, &k2}, {64448.0 / 6561, &k3}, {-212.0 / 729, &k4}}));
        Vec k6 = f(t + h, axpy(y, h,
                               {{9017.0 / 3168, &k1},
                                {-355.0 / 33, &k2},
                                {46732.0 / 5247, &k3},
                                {49.0 / 176, &k4},
                                {-5103.0 / 18656, &k5}}));
        Vec y5 = axpy(y, h,
                      {{35.0 / 384, &k1}, {500.0 / 1113, &k3}, {125.0 / 192, &k4}, {-2187.0 / 6784, &k5}, {11.0 / 84, &k6}});
        Vec k7 = f(t + h, y5);
        Vec y4 = axpy(y, h,
                      {{5179.0 / 57600, &k1},
                       {7571.0 / 16695, &k3},
                       {393.0 / 640, &k4},
                       {-92097.0 / 339200, &k5},
                       {187.0 / 2100, &k6},
                       {1.0 / 40, &k7}});
        double err = 0;
        for (size_t i = 0; i < y.size(); i++) {
            double sc = atol + rtol * std::max(std::fabs(y[i]), std::fabs(y5[i]));
            err = std::max(err, std::fabs(y5[i] - y4[i]) / sc);
        }
        if (!std::isfinite(err)) {
            h /= 10;
            if (std::fabs(h) < 1e-300) fail("ode: the solution diverged");
            continue;
        }
        if (err <= 1) {
            t += h;
            y = y5;
            k1 = k7;
        }
        double factor = err == 0 ? 5 : std::min(5.0, std::max(0.2, 0.9 * std::pow(err, -0.2)));
        h *= factor;
        if (std::fabs(h) < 1e-14 * std::max(1.0, std::fabs(t))) fail("ode: step size underflow (stiff problem?)");
    }
    fail("ode: too many steps");
}

Vec rk4(const OdeSystem& f, double t0, double t1, Vec y, int64_t n) {
    double h = (t1 - t0) / static_cast<double>(n), t = t0;
    for (int64_t s = 0; s < n; s++) {
        Vec k1 = f(t, y);
        Vec k2 = f(t + h / 2, axpy(y, h, {{0.5, &k1}}));
        Vec k3 = f(t + h / 2, axpy(y, h, {{0.5, &k2}}));
        Vec k4 = f(t + h, axpy(y, h, {{1.0, &k3}}));
        y = axpy(y, h, {{1.0 / 6, &k1}, {1.0 / 3, &k2}, {1.0 / 3, &k3}, {1.0 / 6, &k4}});
        t = t0 + static_cast<double>(s + 1) * h;
    }
    return y;
}

Value vecValue(const Vec& y, bool scalar) {
    if (scalar) return Value::real(y[0]);
    std::vector<Value> items;
    for (double v : y) items.push_back(Value::real(v));
    return newList(std::move(items));
}

OdeSystem odeSetup(VM& vm, Value* args, Vec& y0, const char* fname) {
    if (!isCallable(args[0])) fail(std::string(fname) + " expects a function f(t, y)");
    bool scalar = !isList(args[1]);
    if (scalar) {
        y0.push_back(realArg(args[1], fname));
    } else {
        for (const auto& v : as<ObjList>(args[1])->items) y0.push_back(realArg(v, fname));
    }
    return OdeSystem{vm, args[0], scalar};
}

Value n_ode(VM& vm, int argc, Value* args) {
    Vec y;
    OdeSystem sys = odeSetup(vm, args, y, "ode");
    double t0 = realArg(args[2], "ode"), t1 = realArg(args[3], "ode");
    if (argc < 5) return vecValue(dopri(sys, t0, t1, y), sys.scalar);
    int64_t n = toInt64(args[4], "a sample count");
    if (n < 1) fail("ode: sample count must be positive");
    std::vector<Value> out{newList({Value::real(t0), vecValue(y, sys.scalar)})};
    double prev = t0;
    for (int64_t k = 1; k <= n; k++) {
        double t = t0 + (t1 - t0) * static_cast<double>(k) / static_cast<double>(n);
        y = dopri(sys, prev, t, y);
        out.push_back(newList({Value::real(t), vecValue(y, sys.scalar)}));
        prev = t;
    }
    return newList(std::move(out));
}

Value n_rk4(VM& vm, int argc, Value* args) {
    Vec y;
    OdeSystem sys = odeSetup(vm, args, y, "rk4");
    double t0 = realArg(args[2], "rk4"), t1 = realArg(args[3], "rk4");
    int64_t n = argc > 4 ? toInt64(args[4], "a step count") : 1000;
    if (n < 1) fail("rk4: step count must be positive");
    return vecValue(rk4(sys, t0, t1, y, n), sys.scalar);
}

}  // namespace

void registerCalculus(VM& vm) {
    const char* c = "numerical calculus";
    vm.defineNative("nderiv", 2, 4, n_nderiv, "nderiv(f, x0, order) - numerical derivative (Ridders)", c);
    vm.defineNative("nintegrate", 3, 4, n_nintegrate,
                    "nintegrate(f, a, b) - adaptive Gauss-Kronrod quadrature; bounds may be Â±âˆž", c);
    vm.defineNative("root", 2, 4, n_root, "root(f, a, b) - Brent's method; root(f, x0) - secant method", c);
    vm.defineNative("newton", 2, 4, n_newton, "newton(f, x0, df) - Newton's method", c);
    vm.defineNative("minimize", 3, 4, n_minimize, "minimize(f, a, b) - local minimum on [a, b] (golden section)", c);
    vm.defineNative("ode", 4, 5, n_ode,
                    "ode(f, y0, t0, t1, n) - solve y' = f(t, y) with RK45; with n, return n+1 samples [t, y]", c);
    vm.defineNative("rk4", 4, 5, n_rk4, "rk4(f, y0, t0, t1, n) - classic Runge-Kutta with n steps", c);
}
