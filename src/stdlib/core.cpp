#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <random>
#include <sstream>

#include "common/error.h"
#include "common/utf8.h"
#include "numeric/ops.h"
#include "sets/set.h"
#include "stdlib/stdlib.h"
#include "vm/arith.h"
#include "vm/vm.h"

namespace {

std::mt19937_64& rng() {
    static std::mt19937_64 g(std::random_device{}());
    return g;
}

ObjList* listArg(const Value& v, const char* fname) {
    if (!isList(v)) fail(std::string(fname) + " expects a list, got " + typeName(v));
    return as<ObjList>(v);
}

std::vector<Value> seqArg(const Value& v, const char* fname) {
    if (!isIterable(v)) fail(std::string(fname) + " expects a list, set, range or string, got " + typeName(v));
    return iterableToVector(v);
}

void callableArg(const Value& v, const char* fname) {
    if (!isCallable(v)) fail(std::string(fname) + " expects a function, got " + typeName(v));
}

// Results keep the shape of the input: sets stay sets, everything else becomes a list.
Value sameKind(const Value& like, std::vector<Value> items) {
    if (isSet(like)) return newSet(std::move(items));
    return newList(std::move(items));
}

Value n_print(VM& vm, int argc, Value* args) {
    std::ostream& os = *vm.out;
    for (int i = 0; i < argc; i++) {
        if (i) os << " ";
        os << display(args[i]);
    }
    os << "\n";
    return Value::nil();
}

Value n_write(VM& vm, int argc, Value* args) {
    for (int i = 0; i < argc; i++) *vm.out << toString(args[i]);
    return Value::nil();
}

Value n_str(VM&, int argc, Value* args) {
    std::string s;
    for (int i = 0; i < argc; i++) s += toString(args[i]);
    return newString(s);
}

Value n_repr(VM&, int, Value* args) { return newString(repr(args[0])); }
Value n_type(VM&, int, Value* args) { return newString(typeName(args[0])); }

Value n_len(VM&, int, Value* args) {
    const Value& v = args[0];
    if (isList(v)) return Value::integer(static_cast<int64_t>(as<ObjList>(v)->items.size()));
    if (isSet(v)) return Value::integer(static_cast<int64_t>(as<ObjSet>(v)->items.size()));
    if (isString(v)) return Value::integer(static_cast<int64_t>(utf8::length(as<ObjString>(v)->s)));
    if (isRange(v)) return Value::integer(rangeCount(as<ObjRange>(v)));
    if (isMatrix(v)) return Value::integer(static_cast<int64_t>(as<ObjMatrix>(v)->rows));
    if (isRecord(v)) return Value::integer(static_cast<int64_t>(as<ObjRecord>(v)->keys.size()));
    fail("len: a value of type " + typeName(v) + " has no length");
}

Value n_list(VM&, int argc, Value* args) {
    if (argc == 0) return newList();
    return newList(seqArg(args[0], "list"));
}

Value n_range(VM&, int argc, Value* args) {
    if (argc == 1) return makeRange(Value::integer(1), args[0], Value::integer(1));
    return makeRange(args[0], args[1], argc > 2 ? args[2] : Value::integer(1));
}

Value n_push(VM&, int argc, Value* args) {
    ObjList* l = listArg(args[0], "push");
    for (int i = 1; i < argc; i++) l->items.push_back(args[i]);
    return args[0];
}

Value n_pop(VM&, int, Value* args) {
    ObjList* l = listArg(args[0], "pop");
    if (l->items.empty()) fail("pop: list is empty");
    Value v = l->items.back();
    l->items.pop_back();
    return v;
}

Value n_insert(VM&, int, Value* args) {
    ObjList* l = listArg(args[0], "insert");
    int64_t k = toInt64(args[1], "an index");
    int64_t n = static_cast<int64_t>(l->items.size());
    if (k < 0) k = n + k + 2;
    if (k < 1 || k > n + 1) fail("insert: index out of range");
    l->items.insert(l->items.begin() + (k - 1), args[2]);
    return args[0];
}

Value n_remove(VM&, int, Value* args) {
    ObjList* l = listArg(args[0], "remove");
    int64_t k = toInt64(args[1], "an index");
    int64_t n = static_cast<int64_t>(l->items.size());
    if (k < 0) k = n + k + 1;
    if (k < 1 || k > n) fail("remove: index out of range");
    Value v = l->items[static_cast<size_t>(k - 1)];
    l->items.erase(l->items.begin() + (k - 1));
    return v;
}

Value n_concat(VM&, int argc, Value* args) {
    std::vector<Value> out;
    for (int i = 0; i < argc; i++) {
        std::vector<Value> part = seqArg(args[i], "concat");
        out.insert(out.end(), part.begin(), part.end());
    }
    return newList(std::move(out));
}

Value n_reverse(VM&, int, Value* args) {
    if (isString(args[0])) {
        const std::string& s = as<ObjString>(args[0])->s;
        std::vector<std::string> chars;
        for (size_t i = 0; i < s.size();) {
            size_t st = i;
            utf8::decode(s, i);
            chars.push_back(s.substr(st, i - st));
        }
        std::string r;
        for (auto it = chars.rbegin(); it != chars.rend(); ++it) r += *it;
        return newString(r);
    }
    std::vector<Value> v = seqArg(args[0], "reverse");
    std::reverse(v.begin(), v.end());
    return newList(std::move(v));
}

bool lessForSort(const Value& a, const Value& b) {
    if ((isNumber(a) || isSym(a) || isQuantity(a)) && (isNumber(b) || isSym(b) || isQuantity(b)) &&
        !(isNumber(a) && numKind(a) == NumKind::Cplx) && !(isNumber(b) && numKind(b) == NumKind::Cplx))
        return isTruthy(compareOp(CmpOp::Lt, a, b));
    return compareValues(a, b) < 0;
}

Value n_sort(VM& vm, int argc, Value* args) {
    std::vector<Value> v = seqArg(args[0], "sort");
    if (argc > 1) {
        callableArg(args[1], "sort");
        Value key = args[1];
        std::vector<std::pair<Value, Value>> keyed;
        for (const auto& x : v) keyed.push_back({vm.call(key, {x}), x});
        std::stable_sort(keyed.begin(), keyed.end(),
                         [](const std::pair<Value, Value>& a, const std::pair<Value, Value>& b) {
                             return lessForSort(a.first, b.first);
                         });
        for (size_t i = 0; i < v.size(); i++) v[i] = keyed[i].second;
    } else {
        std::stable_sort(v.begin(), v.end(), lessForSort);
    }
    return newList(std::move(v));
}

Value n_unique(VM&, int, Value* args) {
    std::vector<Value> v = seqArg(args[0], "unique"), out;
    for (const auto& x : v) {
        bool seen = false;
        for (const auto& y : out)
            if (valuesEqual(x, y)) {
                seen = true;
                break;
            }
        if (!seen) out.push_back(x);
    }
    return newList(std::move(out));
}

Value n_first(VM&, int, Value* args) {
    std::vector<Value> v = seqArg(args[0], "first");
    if (v.empty()) fail("first: empty sequence");
    return v.front();
}

Value n_last(VM&, int, Value* args) {
    std::vector<Value> v = seqArg(args[0], "last");
    if (v.empty()) fail("last: empty sequence");
    return v.back();
}

Value n_take(VM&, int, Value* args) {
    std::vector<Value> v = seqArg(args[0], "take");
    int64_t n = std::max<int64_t>(0, std::min<int64_t>(toInt64(args[1]), static_cast<int64_t>(v.size())));
    return newList(std::vector<Value>(v.begin(), v.begin() + n));
}

Value n_drop(VM&, int, Value* args) {
    std::vector<Value> v = seqArg(args[0], "drop");
    int64_t n = std::max<int64_t>(0, std::min<int64_t>(toInt64(args[1]), static_cast<int64_t>(v.size())));
    return newList(std::vector<Value>(v.begin() + n, v.end()));
}

Value n_sum(VM&, int argc, Value* args) {
    std::vector<Value> v = seqArg(args[0], "sum");
    Value acc = argc > 1 ? args[1] : Value::integer(0);
    for (const auto& x : v) acc = vadd(acc, x);
    return acc;
}

Value n_prod(VM&, int argc, Value* args) {
    std::vector<Value> v = seqArg(args[0], "prod");
    Value acc = argc > 1 ? args[1] : Value::integer(1);
    for (const auto& x : v) acc = vmul(acc, x);
    return acc;
}

Value n_map(VM& vm, int argc, Value* args) {
    callableArg(args[0], "map");
    Value f = args[0];
    if (argc == 2) {
        std::vector<Value> v = seqArg(args[1], "map");
        for (auto& x : v) x = vm.call(f, {x});
        return sameKind(args[1], std::move(v));
    }
    std::vector<std::vector<Value>> seqs;
    size_t n = SIZE_MAX;
    for (int i = 1; i < argc; i++) {
        seqs.push_back(seqArg(args[i], "map"));
        n = std::min(n, seqs.back().size());
    }
    std::vector<Value> out;
    for (size_t k = 0; k < n; k++) {
        std::vector<Value> callArgs;
        for (auto& s : seqs) callArgs.push_back(s[k]);
        out.push_back(vm.call(f, callArgs));
    }
    return newList(std::move(out));
}

Value n_filter(VM& vm, int, Value* args) {
    callableArg(args[0], "filter");
    std::vector<Value> v = seqArg(args[1], "filter"), out;
    for (const auto& x : v)
        if (isTruthy(vm.call(args[0], {x}))) out.push_back(x);
    return sameKind(args[1], std::move(out));
}

Value n_reduce(VM& vm, int argc, Value* args) {
    callableArg(args[0], "reduce");
    std::vector<Value> v = seqArg(args[1], "reduce");
    size_t start = 0;
    Value acc;
    if (argc > 2) {
        acc = args[2];
    } else {
        if (v.empty()) fail("reduce: empty sequence with no initial value");
        acc = v[0];
        start = 1;
    }
    for (size_t i = start; i < v.size(); i++) acc = vm.call(args[0], {acc, v[i]});
    return acc;
}

Value n_zip(VM&, int argc, Value* args) {
    std::vector<std::vector<Value>> seqs;
    size_t n = SIZE_MAX;
    for (int i = 0; i < argc; i++) {
        seqs.push_back(seqArg(args[i], "zip"));
        n = std::min(n, seqs.back().size());
    }
    std::vector<Value> out;
    for (size_t k = 0; k < n; k++) {
        std::vector<Value> t;
        for (auto& s : seqs) t.push_back(s[k]);
        out.push_back(newList(std::move(t)));
    }
    return newList(std::move(out));
}

Value n_enumerate(VM&, int, Value* args) {
    std::vector<Value> v = seqArg(args[0], "enumerate"), out;
    for (size_t i = 0; i < v.size(); i++) out.push_back(newList({Value::integer(static_cast<int64_t>(i + 1)), v[i]}));
    return newList(std::move(out));
}

Value n_any(VM& vm, int argc, Value* args) {
    std::vector<Value> v = seqArg(args[0], "any");
    for (const auto& x : v)
        if (isTruthy(argc > 1 ? vm.call(args[1], {x}) : x)) return Value::boolean(true);
    return Value::boolean(false);
}

Value n_all(VM& vm, int argc, Value* args) {
    std::vector<Value> v = seqArg(args[0], "all");
    for (const auto& x : v)
        if (!isTruthy(argc > 1 ? vm.call(args[1], {x}) : x)) return Value::boolean(false);
    return Value::boolean(true);
}

Value n_count(VM& vm, int, Value* args) {
    // Like map and filter, count(pred, xs) takes the function first; count(xs, v) is also accepted.
    Value seq = args[0], what = args[1];
    if (isCallable(seq) && !isCallable(what)) std::swap(seq, what);
    std::vector<Value> v = seqArg(seq, "count");
    int64_t c = 0;
    for (const auto& x : v)
        if (isCallable(what) ? isTruthy(vm.call(what, {x})) : valuesEqual(x, what)) c++;
    return Value::integer(c);
}

Value n_find(VM&, int, Value* args) {
    std::vector<Value> v = seqArg(args[0], "find");
    for (size_t i = 0; i < v.size(); i++)
        if (valuesEqual(v[i], args[1])) return Value::integer(static_cast<int64_t>(i + 1));
    return Value::nil();
}

Value n_apply(VM& vm, int, Value* args) {
    callableArg(args[0], "apply");
    return vm.call(args[0], seqArg(args[1], "apply"));
}

Value n_join(VM&, int argc, Value* args) {
    std::vector<Value> v = seqArg(args[0], "join");
    std::string sep = argc > 1 ? toString(args[1]) : "";
    std::string r;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) r += sep;
        r += toString(v[i]);
    }
    return newString(r);
}

Value n_split(VM&, int argc, Value* args) {
    if (!isString(args[0])) fail("split expects a string");
    const std::string& s = as<ObjString>(args[0])->s;
    std::string sep = argc > 1 ? toString(args[1]) : " ";
    std::vector<Value> out;
    if (sep.empty()) return newList(seqArg(args[0], "split"));
    size_t pos = 0;
    for (;;) {
        size_t next = s.find(sep, pos);
        if (next == std::string::npos) {
            out.push_back(newString(s.substr(pos)));
            break;
        }
        out.push_back(newString(s.substr(pos, next - pos)));
        pos = next + sep.size();
    }
    return newList(std::move(out));
}

Value n_assert(VM&, int argc, Value* args) {
    if (!isTruthy(args[0])) fail(argc > 1 ? "assertion failed: " + toString(args[1]) : "assertion failed");
    return Value::nil();
}

Value n_error(VM&, int, Value* args) { throw SedThrow(toString(args[0]), args[0]); }

Value n_clock(VM&, int, Value*) {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return Value::real(duration<double>(steady_clock::now() - start).count());
}

Value n_random(VM&, int argc, Value* args) {
    std::uniform_real_distribution<double> d(0.0, 1.0);
    if (argc == 0) return Value::real(d(rng()));
    std::vector<Value> v = seqArg(args[0], "random");
    if (v.empty()) fail("random: empty sequence");
    std::uniform_int_distribution<size_t> pick(0, v.size() - 1);
    return v[pick(rng())];
}

Value n_randint(VM&, int, Value* args) {
    int64_t a = toInt64(args[0]), b = toInt64(args[1]);
    if (a > b) fail("randint: empty interval");
    std::uniform_int_distribution<int64_t> d(a, b);
    return Value::integer(d(rng()));
}

Value n_seed(VM&, int, Value* args) {
    rng().seed(static_cast<uint64_t>(toInt64(args[0])));
    return Value::nil();
}

Value n_shuffle(VM&, int, Value* args) {
    std::vector<Value> v = seqArg(args[0], "shuffle");
    std::shuffle(v.begin(), v.end(), rng());
    return newList(std::move(v));
}

Value n_isnumber(VM&, int, Value* args) { return Value::boolean(isNumber(args[0])); }

Value n_copy(VM&, int, Value* args) {
    const Value& v = args[0];
    if (isList(v)) return newList(as<ObjList>(v)->items);
    if (isMatrix(v)) {
        const ObjMatrix* m = as<ObjMatrix>(v);
        return newMatrix(m->rows, m->cols, m->data);
    }
    if (isSet(v)) return newSet(as<ObjSet>(v)->items);
    if (isRecord(v)) {
        Value r = newRecord();
        const ObjRecord* rec = as<ObjRecord>(v);
        for (size_t i = 0; i < rec->keys.size(); i++) recordSet(r, rec->keys[i], rec->vals[i]);
        as<ObjRecord>(r)->isModule = rec->isModule;
        return r;
    }
    return v;
}

Value n_record(VM&, int argc, Value* args) {
    if (argc % 2) fail("record: keys and values come in pairs");
    Value r = newRecord();
    for (int i = 0; i < argc; i += 2) recordSet(r, args[i], args[i + 1]);
    return r;
}

Value n_keys(VM&, int, Value* args) {
    if (!isRecord(args[0])) fail("keys: expected a record, got " + typeName(args[0]));
    return newList(as<ObjRecord>(args[0])->keys);
}

Value n_values(VM&, int, Value* args) {
    if (!isRecord(args[0])) fail("values: expected a record, got " + typeName(args[0]));
    return newList(as<ObjRecord>(args[0])->vals);
}

Value n_items(VM&, int, Value* args) {
    if (!isRecord(args[0])) fail("items: expected a record, got " + typeName(args[0]));
    const ObjRecord* r = as<ObjRecord>(args[0]);
    std::vector<Value> out;
    for (size_t i = 0; i < r->keys.size(); i++) out.push_back(newList({r->keys[i], r->vals[i]}));
    return newList(std::move(out));
}

Value n_has(VM&, int, Value* args) {
    try {
        return Value::boolean(containsValue(args[0], args[1]));
    } catch (const SedError&) {
        return Value::boolean(false);
    }
}

Value n_format(VM&, int argc, Value* args) {
    if (argc < 1 || !isString(args[0])) fail("format: the template must be a string");
    const std::string& t = as<ObjString>(args[0])->s;
    std::string out;
    int next = 1;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '{' && i + 1 < t.size() && t[i + 1] == '{') {
            out += '{';
            i++;
            continue;
        }
        if (t[i] == '}' && i + 1 < t.size() && t[i + 1] == '}') {
            out += '}';
            i++;
            continue;
        }
        if (t[i] != '{') {
            out += t[i];
            continue;
        }
        size_t j = t.find('}', i + 1);
        if (j == std::string::npos) fail("format: unclosed '{'");
        std::string spec = t.substr(i + 1, j - (i + 1));
        Value v;
        if (spec.empty()) {
            if (next >= argc) fail("format: not enough arguments");
            v = args[next++];
        } else {
            bool digits = !spec.empty();
            for (char c : spec)
                if (c < '0' || c > '9') digits = false;
            if (digits) {
                int idx = std::stoi(spec);
                if (idx < 0 || idx + 1 >= argc) fail("format: argument {" + spec + "} is missing");
                v = args[idx + 1];
            } else if (argc == 2 && isRecord(args[1])) {
                v = recordGet(args[1], newString(spec));
            } else {
                fail("format: '{" + spec + "}' needs a record argument or a numeric index");
            }
        }
        out += toString(v);
        i = j;
    }
    return newString(out);
}

Value n_readfile(VM&, int, Value* args) {
    if (!isString(args[0])) fail("readfile: the path must be a string");
    std::ifstream f(std::filesystem::u8path(as<ObjString>(args[0])->s), std::ios::binary);
    if (!f) fail("readfile: cannot open '" + as<ObjString>(args[0])->s + "'");
    std::ostringstream ss;
    ss << f.rdbuf();
    return newString(ss.str());
}

Value writeFile(const Value& path, const Value& text, bool append) {
    if (!isString(path)) fail("writefile: the path must be a string");
    auto mode = append ? std::ios::app : std::ios::trunc;
    std::ofstream f(std::filesystem::u8path(as<ObjString>(path)->s), mode);
    if (!f) fail("writefile: cannot open '" + as<ObjString>(path)->s + "'");
    f << toString(text);
    return Value::nil();
}

Value n_writefile(VM&, int, Value* args) { return writeFile(args[0], args[1], false); }
Value n_appendfile(VM&, int, Value* args) { return writeFile(args[0], args[1], true); }

Value n_exists(VM&, int, Value* args) {
    if (!isString(args[0])) fail("exists: the path must be a string");
    std::error_code ec;
    return Value::boolean(std::filesystem::exists(std::filesystem::u8path(as<ObjString>(args[0])->s), ec));
}

Value n_removefile(VM&, int, Value* args) {
    if (!isString(args[0])) fail("removefile: the path must be a string");
    std::error_code ec;
    bool ok = std::filesystem::remove(std::filesystem::u8path(as<ObjString>(args[0])->s), ec);
    return Value::boolean(ok && !ec);
}

Value n_input(VM& vm, int argc, Value* args) {
    if (argc) {
        *vm.out << toString(args[0]);
        vm.out->flush();
    }
    std::string line;
    if (!std::getline(std::cin, line)) return Value::nil();
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return newString(line);
}

}  // namespace

void registerCore(VM& vm) {
    const char* io = "core";
    vm.defineNative("print", 0, -1, n_print, "print(a, b, ...) - print values separated by spaces", io);
    vm.defineNative("write", 0, -1, n_write, "write(a, ...) - print values without a newline", io);
    vm.defineNative("str", 0, -1, n_str, "str(a, ...) - concatenate the text of values", io);
    vm.defineNative("repr", 1, 1, n_repr, "repr(x) - source-like text of x", io);
    vm.defineNative("type", 1, 1, n_type, "type(x) - name of the type of x", io);
    vm.defineNative("isnumber", 1, 1, n_isnumber, "isnumber(x) - true for numeric values", io);
    vm.defineNative("assert", 1, 2, n_assert, "assert(cond, msg) - raise an error when cond is false", io);
    vm.defineNative("error", 1, 1, n_error, "error(v) - raise an error; `catch e` receives v", io);
    vm.defineNative("clock", 0, 0, n_clock, "clock() - seconds since start", io);
    vm.defineNative("random", 0, 1, n_random, "random() - uniform in [0,1); random(xs) - random element", io);
    vm.defineNative("randint", 2, 2, n_randint, "randint(a, b) - uniform integer in a..b", io);
    vm.defineNative("seed", 1, 1, n_seed, "seed(n) - seed the random generator", io);
    vm.defineNative("input", 0, 1, n_input, "input(prompt) - read one line from the keyboard", io);
    vm.defineNative("readfile", 1, 1, n_readfile, "readfile(path) - the text of a file", io);
    vm.defineNative("writefile", 2, 2, n_writefile, "writefile(path, text) - replace a file with text", io);
    vm.defineNative("appendfile", 2, 2, n_appendfile, "appendfile(path, text) - add text to the end of a file", io);
    vm.defineNative("exists", 1, 1, n_exists, "exists(path) - true when a file or directory exists", io);
    vm.defineNative("removefile", 1, 1, n_removefile, "removefile(path) - delete a file; false if it was absent", io);
    vm.defineNative("format", 1, -1, n_format, "format(template, ...) - fill {} or {name} holes", io);

    const char* ls = "lists";
    vm.defineNative("len", 1, 1, n_len, "len(x) - number of elements", ls);
    vm.defineNative("list", 0, 1, n_list, "list(xs) - list of the elements of xs", ls);
    vm.defineNative("range", 1, 3, n_range, "range(a, b, step) - inclusive range; range(n) = 1..n", ls);
    vm.defineNative("push", 2, -1, n_push, "push(xs, v, ...) - append to a list in place", ls);
    vm.defineNative("pop", 1, 1, n_pop, "pop(xs) - remove and return the last element", ls);
    vm.defineNative("insert", 3, 3, n_insert, "insert(xs, i, v) - insert v at index i", ls);
    vm.defineNative("remove", 2, 2, n_remove, "remove(xs, i) - remove and return element i", ls);
    vm.defineNative("concat", 1, -1, n_concat, "concat(xs, ys, ...) - joined list", ls);
    vm.defineNative("reverse", 1, 1, n_reverse, "reverse(xs) - reversed list or string", ls);
    vm.defineNative("sort", 1, 2, n_sort, "sort(xs, key) - sorted list", ls);
    vm.defineNative("unique", 1, 1, n_unique, "unique(xs) - elements without duplicates, in order", ls);
    vm.defineNative("first", 1, 1, n_first, "first(xs) - first element", ls);
    vm.defineNative("last", 1, 1, n_last, "last(xs) - last element", ls);
    vm.defineNative("take", 2, 2, n_take, "take(xs, n) - first n elements", ls);
    vm.defineNative("drop", 2, 2, n_drop, "drop(xs, n) - all but the first n elements", ls);
    vm.defineNative("sum", 1, 2, n_sum, "sum(xs) - sum of elements; also sum(expr, k = a..b)", ls);
    vm.defineNative("prod", 1, 2, n_prod, "prod(xs) - product of elements; also prod(expr, k = a..b)", ls);
    vm.defineNative("map", 2, -1, n_map, "map(f, xs) - apply f to each element", ls);
    vm.defineNative("filter", 2, 2, n_filter, "filter(f, xs) - elements where f is true", ls);
    vm.defineNative("reduce", 2, 3, n_reduce, "reduce(f, xs, init) - fold xs with f", ls);
    vm.defineNative("zip", 1, -1, n_zip, "zip(xs, ys, ...) - list of tuples", ls);
    vm.defineNative("enumerate", 1, 1, n_enumerate, "enumerate(xs) - list of [index, value]", ls);
    vm.defineNative("any", 1, 2, n_any, "any(xs, f) - true if some element satisfies f", ls);
    vm.defineNative("all", 1, 2, n_all, "all(xs, f) - true if every element satisfies f", ls);
    vm.defineNative("count", 2, 2, n_count, "count(xs, v_or_f) - number of matching elements", ls);
    vm.defineNative("find", 2, 2, n_find, "find(xs, v) - index of v, or nil", ls);
    vm.defineNative("apply", 2, 2, n_apply, "apply(f, args) - call f with a list of arguments", ls);
    vm.defineNative("join", 1, 2, n_join, "join(xs, sep) - concatenate as text", ls);
    vm.defineNative("split", 1, 2, n_split, "split(s, sep) - split a string", ls);
    vm.defineNative("shuffle", 1, 1, n_shuffle, "shuffle(xs) - randomly permuted list", ls);
    vm.defineNative("copy", 1, 1, n_copy, "copy(xs) - shallow copy of a list, matrix, set or record", ls);
    vm.defineNative("record", 0, -1, n_record, "record(k, v, ...) - a record; {:} is the empty record", ls);
    vm.defineNative("keys", 1, 1, n_keys, "keys(r) - the keys of a record, in insertion order", ls);
    vm.defineNative("values", 1, 1, n_values, "values(r) - the values of a record", ls);
    vm.defineNative("items", 1, 1, n_items, "items(r) - [[key, value], ...] for a record", ls);
    vm.defineNative("has", 2, 2, n_has, "has(container, x) - true when x is an element or a record key", ls);
}
