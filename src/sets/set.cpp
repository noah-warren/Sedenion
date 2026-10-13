#include "sets/set.h"

#include <algorithm>

#include "common/error.h"
#include "numeric/ops.h"
#include "vm/arith.h"
#include "vm/vm.h"

namespace {

bool lessValue(const Value& a, const Value& b) { return compareValues(a, b) < 0; }

std::vector<Value> asSetItems(const Value& v) {
    if (isSet(v)) return as<ObjSet>(v)->items;
    if (isList(v) || isRange(v)) {
        std::vector<Value> items = iterableToVector(v);
        sortUnique(items);
        return items;
    }
    fail("expected a set, got " + typeName(v));
}

Value rawSet(std::vector<Value> sortedItems) {
    return Value::object(Heap::instance().make<ObjSet>(std::move(sortedItems)));
}

}  // namespace

void sortUnique(std::vector<Value>& items) {
    std::stable_sort(items.begin(), items.end(), lessValue);
    items.erase(std::unique(items.begin(), items.end(),
                            [](const Value& a, const Value& b) { return compareValues(a, b) == 0; }),
                items.end());
}

void setInsert(ObjSet* s, const Value& v) {
    auto it = std::lower_bound(s->items.begin(), s->items.end(), v, lessValue);
    if (it != s->items.end() && compareValues(*it, v) == 0) return;
    s->items.insert(it, v);
}

Value setUnion(const Value& a, const Value& b) {
    std::vector<Value> x = asSetItems(a), y = asSetItems(b), r;
    std::set_union(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(r), lessValue);
    return rawSet(std::move(r));
}

Value setIntersect(const Value& a, const Value& b) {
    std::vector<Value> x = asSetItems(a), y = asSetItems(b), r;
    std::set_intersection(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(r), lessValue);
    return rawSet(std::move(r));
}

Value setDifference(const Value& a, const Value& b) {
    std::vector<Value> x = asSetItems(a), y = asSetItems(b), r;
    std::set_difference(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(r), lessValue);
    return rawSet(std::move(r));
}

Value cartesianProduct(const Value& a, const Value& b) {
    std::vector<Value> x = asSetItems(a), y = asSetItems(b), r;
    for (const auto& p : x)
        for (const auto& q : y) r.push_back(newList({p, q}));
    return rawSet(std::move(r));
}

bool containsValue(const Value& container, const Value& x) {
    if (isSet(container)) {
        const auto& items = as<ObjSet>(container)->items;
        return std::binary_search(items.begin(), items.end(), x, lessValue);
    }
    if (isList(container)) {
        for (const auto& v : as<ObjList>(container)->items)
            if (valuesEqual(v, x)) return true;
        return false;
    }
    if (isRange(container)) {
        const ObjRange* r = as<ObjRange>(container);
        if (isSym(r->start) || isSym(r->stop) || isSym(r->step)) {
            for (const auto& v : iterableToVector(container))
                if (valuesEqual(v, x)) return true;
            return false;
        }
        if (!isRealNumber(x)) return false;
        Value k = numDiv(numSub(x, r->start), r->step);
        if (!isIntegerValue(k)) return false;
        int c = numCompare(k, Value::integer(0));
        return c >= 0 && numCompare(k, Value::integer(rangeCount(r))) < 0;
    }
    if (isString(container)) {
        if (!isString(x)) return false;
        return as<ObjString>(container)->s.find(as<ObjString>(x)->s) != std::string::npos;
    }
    if (isRecord(container)) return recordHas(container, x);
    if (isMatrix(container)) {
        for (const auto& v : as<ObjMatrix>(container)->data)
            if (valuesEqual(v, x)) return true;
        return false;
    }
    fail("'∈' needs a set, list, range, string or record on the right, got " + typeName(container));
}

bool isSubset(const Value& a, const Value& b, bool strict) {
    std::vector<Value> x = asSetItems(a);
    for (const auto& v : x)
        if (!containsValue(b, v)) return false;
    if (!strict) return true;
    return asSetItems(b).size() > x.size();
}

namespace {

Value n_set(VM&, int argc, Value* args) {
    if (argc == 0) return newSet({});
    if (isSet(args[0])) return args[0];
    return newSet(iterableToVector(args[0]));
}

Value n_union(VM&, int argc, Value* args) {
    Value r = newSet({});
    for (int i = 0; i < argc; i++) r = setUnion(r, args[i]);
    return r;
}

Value n_intersect(VM&, int argc, Value* args) {
    Value r = isSet(args[0]) ? args[0] : newSet(iterableToVector(args[0]));
    for (int i = 1; i < argc; i++) r = setIntersect(r, args[i]);
    return r;
}

Value n_setdiff(VM&, int, Value* args) { return setDifference(args[0], args[1]); }
Value n_cartesian(VM&, int, Value* args) { return cartesianProduct(args[0], args[1]); }
Value n_issubset(VM&, int, Value* args) { return Value::boolean(isSubset(args[0], args[1], false)); }

Value n_powerset(VM&, int, Value* args) {
    std::vector<Value> items = asSetItems(args[0]);
    if (items.size() > 20) fail("powerset: set has too many elements (max 20)");
    std::vector<Value> subsets;
    size_t n = items.size();
    for (uint64_t mask = 0; mask < (1ULL << n); mask++) {
        std::vector<Value> s;
        for (size_t i = 0; i < n; i++)
            if (mask & (1ULL << i)) s.push_back(items[i]);
        subsets.push_back(rawSet(std::move(s)));
    }
    return newSet(std::move(subsets));
}

}  // namespace

void registerSets(VM& vm) {
    const char* cat = "sets";
    vm.defineNative("set", 0, 1, n_set, "set(xs) - set of the elements of a list, range or string", cat);
    vm.defineNative("union", 1, -1, n_union, "union(A, B, ...) - A ∪ B ∪ ...", cat);
    vm.defineNative("intersect", 1, -1, n_intersect, "intersect(A, B, ...) - A ∩ B ∩ ...", cat);
    vm.defineNative("setdiff", 2, 2, n_setdiff, "setdiff(A, B) - A \\ B", cat);
    vm.defineNative("cartesian", 2, 2, n_cartesian, "cartesian(A, B) - A × B as a set of pairs", cat);
    vm.defineNative("issubset", 2, 2, n_issubset, "issubset(A, B) - A ⊆ B", cat);
    vm.defineNative("powerset", 1, 1, n_powerset, "powerset(S) - set of all subsets of S", cat);
    vm.setGlobal("∅", newSet({}));
}
