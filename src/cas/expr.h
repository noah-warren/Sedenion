#pragma once

#include <complex>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/error.h"
#include "numeric/rational.h"

namespace cas {

// Expression trees are immutable and hash-consed: structurally equal trees share one node,
// so equality is pointer equality. Every constructor below returns an automatically
// simplified, canonically ordered expression.
enum class Kind : uint8_t { Num, Sym, Add, Mul, Pow, Fn, Eq };

struct Expr;
using E = std::shared_ptr<const Expr>;

struct Expr {
    Kind kind = Kind::Num;
    bool exact = true;  // Num: exact rational `q`, otherwise floating-point `d`
    Rational q;
    double d = 0;
    std::string name;  // Sym and Fn
    std::vector<E> args;
    size_t hash = 0;
};

struct CasError : SedError {
    using SedError::SedError;
};

// Names of the special symbols.
extern const char* const PI_NAME;
extern const char* const E_NAME;
extern const char* const I_NAME;

// ---- construction ----
E num(const Rational& q);
E num(int64_t v);
E real(double d);
E sym(const std::string& name);
E add(std::vector<E> terms);
E add(const E& a, const E& b);
E sub(const E& a, const E& b);
E mul(std::vector<E> factors);
E mul(const E& a, const E& b);
E div(const E& a, const E& b);
E neg(const E& a);
E pow(const E& base, const E& exp);
E fn(const std::string& name, std::vector<E> args);
E fn(const std::string& name, const E& arg);
E eq(const E& lhs, const E& rhs);
E sqrt(const E& a);
E rebuild(const E& e, std::vector<E> args);

const E& zero();
const E& one();
const E& minusOne();
const E& half();
const E& piConst();
const E& eConst();
const E& imagUnit();

// ---- predicates ----
inline bool isNum(const E& e) { return e->kind == Kind::Num; }
bool isZero(const E& e);
bool isOne(const E& e);
bool isInteger(const E& e);      // exact integer
bool isRationalNum(const E& e);  // exact number
bool isNegativeNum(const E& e);
bool isNegativeTerm(const E& e);  // negative number, or product with a negative coefficient
bool isConstantSymbol(const E& e);  // π, e, i
bool isSymbol(const E& e);
double numValue(const E& e);
int compare(const E& a, const E& b);
bool freeOf(const E& e, const E& x);
std::vector<E> freeSymbols(const E& e);  // excludes π, e and i
int countNodes(const E& e);
bool isInexact(const E& e);  // contains a floating-point number

// Splits a term into numeric coefficient and the rest: 3x^2 -> (3, x^2), x -> (1, x).
E splitCoeff(const E& term, E& rest);
// Splits a factor into base and exponent: x^3 -> (x, 3), x -> (x, 1).
E splitPow(const E& factor, E& exponent);
// Exact rational arithmetic on numbers; mixes to floating point when either side is inexact.
E numAddE(const E& a, const E& b);
E numMulE(const E& a, const E& b);

// ---- transformation ----
E subs(const E& e, const E& x, const E& v);
E diff(const E& e, const E& x);
E expand(const E& e);
E together(const E& e);
void numDen(const E& e, E& num, E& den);
E cancel(const E& e);
E simplify(const E& e);
E factor(const E& e);
E apart(const E& e, const E& x);
E taylor(const E& e, const E& x, const E& a, int order);
E limit(const E& e, const E& x, const E& a);
std::vector<E> solve(const E& equation, const E& x);
using Solution = std::vector<std::pair<E, E>>;  // variable -> value
std::vector<Solution> solveSystem(const std::vector<E>& equations, const std::vector<E>& vars);
E integrate(const E& e, const E& x);  // nullptr when no antiderivative was found
E integrateDefinite(const E& e, const E& x, const E& a, const E& b);

// ---- numeric evaluation ----
using Env = std::function<bool(const std::string&, std::complex<double>&)>;
bool evalComplex(const E& e, const Env& env, std::complex<double>& out);
bool evalFnComplex(const std::string& name, const std::vector<std::complex<double>>& args,
                   std::complex<double>& out);
E evalf(const E& e);
std::function<double(double)> compileReal(const E& e, const std::string& var);

std::string toString(const E& e);
std::string toLatex(const E& e);

}  // namespace cas
