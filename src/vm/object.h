#pragma once

#include <complex>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "compiler/chunk.h"
#include "numeric/bigint.h"
#include "numeric/rational.h"
#include "units/dim.h"
#include "vm/value.h"

namespace cas {
struct Expr;
using E = std::shared_ptr<const Expr>;
}  // namespace cas

class VM;

enum class ObjType : uint8_t {
    String,
    BigInt,
    Rational,
    Complex,
    List,
    Matrix,
    Set,
    Range,
    Function,
    Closure,
    Native,
    Upvalue,
    Sym,
    Quantity,
    Hyper,
    Record,
    Multi,
    Radical
};

// A scope for global names. Modules each have one; lookups that miss fall through to builtins.
struct GlobalEnv {
    std::unordered_map<int, Value> slots;
};

struct Obj {
    ObjType type;
    bool marked = false;
    Obj* next = nullptr;
    explicit Obj(ObjType t) : type(t) {}
    virtual ~Obj() = default;
};

struct ObjString : Obj {
    std::string s;
    explicit ObjString(std::string v) : Obj(ObjType::String), s(std::move(v)) {}
};

struct ObjBigInt : Obj {
    BigInt v;
    explicit ObjBigInt(BigInt b) : Obj(ObjType::BigInt), v(std::move(b)) {}
};

struct ObjRational : Obj {
    Rational v;
    explicit ObjRational(Rational r) : Obj(ObjType::Rational), v(std::move(r)) {}
};

struct ObjComplex : Obj {
    std::complex<double> v;
    explicit ObjComplex(std::complex<double> c) : Obj(ObjType::Complex), v(c) {}
};

struct ObjList : Obj {
    std::vector<Value> items;
    explicit ObjList(std::vector<Value> v) : Obj(ObjType::List), items(std::move(v)) {}
};

struct ObjMatrix : Obj {
    size_t rows, cols;
    std::vector<Value> data;  // row-major
    ObjMatrix(size_t r, size_t c, std::vector<Value> d) : Obj(ObjType::Matrix), rows(r), cols(c), data(std::move(d)) {}
    Value& at(size_t i, size_t j) { return data[i * cols + j]; }
    const Value& at(size_t i, size_t j) const { return data[i * cols + j]; }
};

// Elements are kept sorted by compareValues and contain no duplicates.
struct ObjSet : Obj {
    std::vector<Value> items;
    explicit ObjSet(std::vector<Value> v) : Obj(ObjType::Set), items(std::move(v)) {}
};

// Inclusive arithmetic progression start, start+step, ... up to stop.
struct ObjRange : Obj {
    Value start, stop, step;
    ObjRange(Value a, Value b, Value s) : Obj(ObjType::Range), start(a), stop(b), step(s) {}
};

struct ObjFunction : Obj {
    std::string name;
    int arity = 0;     // parameter slots, including the rest parameter when hasRest
    int minArity = 0;  // how many arguments the caller must supply
    bool hasRest = false;
    int numSlots = 1;  // slot 0 holds the closure itself
    int upvalueCount = 0;
    Chunk chunk;
    std::vector<std::string> slotNames;
    std::string source;  // file the function was compiled from ("" or "<repl>" when not a file)
    ObjFunction() : Obj(ObjType::Function) {}
};

struct ObjUpvalue : Obj {
    Value* location;
    Value closed;
    ObjUpvalue* nextOpen = nullptr;
    explicit ObjUpvalue(Value* loc) : Obj(ObjType::Upvalue), location(loc) {}
};

struct ObjClosure : Obj {
    ObjFunction* fn;
    std::vector<ObjUpvalue*> upvalues;
    GlobalEnv* env = nullptr;  // globals this function reads and writes
    explicit ObjClosure(ObjFunction* f)
        : Obj(ObjType::Closure), fn(f), upvalues(static_cast<size_t>(f->upvalueCount), nullptr) {}
};

using NativeFn = Value (*)(VM& vm, int argc, Value* args);

struct ObjNative : Obj {
    std::string name;
    int minArgs;
    int maxArgs;  // -1 = variadic
    NativeFn fn;
    std::string doc;
    std::string category;
    ObjNative(std::string n, int mn, int mx, NativeFn f, std::string d, std::string c)
        : Obj(ObjType::Native), name(std::move(n)), minArgs(mn), maxArgs(mx), fn(f), doc(std::move(d)),
          category(std::move(c)) {}
};

struct ObjSym : Obj {
    cas::E e;
    explicit ObjSym(cas::E x) : Obj(ObjType::Sym), e(std::move(x)) {}
};

// A physical quantity. `mag` is in SI base units; `label`/`labelFactor` remember the unit
// the user wrote (e.g. km with factor 1000) so the value can be shown the same way.
struct ObjQuantity : Obj {
    Value mag;
    Dim dim;
    std::string label;
    Value labelFactor;
    ObjQuantity(Value m, Dim d, std::string l, Value f)
        : Obj(ObjType::Quantity), mag(m), dim(d), label(std::move(l)), labelFactor(f) {}
};

// A Cayley-Dickson hypercomplex number with 2^n real components (4: quaternion, 8: octonion,
// 16: sedenion). Component 0 is the real part; the components may be exact or symbolic.
struct ObjHyper : Obj {
    std::vector<Value> c;
    explicit ObjHyper(std::vector<Value> v) : Obj(ObjType::Hyper), c(std::move(v)) {}
};

// Insertion-ordered map. Keys may be any value; field access uses string keys.
struct ObjRecord : Obj {
    std::vector<Value> keys;
    std::vector<Value> vals;
    bool isModule = false;  // an import namespace: field calls pass only the written arguments
    ObjRecord() : Obj(ObjType::Record) {}
};

// Multivector in Cl(p,q), components in binary blade order (bit i is e_{i+1}). At most 6 generators.
struct ObjMulti : Obj {
    int p = 0, q = 0;
    std::vector<Value> c;
    ObjMulti(int pp, int qq, std::vector<Value> v) : Obj(ObjType::Multi), p(pp), q(qq), c(std::move(v)) {}
};

// Exact sum of rational multiples of square roots of square-free integers.
// terms are sorted by radicand; a pure rational is never stored here.
struct ObjRadical : Obj {
    std::vector<std::pair<BigInt, Rational>> terms;
    explicit ObjRadical(std::vector<std::pair<BigInt, Rational>> t) : Obj(ObjType::Radical), terms(std::move(t)) {}
};

// Every heap object is linked into a single list so the mark-and-sweep collector can find it.
class Heap {
public:
    static Heap& instance() {
        static Heap* h = new Heap();
        return *h;
    }
    template <class T, class... Args>
    T* make(Args&&... args) {
        T* o = new T(std::forward<Args>(args)...);
        o->next = head;
        head = o;
        ++count;
        return o;
    }
    Obj* head = nullptr;
    size_t count = 0;
    size_t threshold = 200000;
};

inline bool isObjType(const Value& v, ObjType t) { return v.type == ValType::Obj && v.as.o->type == t; }
template <class T>
inline T* as(const Value& v) {
    return static_cast<T*>(v.as.o);
}

inline bool isString(const Value& v) { return isObjType(v, ObjType::String); }
inline bool isList(const Value& v) { return isObjType(v, ObjType::List); }
inline bool isMatrix(const Value& v) { return isObjType(v, ObjType::Matrix); }
inline bool isSet(const Value& v) { return isObjType(v, ObjType::Set); }
inline bool isRange(const Value& v) { return isObjType(v, ObjType::Range); }
inline bool isSym(const Value& v) { return isObjType(v, ObjType::Sym); }
inline bool isQuantity(const Value& v) { return isObjType(v, ObjType::Quantity); }
inline bool isHyper(const Value& v) { return isObjType(v, ObjType::Hyper); }
inline bool isRecord(const Value& v) { return isObjType(v, ObjType::Record); }
inline bool isMulti(const Value& v) { return isObjType(v, ObjType::Multi); }
inline bool isRadical(const Value& v) { return isObjType(v, ObjType::Radical); }
inline bool isClosure(const Value& v) { return isObjType(v, ObjType::Closure); }
inline bool isNative(const Value& v) { return isObjType(v, ObjType::Native); }
inline bool isCallable(const Value& v) { return isClosure(v) || isNative(v); }

Value newString(std::string s);
Value newList(std::vector<Value> items = {});
Value newMatrix(size_t rows, size_t cols, std::vector<Value> data);
Value newSet(std::vector<Value> items);  // sorts and removes duplicates
Value newRange(Value start, Value stop, Value step);
Value newSym(cas::E e);

std::string typeName(const Value& v);
std::string toString(const Value& v);  // strings unquoted at top level
std::string repr(const Value& v);      // strings quoted
std::string display(const Value& v);   // like toString, but matrices are laid out on several lines
bool valuesEqual(const Value& a, const Value& b);
int compareValues(const Value& a, const Value& b);  // total order used by sets
bool isTruthy(const Value& v);

Value newRecord();
int recordFind(const Value& rec, const Value& key);  // -1 when absent
bool recordHas(const Value& rec, const Value& key);
Value recordGet(const Value& rec, const Value& key);
void recordSet(const Value& rec, const Value& key, const Value& val);
