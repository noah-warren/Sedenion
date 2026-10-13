#include "cas/expr.h"

#include "numeric/ops.h"

namespace cas {

namespace {

std::string lx(const E& e);

std::string atom(const E& e) {
    switch (e->kind) {
        case Kind::Num:
        case Kind::Sym:
        case Kind::Fn: return lx(e);
        case Kind::Pow:
            if (e->args[1] == half()) return lx(e);
            return "(" + lx(e) + ")";
        default: return "(" + lx(e) + ")";
    }
}

std::string lx(const E& e) {
    switch (e->kind) {
        case Kind::Num:
            if (e->exact && !e->q.isInteger()) {
                std::string n = e->q.num.toString(), d = e->q.den.toString();
                if (!n.empty() && n[0] == '-') return "-\\frac{" + n.substr(1) + "}{" + d + "}";
                return "\\frac{" + n + "}{" + d + "}";
            }
            return e->exact ? e->q.toString() : formatDouble(e->d, true);
        case Kind::Sym:
            if (e->name == PI_NAME) return "\\pi";
            if (e->name == I_NAME) return "i";
            {
                std::string s;
                for (char c : e->name) {
                    if (c == '_' || c == '%' || c == '&' || c == '#') s += '\\';
                    s += c;
                }
                return s;
            }
        case Kind::Add: {
            std::string s;
            for (size_t i = 0; i < e->args.size(); i++) {
                const E& t = e->args[i];
                if (i && isNegativeTerm(t))
                    s += " - " + lx(neg(t));
                else if (i)
                    s += " + " + lx(t);
                else
                    s += lx(t);
            }
            return s;
        }
        case Kind::Mul: {
            E coeff = one();
            std::vector<E> top, bottom;
            for (const auto& f : e->args) {
                if (isNum(f))
                    coeff = numMulE(coeff, f);
                else if (f->kind == Kind::Pow && isNegativeTerm(f->args[1]))
                    bottom.push_back(pow(f->args[0], neg(f->args[1])));
                else
                    top.push_back(f);
            }
            auto join = [](const std::vector<E>& fs) {
                std::string s;
                for (size_t i = 0; i < fs.size(); i++) {
                    if (i) s += " ";
                    s += atom(fs[i]);
                }
                return s;
            };
            std::string num = join(top);
            bool neg = isNegativeNum(coeff);
            if (neg) coeff = numMulE(coeff, minusOne());
            if (!(isOne(coeff) && !num.empty())) {
                std::string c = lx(coeff);
                num = num.empty() ? c : c + " " + num;
            }
            if (num.empty()) num = "1";
            std::string s = bottom.empty() ? num : "\\frac{" + num + "}{" + join(bottom) + "}";
            return neg ? "-" + s : s;
        }
        case Kind::Pow:
            if (isNegativeTerm(e->args[1]))
                return "\\frac{1}{" + atom(pow(e->args[0], neg(e->args[1]))) + "}";
            if (e->args[1] == half()) return "\\sqrt{" + lx(e->args[0]) + "}";
            return atom(e->args[0]) + "^{" + lx(e->args[1]) + "}";
        case Kind::Fn: {
            std::string name = e->name;
            if (name == "asin") name = "arcsin";
            if (name == "acos") name = "arccos";
            if (name == "atan") name = "arctan";
            std::string s = "\\" + name + "\\left(";
            for (size_t i = 0; i < e->args.size(); i++) {
                if (i) s += ", ";
                s += lx(e->args[i]);
            }
            return s + "\\right)";
        }
        case Kind::Eq: return lx(e->args[0]) + " = " + lx(e->args[1]);
    }
    return "?";
}

}  // namespace

std::string toLatex(const E& e) { return lx(e); }

}  // namespace cas
