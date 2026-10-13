#pragma once

#include <vector>

#include "vm/object.h"

Value setUnion(const Value& a, const Value& b);
Value setIntersect(const Value& a, const Value& b);
Value setDifference(const Value& a, const Value& b);
Value cartesianProduct(const Value& a, const Value& b);
bool containsValue(const Value& container, const Value& x);
bool isSubset(const Value& a, const Value& b, bool strict);
void setInsert(ObjSet* s, const Value& v);
void sortUnique(std::vector<Value>& items);

bool isIterable(const Value& v);
std::vector<Value> iterableToVector(const Value& v);  // lists, sets, ranges, strings, matrix rows

int64_t rangeCount(const ObjRange* r);
Value rangeAt(const ObjRange* r, int64_t k);
Value rangeToList(const Value& r);
Value makeRange(const Value& start, const Value& stop, const Value& step);

// Iteration protocol used by `for` loops and comprehensions. `k` is an opaque cursor that
// starts at 0; returns false when the iterable is exhausted.
bool iterNext(const Value& it, int64_t& k, Value& out);

class VM;
void registerSets(VM& vm);
