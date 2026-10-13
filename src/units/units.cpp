#include "units/units.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

#include "cas/bridge.h"
#include "cas/expr.h"
#include "common/error.h"
#include "common/utf8.h"
#include "numeric/ops.h"
#include "vm/arith.h"
#include "vm/vm.h"

namespace {

// Factors are plain Rationals: a static table must not hold heap Values the GC could free.
struct UnitDef {
    Rational factor;  // SI magnitude of one unit (times π/180 when `degree`)
    Dim dim;
    bool prefixable;
    bool degree = false;
};

Dim dimOf(int m, int kg, int s, int A = 0, int K = 0, int mol = 0, int cd = 0) {
    Dim d;
    int v[7] = {m, kg, s, A, K, mol, cd};
    for (int i = 0; i < 7; i++) d.e[i] = static_cast<int8_t>(v[i]);
    return d;
}

Rational ratio(int64_t n, int64_t d) { return Rational(BigInt(n), BigInt(d)); }
Rational exactDecimal(const char* s) {
    // "1.602176634e-19" -> exact rational
    std::string t(s);
    size_t e = t.find_first_of("eE");
    int64_t exp10 = 0;
    if (e != std::string::npos) {
        exp10 = std::stoll(t.substr(e + 1));
        t = t.substr(0, e);
    }
    size_t dot = t.find('.');
    if (dot != std::string::npos) {
        exp10 -= static_cast<int64_t>(t.size() - dot - 1);
        t.erase(dot, 1);
    }
    Rational r(BigInt::fromString(t));
    Rational ten(10);
    return r * Rational::pow(ten, exp10);
}

const std::map<std::string, UnitDef>& unitTable() {
    static const std::map<std::string, UnitDef>* table = [] {
        auto* t = new std::map<std::string, UnitDef>();
        auto def = [&](const std::string& name, Rational f, Dim d, bool prefix, bool degree = false) {
            (*t)[name] = UnitDef{std::move(f), d, prefix, degree};
        };
        auto R = [](int64_t v) { return Rational(v); };
        Rational one(1);
        def("m", one, dimOf(1, 0, 0), true);
        def("g", ratio(1, 1000), dimOf(0, 1, 0), true);
        def("s", one, dimOf(0, 0, 1), true);
        def("A", one, dimOf(0, 0, 0, 1), true);
        def("K", one, dimOf(0, 0, 0, 0, 1), true);
        def("mol", one, dimOf(0, 0, 0, 0, 0, 1), true);
        def("cd", one, dimOf(0, 0, 0, 0, 0, 0, 1), true);
        def("Hz", one, dimOf(0, 0, -1), true);
        def("N", one, dimOf(1, 1, -2), true);
        def("J", one, dimOf(2, 1, -2), true);
        def("W", one, dimOf(2, 1, -3), true);
        def("Pa", one, dimOf(-1, 1, -2), true);
        def("C", one, dimOf(0, 0, 1, 1), true);
        def("V", one, dimOf(2, 1, -3, -1), true);
        def("Ω", one, dimOf(2, 1, -3, -2), true);
        def("ohm", one, dimOf(2, 1, -3, -2), true);
        def("F", one, dimOf(-2, -1, 4, 2), true);
        def("T", one, dimOf(0, 1, -2, -1), true);
        def("Wb", one, dimOf(2, 1, -2, -1), true);
        def("H", one, dimOf(2, 1, -2, -2), true);
        def("S", one, dimOf(-2, -1, 3, 2), true);
        def("L", ratio(1, 1000), dimOf(3, 0, 0), true);
        def("l", ratio(1, 1000), dimOf(3, 0, 0), true);
        def("eV", exactDecimal("1.602176634e-19"), dimOf(2, 1, -2), true);
        def("Wh", R(3600), dimOf(2, 1, -2), true);
        def("bar", R(100000), dimOf(-1, 1, -2), true);
        def("t", R(1000), dimOf(0, 1, 0), true);
        def("min", R(60), dimOf(0, 0, 1), false);
        def("h", R(3600), dimOf(0, 0, 1), false);
        def("day", R(86400), dimOf(0, 0, 1), false);
        def("week", R(604800), dimOf(0, 0, 1), false);
        def("yr", R(31557600), dimOf(0, 0, 1), false);
        def("year", R(31557600), dimOf(0, 0, 1), false);
        def("in", exactDecimal("0.0254"), dimOf(1, 0, 0), false);
        def("ft", exactDecimal("0.3048"), dimOf(1, 0, 0), false);
        def("yd", exactDecimal("0.9144"), dimOf(1, 0, 0), false);
        def("mi", exactDecimal("1609.344"), dimOf(1, 0, 0), false);
        def("nmi", R(1852), dimOf(1, 0, 0), false);
        def("au", R(149597870700), dimOf(1, 0, 0), false);
        def("ly", R(9460730472580800), dimOf(1, 0, 0), false);
        def("pc", exactDecimal("3.0856775814913673e16"), dimOf(1, 0, 0), false);
        def("Å", exactDecimal("1e-10"), dimOf(1, 0, 0), false);
        def("ha", R(10000), dimOf(2, 0, 0), false);
        def("acre", exactDecimal("4046.8564224"), dimOf(2, 0, 0), false);
        def("gal", exactDecimal("0.003785411784"), dimOf(3, 0, 0), false);
        def("lb", exactDecimal("0.45359237"), dimOf(0, 1, 0), false);
        def("oz", exactDecimal("0.028349523125"), dimOf(0, 1, 0), false);
        def("mph", ratio(1609344, 3600000), dimOf(1, 0, -1), false);
        def("kn", ratio(1852, 3600), dimOf(1, 0, -1), false);
        def("atm", R(101325), dimOf(-1, 1, -2), false);
        def("psi", exactDecimal("6894.757293168361"), dimOf(-1, 1, -2), false);
        def("mmHg", exactDecimal("133.322387415"), dimOf(-1, 1, -2), false);
        def("cal", exactDecimal("4.184"), dimOf(2, 1, -2), true);
        def("lbf", exactDecimal("4.4482216152605"), dimOf(1, 1, -2), false);
        def("hp", exactDecimal("745.69987158227022"), dimOf(2, 1, -3), false);
        def("Da", exactDecimal("1.66053906660e-27"), dimOf(0, 1, 0), true);
        def("rad", one, Dim(), false);
        def("sr", one, Dim(), false);
        def("°", ratio(1, 180), Dim(), false, true);
        def("deg", ratio(1, 180), Dim(), false, true);
        def("c", R(299792458), dimOf(1, 0, -1), false);
        def("g0", exactDecimal("9.80665"), dimOf(1, 0, -2), false);
        return t;
    }();
    return *table;
}

struct Prefix {
    const char* name;
    int exp10;
};

const Prefix kPrefixes[] = {{"Y", 24}, {"Z", 21}, {"E", 18}, {"P", 15}, {"T", 12}, {"G", 9},  {"M", 6},
                            {"k", 3},  {"h", 2},  {"da", 1}, {"d", -1}, {"c", -2}, {"m", -3}, {"µ", -6},
                            {"μ", -6}, {"u", -6}, {"n", -9}, {"p", -12}, {"f", -15}, {"a", -18}, {"z", -21},
                            {"y", -24}};

bool lookupUnit(const std::string& name, Value& factor, Dim& dim) {
    const auto& t = unitTable();
    auto it = t.find(name);
    if (it != t.end()) {
        factor = fromRational(it->second.factor);
        if (it->second.degree) factor = newSym(cas::mul(cas::num(it->second.factor), cas::piConst()));
        dim = it->second.dim;
        return true;
    }
    for (const auto& p : kPrefixes) {
        size_t pl = std::strlen(p.name);
        if (name.size() <= pl || name.compare(0, pl, p.name) != 0) continue;
        auto u = t.find(name.substr(pl));
        if (u == t.end() || !u->second.prefixable) continue;
        factor = fromRational(u->second.factor * Rational::pow(Rational(10), p.exp10));
        dim = u->second.dim;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- labels

// A label is a product of named units with integer exponents, kept in first-use order.
using Label = std::vector<std::pair<std::string, int>>;

std::string encodeLabel(const Label& l) {
    std::string s;
    for (const auto& p : l) {
        if (!s.empty()) s += ' ';
        s += p.first;
        if (p.second != 1) s += "^" + std::to_string(p.second);
    }
    return s;
}

Label decodeLabel(const std::string& s) {
    Label l;
    std::istringstream in(s);
    std::string part;
    while (in >> part) {
        size_t caret = part.rfind('^');
        if (caret == std::string::npos)
            l.push_back({part, 1});
        else
            l.push_back({part.substr(0, caret), std::stoi(part.substr(caret + 1))});
    }
    return l;
}

Label combine(const Label& a, const Label& b, int sign) {
    Label r = a;
    for (const auto& p : b) {
        bool found = false;
        for (auto& q : r) {
            if (q.first == p.first) {
                q.second += sign * p.second;
                found = true;
                break;
            }
        }
        if (!found) r.push_back({p.first, sign * p.second});
    }
    Label out;
    for (const auto& p : r)
        if (p.second != 0) out.push_back(p);
    return out;
}

Value labelFactorOf(const Label& l) {
    Value f = Value::integer(1);
    for (const auto& p : l) {
        Value uf;
        Dim d;
        if (!lookupUnit(p.first, uf, d)) fail("unknown unit '" + p.first + "'");
        f = vmul(f, vpow(uf, Value::integer(p.second)));
    }
    return f;
}

Dim labelDim(const Label& l) {
    Dim r;
    for (const auto& p : l) {
        Value uf;
        Dim d;
        if (lookupUnit(p.first, uf, d)) r = r + d.scaled(p.second);
    }
    return r;
}

std::string displayLabel(const Label& l) {
    std::string top, bottom;
    int nb = 0;
    for (const auto& p : l) {
        if (p.second > 0) {
            if (!top.empty()) top += "*";
            top += p.first + (p.second != 1 ? "^" + std::to_string(p.second) : "");
        } else {
            if (!bottom.empty()) bottom += "*";
            bottom += p.first + (p.second != -1 ? "^" + std::to_string(-p.second) : "");
            nb++;
        }
    }
    if (bottom.empty()) return top;
    if (top.empty()) top = "1";
    return top + "/" + (nb > 1 ? "(" + bottom + ")" : bottom);
}

const char* const kBaseNames[7] = {"m", "kg", "s", "A", "K", "mol", "cd"};

// The SI label for a dimension: a named derived unit when one matches exactly, else base units.
Label siLabel(const Dim& d) {
    static const char* derived[] = {"N", "J", "W", "Pa", "C", "V", "Ω", "F", "T", "Wb", "H", "S", "Hz"};
    for (const char* n : derived) {
        Value f;
        Dim ud;
        lookupUnit(n, f, ud);
        if (ud == d) return {{n, 1}};
    }
    Label l;
    for (int i = 0; i < 7; i++)
        if (d.e[i]) l.push_back({kBaseNames[i], d.e[i]});
    return l;
}

bool isDimensionlessUnit(const std::string& n) { return n == "rad" || n == "sr" || n == "°" || n == "deg"; }

bool labelIsAngle(const Label& l) {
    if (l.empty()) return false;
    for (const auto& p : l)
        if (!isDimensionlessUnit(p.first)) return false;
    return true;
}

ObjQuantity* asQ(const Value& v) { return as<ObjQuantity>(v); }

// Re-labels SI-only labels that combine several units into a single derived name: kg*m/s^2 -> N.
Label tidy(const Label& l, const Dim& d) {
    if (l.size() <= 1) return l;
    // Different units of the same dimension (km and m, h and s) are clearer in SI.
    for (size_t i = 0; i < l.size(); i++) {
        for (size_t j = i + 1; j < l.size(); j++) {
            Value f;
            Dim di, dj;
            lookupUnit(l[i].first, f, di);
            lookupUnit(l[j].first, f, dj);
            if (di == dj && !di.isNone()) return siLabel(d);
        }
    }
    Value f = labelFactorOf(l);
    if (!(isExactNumber(f) && numIsOne(f))) return l;
    Label si = siLabel(d);
    if (si.size() == 1) return si;
    return l;
}

// ---------------------------------------------------------------- unit text parsing

struct UnitParser {
    std::vector<std::string> toks;
    size_t pos = 0;

    explicit UnitParser(const std::string& text) {
        size_t i = 0;
        while (i < text.size()) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c == ' ') {
                i++;
                continue;
            }
            size_t start = i;
            uint32_t cp = utf8::decode(text, i);
            if (cp == '*' || cp == '/' || cp == '(' || cp == ')' || cp == '^' || cp == '-' || cp == 0x00B7 ||
                cp == 0x22C5 || cp == 0x2212) {
                std::string t = text.substr(start, i - start);
                if (cp == 0x00B7 || cp == 0x22C5) t = "*";
                if (cp == 0x2212) t = "-";
                toks.push_back(t);
                continue;
            }
            if (c >= '0' && c <= '9') {
                while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.')) i++;
                toks.push_back(text.substr(start, i - start));
                continue;
            }
            while (i < text.size()) {
                size_t save = i;
                uint32_t n = utf8::decode(text, i);
                if (n == ' ' || n == '*' || n == '/' || n == '(' || n == ')' || n == '^' || n == 0x00B7 || n == 0x22C5) {
                    i = save;
                    break;
                }
            }
            toks.push_back(text.substr(start, i - start));
        }
    }

    bool at(const char* t) const { return pos < toks.size() && toks[pos] == t; }

    // Returns the label; numeric factors such as the 1 in "1/s" multiply `scale`.
    Label expr(Value& scale) {
        Label l = term(scale);
        for (;;) {
            if (at("*")) {
                pos++;
                l = combine(l, term(scale), 1);
            } else if (at("/")) {
                pos++;
                Value s2 = Value::integer(1);
                Label r = term(s2);
                scale = vdiv(scale, s2);
                l = combine(l, r, -1);
            } else if (pos < toks.size() && !at(")")) {
                l = combine(l, term(scale), 1);
            } else {
                return l;
            }
        }
    }

    Label term(Value& scale) {
        Value s = Value::integer(1);
        Label l = factor(s);
        if (at("^")) {
            pos++;
            int sign = 1;
            if (at("-")) {
                sign = -1;
                pos++;
            }
            if (pos >= toks.size()) fail("expected an exponent in unit");
            int k = std::stoi(toks[pos++]) * sign;
            for (auto& p : l) p.second *= k;
            s = vpow(s, Value::integer(k));
        }
        scale = vmul(scale, s);
        return l;
    }

    Label factor(Value& scale) {
        if (pos >= toks.size()) fail("incomplete unit");
        const std::string& t = toks[pos];
        if (t == "(") {
            pos++;
            Label l = expr(scale);
            if (!at(")")) fail("missing ')' in unit");
            pos++;
            return l;
        }
        if (!t.empty() && t[0] >= '0' && t[0] <= '9') {
            pos++;
            scale = vmul(scale, t.find('.') != std::string::npos ? Value::real(std::stod(t))
                                                                  : Value::integer(std::stoll(t)));
            return {};
        }
        if ((t == "°" || t == "deg") && pos + 1 < toks.size() && (toks[pos + 1] == "C" || toks[pos + 1] == "F"))
            fail("temperature scales with an offset (°C, °F) are not units; use [K], e.g. (c + 273.15)[K]");
        Value f;
        Dim d;
        if (!lookupUnit(t, f, d)) fail("unknown unit '" + t + "'");
        pos++;
        return {{t, 1}};
    }
};

struct Parts {
    Value mag;
    Dim dim;
    Label label;
    bool isQ;
};

Parts partsOf(const Value& v) {
    if (isQuantity(v)) {
        ObjQuantity* q = asQ(v);
        return {q->mag, q->dim, decodeLabel(q->label), true};
    }
    return {v, Dim(), {}, false};
}

std::string describe(const Parts& p) {
    Label l = p.label.empty() ? siLabel(p.dim) : p.label;
    std::string s = displayLabel(l);
    return s.empty() ? "a plain number" : s;
}

}  // namespace

Value makeQuantity(Value mag, Dim dim, std::string label, Value labelFactor) {
    Label l = decodeLabel(label);
    if (dim.isNone() && !labelIsAngle(l)) return mag;
    if (dim.isNone() && l.empty()) return mag;
    return Value::object(Heap::instance().make<ObjQuantity>(mag, dim, std::move(label), labelFactor));
}

Value parseUnitLiteral(const std::string& text) {
    UnitParser p(text);
    if (p.toks.empty()) fail("empty unit");
    Value scale = Value::integer(1);
    Label l = p.expr(scale);
    if (p.pos != p.toks.size()) fail("unexpected '" + p.toks[p.pos] + "' in unit");
    Value factor = labelFactorOf(l);
    return makeQuantity(vmul(scale, factor), labelDim(l), encodeLabel(l), factor);
}

Value quantityBinary(BinOp op, const Value& a, const Value& b) {
    if ((isList(a) || isMatrix(a)) || (isList(b) || isMatrix(b))) fail("internal: quantity with a collection");
    if (isSym(a) || isSym(b)) {
        // Allow symbolic constants such as π as plain numbers.
        double d;
        if (isSym(a) && !symToDouble(a, d)) fail("quantities with units cannot be mixed with symbolic expressions");
        if (isSym(b) && !symToDouble(b, d)) fail("quantities with units cannot be mixed with symbolic expressions");
    }
    Parts x = partsOf(a), y = partsOf(b);
    switch (op) {
        case BinOp::Add:
        case BinOp::Sub:
        case BinOp::Mod: {
            if (!x.isQ && isZeroValue(x.mag) && op != BinOp::Mod)
                return op == BinOp::Add ? b : quantityNeg(b);
            if (!y.isQ && isZeroValue(y.mag)) return a;
            if (x.dim != y.dim)
                fail(std::string("cannot ") + (op == BinOp::Add ? "add" : op == BinOp::Sub ? "subtract" : "take %") +
                     " quantities with different dimensions (" + describe(x) + " and " + describe(y) + ")");
            Value m = binaryOp(op, x.mag, y.mag);
            const Parts& lab = x.isQ ? x : y;
            return makeQuantity(m, lab.dim, encodeLabel(lab.label), labelFactorOf(lab.label));
        }
        case BinOp::Mul:
        case BinOp::Dot:
        case BinOp::Cross:
        case BinOp::Div:
        case BinOp::LDiv: {
            bool divide = op == BinOp::Div;
            if (op == BinOp::LDiv) std::swap(x, y), divide = true;
            Value m = binaryOp(divide ? BinOp::Div : (op == BinOp::Mul ? BinOp::Mul : op), x.mag, y.mag);
            Dim d = divide ? x.dim - y.dim : x.dim + y.dim;
            Label l = tidy(combine(x.label, y.label, divide ? -1 : 1), d);
            if (labelDim(l) != d) l = siLabel(d);
            return makeQuantity(m, d, encodeLabel(l), labelFactorOf(l));
        }
        case BinOp::Pow: {
            if (y.isQ) fail("exponents cannot have units");
            Value e = y.mag;
            if (isSym(e)) fail("quantity exponents must be numbers");
            Value m = binaryOp(BinOp::Pow, x.mag, e);
            if (isIntegerValue(e)) {
                int k = static_cast<int>(toInt64(e, "an integer exponent"));
                Label l = x.label;
                for (auto& p : l) p.second *= k;
                Dim d = x.dim.scaled(k);
                return makeQuantity(m, d, encodeLabel(l), labelFactorOf(l));
            }
            if (isExactNumber(e)) {
                Rational r = toRational(e);
                if (!r.num.fitsInt64() || !r.den.fitsInt64()) fail("quantity exponent too large");
                int num = static_cast<int>(r.num.toInt64()), den = static_cast<int>(r.den.toInt64());
                Dim d = x.dim.scaled(num);
                if (!d.divisibleBy(den)) fail("this root of a quantity has fractional dimensions");
                d = d.divided(den);
                Label l;
                bool ok = true;
                for (const auto& p : x.label) {
                    if ((p.second * num) % den) ok = false;
                    l.push_back({p.first, p.second * num / den});
                }
                if (!ok || labelDim(l) != d) l = siLabel(d);
                return makeQuantity(m, d, encodeLabel(l), labelFactorOf(l));
            }
            fail("quantities can only be raised to exact powers");
        }
    }
    fail("unsupported operation on quantities");
}

int quantityCompare(const Value& a, const Value& b) {
    Parts x = partsOf(a), y = partsOf(b);
    if (x.dim != y.dim) fail("cannot compare quantities with different dimensions (" + describe(x) + " and " + describe(y) + ")");
    Value c = compareOp(CmpOp::Lt, x.mag, y.mag);
    if (isTruthy(c)) return -1;
    return isTruthy(compareOp(CmpOp::Gt, x.mag, y.mag)) ? 1 : 0;
}

bool quantityEqual(const Value& a, const Value& b) {
    Parts x = partsOf(a), y = partsOf(b);
    if (x.dim != y.dim) return false;
    if (isNumber(x.mag) && isNumber(y.mag) && (!isExactNumber(x.mag) || !isExactNumber(y.mag))) {
        double p = toDouble(x.mag), q = toDouble(y.mag);
        return std::fabs(p - q) <= 1e-12 * std::max(std::fabs(p), std::fabs(q));
    }
    return valuesEqual(x.mag, y.mag);
}

Value quantityNeg(const Value& a) {
    ObjQuantity* q = asQ(a);
    return makeQuantity(negateValue(q->mag), q->dim, q->label, q->labelFactor);
}

Value convertTo(const Value& x, const Value& unit) {
    if (isList(x)) return mapElements(x, [&](const Value& e) { return convertTo(e, unit); });
    if (!isQuantity(unit)) {
        if (isNumber(unit)) fail("'to' needs a unit on the right, like x to [km]");
        fail("'to' needs a unit on the right, got " + typeName(unit));
    }
    ObjQuantity* u = asQ(unit);
    Parts p = partsOf(x);
    if (p.dim != u->dim)
        fail("cannot convert " + describe(p) + " to " + displayLabel(decodeLabel(u->label)) + " (different dimensions)");
    if (!numIsOne(u->mag) && !valuesEqual(u->mag, u->labelFactor))
        fail("the unit after 'to' must not have a numeric factor");
    return Value::object(Heap::instance().make<ObjQuantity>(p.mag, u->dim, u->label, u->labelFactor));
}

std::string dimToString(const Dim& d) { return displayLabel(siLabel(d)); }

namespace {

std::string formatMagnitude(const Value& v) {
    if (isList(v)) {
        std::string s = "[";
        const auto& items = as<ObjList>(v)->items;
        for (size_t i = 0; i < items.size(); i++) {
            if (i) s += ", ";
            s += formatMagnitude(items[i]);
        }
        return s + "]";
    }
    if (isExactNumber(v)) {
        if (isIntegerValue(v)) return toString(v);
        return formatDouble(toDouble(v), false);
    }
    if (v.isReal()) return formatDouble(v.as.d, false);
    double d;
    if (isSym(v) && symToDouble(v, d)) {
        Value s = simplifyValue(v);
        if (isNumber(s)) return formatMagnitude(s);
        return formatDouble(d, false);
    }
    return toString(v);
}

}  // namespace

std::string formatQuantity(const ObjQuantity* q) {
    Label l = decodeLabel(q->label);
    Value factor = q->labelFactor;
    if (l.empty()) {
        l = siLabel(q->dim);
        factor = Value::integer(1);
    }
    Value shown = isList(q->mag) ? mapElements(q->mag, [&](const Value& e) { return vdiv(e, factor); }) : vdiv(q->mag, factor);
    std::string unit = displayLabel(l);
    if (unit == "°") return formatMagnitude(shown) + "°";
    return formatMagnitude(shown) + " " + unit;
}

Value dimensionlessMagnitude(const Value& q, const char* fname) {
    ObjQuantity* x = asQ(q);
    if (!x->dim.isNone())
        fail(std::string(fname) + " needs a dimensionless argument, got a quantity in " +
             displayLabel(decodeLabel(x->label).empty() ? siLabel(x->dim) : decodeLabel(x->label)));
    return x->mag;
}

namespace {

Value n_si(VM&, int, Value* args) {
    if (!isQuantity(args[0])) return args[0];
    ObjQuantity* q = asQ(args[0]);
    Label l = siLabel(q->dim);
    return Value::object(Heap::instance().make<ObjQuantity>(q->mag, q->dim, encodeLabel(l), Value::integer(1)));
}

Value n_magnitude(VM&, int, Value* args) {
    if (!isQuantity(args[0])) return args[0];
    ObjQuantity* q = asQ(args[0]);
    if (q->label.empty()) return q->mag;
    return isList(q->mag) ? mapElements(q->mag, [&](const Value& e) { return vdiv(e, q->labelFactor); })
                          : vdiv(q->mag, q->labelFactor);
}

Value n_unitof(VM&, int, Value* args) {
    if (!isQuantity(args[0])) return newString("");
    ObjQuantity* q = asQ(args[0]);
    Label l = decodeLabel(q->label);
    if (l.empty()) l = siLabel(q->dim);
    return newString(displayLabel(l));
}

Value n_dimension(VM&, int, Value* args) {
    static const char* names[7] = {"L", "M", "T", "I", "Θ", "N", "J"};
    if (!isQuantity(args[0])) return newString("1");
    const Dim& d = asQ(args[0])->dim;
    std::string s;
    for (int i = 0; i < 7; i++) {
        if (!d.e[i]) continue;
        if (!s.empty()) s += " ";
        s += names[i];
        if (d.e[i] != 1) s += "^" + std::to_string(d.e[i]);
    }
    return newString(s.empty() ? "1" : s);
}

Value n_unit(VM&, int, Value* args) {
    if (!isString(args[0])) fail("unit expects a string such as \"km/h\"");
    return parseUnitLiteral(as<ObjString>(args[0])->s);
}

Value physical(const char* value, const char* unit) {
    Value u = parseUnitLiteral(unit);
    Value mag = fromRational(exactDecimal(value));
    ObjQuantity* q = asQ(u);
    return Value::object(Heap::instance().make<ObjQuantity>(vmul(mag, q->labelFactor), q->dim, q->label, q->labelFactor));
}

Value n_constant(VM&, int, Value* args) {
    if (!isString(args[0])) fail("constant expects a name string, e.g. constant(\"G\")");
    const std::string& n = as<ObjString>(args[0])->s;
    if (n == "c") return physical("299792458", "m/s");
    if (n == "G") return physical("6.67430e-11", "m^3/(kg*s^2)");
    if (n == "h") return physical("6.62607015e-34", "J*s");
    if (n == "hbar" || n == "ħ") {
        Value h = physical("6.62607015e-34", "J*s");
        return vdiv(h, vmul(Value::integer(2), Value::real(M_PI)));
    }
    if (n == "e" || n == "qe") return physical("1.602176634e-19", "C");
    if (n == "k" || n == "kB") return physical("1.380649e-23", "J/K");
    if (n == "NA") return physical("6.02214076e23", "mol^-1");
    if (n == "R") return physical("8.314462618", "J/(mol*K)");
    if (n == "me") return physical("9.1093837015e-31", "kg");
    if (n == "mp") return physical("1.67262192369e-27", "kg");
    if (n == "eps0" || n == "ε0") return physical("8.8541878128e-12", "F/m");
    if (n == "mu0" || n == "μ0") return physical("1.25663706212e-6", "N/A^2");
    if (n == "g" || n == "g0") return physical("9.80665", "m/s^2");
    if (n == "sigma" || n == "σ") return physical("5.670374419e-8", "W/(m^2*K^4)");
    if (n == "atm") return physical("101325", "Pa");
    fail("unknown constant '" + n + "' (try c, G, h, hbar, e, k, NA, R, me, mp, eps0, mu0, g, sigma, atm)");
}

}  // namespace

void registerUnits(VM& vm) {
    const char* c = "units";
    vm.defineNative("si", 1, 1, n_si, "si(q) - express a quantity in SI units", c);
    vm.defineNative("magnitude", 1, 1, n_magnitude, "magnitude(q) - the number in the quantity's own unit", c);
    vm.defineNative("unitof", 1, 1, n_unitof, "unitof(q) - the unit of q as text", c);
    vm.defineNative("dimension", 1, 1, n_dimension, "dimension(q) - dimension such as L T^-1", c);
    vm.defineNative("unit", 1, 1, n_unit, "unit(\"km/h\") - same as the literal [km/h]", c);
    vm.defineNative("constant", 1, 1, n_constant, "constant(name) - physical constant with units, e.g. constant(\"G\")", c);
}
