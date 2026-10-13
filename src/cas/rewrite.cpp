#include "cas/rewrite.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "cas/bridge.h"
#include "cas/steps.h"
#include "vm/vm.h"

static Value (*p_defrule)(VM&, int, Value*) = nullptr;
static Value (*p_rewrite)(VM&, int, Value*) = nullptr;
static Value (*p_assume)(VM&, int, Value*) = nullptr;
static Value (*p_forget)(VM&, int, Value*) = nullptr;
static Value (*p_check)(VM&, int, Value*) = nullptr;

namespace cas {

namespace {

enum class Rel { Lt, Le, Gt, Ge, Ne };

struct Assumption {
    E var;
    Rel rel;
    E bound;
};

std::vector<Assumption> g_assume;
std::vector<Rule> g_rules;

int signHint(const E& x) {
    if (!isSymbol(x) || isConstantSymbol(x)) return 0;
    for (const auto& a : g_assume) {
        if (a.var != x || !isZero(a.bound)) continue;
        if (a.rel == Rel::Gt || a.rel == Rel::Ge) return 1;
        if (a.rel == Rel::Lt || a.rel == Rel::Le) return -1;
    }
    return 0;
}

E applyRec(const E& e) {
    if (e->kind == Kind::Pow && e->args[1] == half() && e->args[0]->kind == Kind::Pow && isInteger(e->args[0]->args[1]) &&
        e->args[0]->args[1]->q == Rational(2)) {
        E u = applyRec(e->args[0]->args[0]);
        int s = signHint(u);
        if (s > 0) return u;
        if (s < 0) return neg(u);
    }
    if (e->args.empty()) return e;
    std::vector<E> args;
    for (const auto& a : e->args) args.push_back(applyRec(a));
    return rebuild(e, args);
}

bool matchComm(const std::vector<E>& es, const std::vector<E>& ps, std::vector<std::pair<std::string, E>>& env);

bool matchPat(const E& e, const E& p, std::vector<std::pair<std::string, E>>& env) {
    if (isSymbol(p) && !isConstantSymbol(p)) {
        for (const auto& kv : env)
            if (kv.first == p->name) return kv.second == e;
        env.push_back({p->name, e});
        return true;
    }
    if (!p || !e || p->kind != e->kind) return false;
    if (p->kind == Kind::Num || p->kind == Kind::Sym) return p == e;
    if (p->kind == Kind::Fn && p->name != e->name) return false;
    if (p->args.size() != e->args.size()) return false;
    if (p->kind == Kind::Add || p->kind == Kind::Mul) return matchComm(e->args, p->args, env);
    size_t mark = env.size();
    for (size_t i = 0; i < p->args.size(); i++) {
        if (!matchPat(e->args[i], p->args[i], env)) {
            env.resize(mark);
            return false;
        }
    }
    return true;
}

bool matchComm(const std::vector<E>& es, const std::vector<E>& ps, std::vector<std::pair<std::string, E>>& env) {
    std::vector<char> used(es.size(), 0);
    std::function<bool(size_t)> go = [&](size_t i) {
        if (i == ps.size()) return true;
        for (size_t j = 0; j < es.size(); j++) {
            if (used[j]) continue;
            size_t mark = env.size();
            if (!matchPat(es[j], ps[i], env)) {
                env.resize(mark);
                continue;
            }
            used[j] = 1;
            if (go(i + 1)) return true;
            used[j] = 0;
            env.resize(mark);
        }
        return false;
    };
    return go(0);
}

E substitute(E rep, const std::vector<std::pair<std::string, E>>& env) {
    for (const auto& kv : env) rep = subs(rep, sym(kv.first), kv.second);
    return rep;
}

E rewriteRec(const E& e, const std::vector<const Rule*>& rules, int depth) {
    if (depth > 24) return e;
    E cur = e;
    if (!cur->args.empty()) {
        std::vector<E> args;
        bool changed = false;
        for (const auto& a : cur->args) {
            E b = rewriteRec(a, rules, depth + 1);
            changed = changed || b != a;
            args.push_back(b);
        }
        if (changed) cur = rebuild(cur, args);
    }
    for (int guard = 0; guard < 12; guard++) {
        bool hit = false;
        for (const Rule* rule : rules) {
            std::vector<std::pair<std::string, E>> env;
            if (!matchPat(cur, rule->pattern, env)) continue;
            E rep = substitute(rule->replacement, env);
            if (rep == cur) continue;
            if (stepsWant(StepKind::Rewrite))
                stepNote(StepKind::Rewrite, rule->name + ": " + stepExpr(cur) + "  =>  " + stepExpr(rep));
            cur = rewriteRec(rep, rules, depth + 1);
            hit = true;
            break;
        }
        if (!hit) break;
    }
    return cur;
}

Value n_defrule(VM&, int, Value* args) {
    if (!isString(args[0])) fail("defrule: the name must be a string");
    defineRule(as<ObjString>(args[0])->s, toExpr(args[1]), toExpr(args[2]));
    return Value::nil();
}

Value n_rewrite(VM&, int argc, Value* args) {
    E e = toExpr(args[0]);
    std::vector<std::string> only;
    for (int i = 1; i < argc; i++) {
        if (!isString(args[i])) fail("rewrite: rule names must be strings");
        only.push_back(as<ObjString>(args[i])->s);
    }
    StepScope s(StepKind::Rewrite);
    if (s.active()) s.head("rewrite " + stepExpr(e));
    E r = rewriteExpr(e, only);
    if (r != e) s.done(r);
    return fromExpr(r);
}

Value n_assume(VM&, int, Value* args) {
    E var = toExpr(args[0]);
    if (!isSymbol(var) || isConstantSymbol(var)) fail("assume: the left side must be a symbol, such as x");
    if (!isString(args[1])) fail("assume: expected a relation");
    const std::string& op = as<ObjString>(args[1])->s;
    Rel rel;
    if (op == "<") rel = Rel::Lt;
    else if (op == "<=") rel = Rel::Le;
    else if (op == ">") rel = Rel::Gt;
    else if (op == ">=") rel = Rel::Ge;
    else if (op == "!=") rel = Rel::Ne;
    else fail("assume: the relation must be <, <=, >, >= or !=");
    E bound = toExpr(args[2]);
    for (auto& a : g_assume) {
        if (a.var == var && a.rel == rel) {
            a.bound = bound;
            return Value::nil();
        }
    }
    g_assume.push_back({var, rel, bound});
    return Value::nil();
}

Value n_forget(VM&, int argc, Value* args) {
    if (argc == 0) {
        g_assume.clear();
        return Value::nil();
    }
    E var = toExpr(args[0]);
    g_assume.erase(std::remove_if(g_assume.begin(), g_assume.end(), [&](const Assumption& a) { return a.var == var; }),
                   g_assume.end());
    return Value::nil();
}

bool nearZero(const E& e) {
    std::complex<double> z;
    if (!evalComplex(e, nullptr, z)) return false;
    return std::abs(z) <= 1e-8;
}

Value n_check(VM&, int, Value* args) {
    if (args[0].isBool()) return args[0];
    E e = toExpr(args[0]);
    E diff = e->kind == Kind::Eq ? simplify(sub(e->args[0], e->args[1])) : simplify(e);
    diff = applyAssumptions(diff);
    if (isZero(diff)) return Value::boolean(true);
    std::vector<E> syms = freeSymbols(diff);
    if (syms.size() > 3) syms.resize(3);
    if (syms.empty()) return Value::boolean(nearZero(diff));
    const int samples[] = {0, 1, -1, 2, -2};
    int n = static_cast<int>(syms.size());
    int total = 1;
    for (int i = 0; i < n; i++) total *= 5;
    int tried = 0;
    for (int k = 0; k < total; k++) {
        E cur = diff;
        int x = k;
        for (int i = 0; i < n; i++) {
            cur = subs(cur, syms[static_cast<size_t>(i)], num(samples[x % 5]));
            x /= 5;
        }
        cur = simplify(cur);
        std::complex<double> z;
        if (!evalComplex(cur, nullptr, z) || !std::isfinite(z.real()) || !std::isfinite(z.imag())) continue;
        tried++;
        if (std::abs(z) > 1e-6) return Value::boolean(false);
    }
    return Value::boolean(tried > 0);
}

struct Hook {
    Hook() {
        p_defrule = n_defrule;
        p_rewrite = n_rewrite;
        p_assume = n_assume;
        p_forget = n_forget;
        p_check = n_check;
    }
} hook;

void clearRewriteState() {
    g_assume.clear();
    g_rules.clear();
}

}  // namespace

E applyAssumptions(const E& e) { return g_assume.empty() ? e : applyRec(e); }

void resetRewriteState() { clearRewriteState(); }

void defineRule(const std::string& name, const E& pattern, const E& replacement) {
    g_rules.push_back(Rule{name, pattern, replacement});
}

E rewriteExpr(const E& e, const std::vector<std::string>& only) {
    std::vector<const Rule*> rules;
    for (const auto& r : g_rules) {
        if (!only.empty() && std::find(only.begin(), only.end(), r.name) == only.end()) continue;
        rules.push_back(&r);
    }
    return rewriteRec(e, rules, 0);
}

}  // namespace cas

void registerRewrite(VM& vm) {
    const char* c = "symbolic";
    vm.defineNative("defrule", 3, 3, p_defrule, "defrule(name, pattern, replacement) - register a rewrite rule", c);
    vm.defineNative("rewrite", 1, -1, p_rewrite, "rewrite(e, name, ...) - apply rewrite rules (all of them, if no name)", c);
    vm.defineNative("assume", 3, 3, p_assume, "assume(symbol, relation, bound) - used by the assume statement", c);
    vm.defineNative("forget", 0, 1, p_forget, "forget() - drop assumptions; forget(x) drops those about x", c);
    vm.defineNative("check", 1, 1, p_check,
                    "check(a == b) - true when the two sides agree symbolically or on sample points", c);
}
