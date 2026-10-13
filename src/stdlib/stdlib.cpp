#include "stdlib/stdlib.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <ostream>

#include "calculus/numeric_calc.h"
#include "cas/bridge.h"
#include "cas/expr.h"
#include "cas/rewrite.h"
#include "common/error.h"
#include "ga/ga.h"
#include "hyper/hyper.h"
#include "linalg/matrix.h"
#include "numeric/ops.h"
#include "sets/set.h"
#include "units/units.h"
#include "vm/vm.h"

namespace {

Value n_help(VM& vm, int argc, Value* args) {
    std::ostream& os = *vm.out;
    if (argc == 0) {
        std::map<std::string, std::vector<std::string>> byCat;
        for (const ObjNative* n : vm.natives()) byCat[n->category].push_back(n->name);
        os << "Sedenion builtins by category (help(name) for details):\n";
        for (auto& [cat, names] : byCat) {
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            os << "  " << cat << ":";
            size_t col = cat.size() + 3;
            for (const auto& name : names) {
                if (col + name.size() + 1 > 78) {
                    os << "\n     ";
                    col = 5;
                }
                os << " " << name;
                col += name.size() + 1;
            }
            os << "\n";
        }
        os << "Constants: π pi e i ∞ inf nan φ τ tau ° ∅\n";
        os << "Syntax: help(\"syntax\")\n";
        return Value::nil();
    }
    std::string name;
    if (isString(args[0])) {
        name = as<ObjString>(args[0])->s;
    } else if (isNative(args[0])) {
        name = as<ObjNative>(args[0])->name;
    } else if (isClosure(args[0])) {
        const ObjFunction* f = as<ObjClosure>(args[0])->fn;
        std::string count;
        if (f->hasRest)
            count = "at least " + std::to_string(f->minArity) + (f->minArity == 1 ? " argument" : " arguments");
        else if (f->minArity != f->arity)
            count = std::to_string(f->minArity) + " to " + std::to_string(f->arity) + " arguments";
        else
            count = std::to_string(f->arity) + " argument" + (f->arity == 1 ? "" : "s");
        os << f->name << ": user function of " << count << "\n";
        return Value::nil();
    } else {
        os << typeName(args[0]) << ": " << repr(args[0]) << "\n";
        return Value::nil();
    }
    if (name == "syntax") {
        os <<               "  f(x) = x^2 + 1          function definition\n"
              "  f(x, n = 1) = n*x       optional parameter; the default runs in the function\n"
              "  fn f(x, ...rest) ... end  extra arguments are a list named rest\n"
              "  g = x -> 2x + 3         lambda (2x is implicit multiplication)\n"
              "  fn name(a, b) ... end   block function; return, implicit last value\n"
              "  point.norm()            method call; point is the first argument\n"
              "  if c then ... elif c then ... else ... end\n"
              "  while c do ... end ;  for k in 1..10 do ... end\n"
              "  [1, 2, 3]  [1, 2; 3, 4]  {1, 2}  1..10  0..1 step 1/10\n"
              "  [x^2 | x ∈ 1..10, x % 2 == 0]   {…} for a set comprehension\n"
              "  ∑(k = 1..n) k^2   ∏(k = 1..n) k   sum(k^2, k = 1..n)\n"
              "  sym x, y ; diff(x^3, x) ; solve(x^2 == 4, x)\n"
              "  100[km] / 2[h] to [m/s]\n"
              "  |x|  √x  n!  A'  A^-1  a ∈ S  A ∪ B  A ∩ B  A \\ B  u · v  u × v\n"
              "  {a: 1, b: 2}  r.a  r.f()  {:}   records and methods;  \"hello $name\"  \"${1 + 2}\"\n"
              "  import \"lib.sed\" as M ; export f\n"
              "  rule name  a^2 - b^2 => (a - b)*(a + b)  end ;  rewrite(e)\n"
              "  assume x > 0 ;  working(expr).text\n";
        return Value::nil();
    }
    for (const ObjNative* n : vm.natives()) {
        if (n->name == name) {
            os << n->name << " [" << n->category << "]: " << n->doc << "\n";
            return Value::nil();
        }
    }
    os << "no help for '" << name << "'\n";
    return Value::nil();
}

}  // namespace

void registerStdlib(VM& vm) {
    registerCore(vm);
    registerMath(vm);
    registerSets(vm);
    registerLinalg(vm);
    registerCalculus(vm);
    registerCas(vm);
    registerUnits(vm);
    registerHyper(vm);
    registerGa(vm);
    registerRewrite(vm);
    vm.defineNative("help", 0, 1, n_help, "help() - list builtins; help(name) - describe one", "core");

    Value pi = newSym(cas::piConst());
    vm.setGlobal("π", pi);
    vm.setGlobal("pi", pi);
    Value tau = newSym(cas::mul(cas::num(2), cas::piConst()));
    vm.setGlobal("τ", tau);
    vm.setGlobal("tau", tau);
    vm.setGlobal("e", newSym(cas::eConst()));
    vm.setGlobal("i", fromComplex(std::complex<double>(0, 1)));
    vm.setGlobal("inf", Value::real(INFINITY));
    vm.setGlobal("∞", Value::real(INFINITY));
    vm.setGlobal("nan", Value::real(NAN));
    vm.setGlobal("φ", Value::real((1 + std::sqrt(5.0)) / 2));
    vm.setGlobal("phi", Value::real((1 + std::sqrt(5.0)) / 2));
    vm.setGlobal("argv", newList());
    vm.setGlobal("°", newSym(cas::div(cas::piConst(), cas::num(180))));
}
