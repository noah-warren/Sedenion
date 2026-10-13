#pragma once

#include "vm/object.h"

// 1-based indexing with negative indices counting from the end; ranges and lists select slices.
Value indexValue(const Value& obj, const Value* idx, int n);
void setIndexValue(const Value& obj, const Value* idx, int n, const Value& v);
