#pragma once

#include <string>

#include "vm/arith.h"
#include "vm/object.h"

// Parses unit text such as "km/h" or "kg*m/s^2" into a Quantity of magnitude 1 in that unit.
Value parseUnitLiteral(const std::string& text);

// Builds a quantity; a dimensionless result without a display label collapses to its magnitude.
Value makeQuantity(Value mag, Dim dim, std::string label, Value labelFactor);

Value quantityBinary(BinOp op, const Value& a, const Value& b);
int quantityCompare(const Value& a, const Value& b);
bool quantityEqual(const Value& a, const Value& b);
Value quantityNeg(const Value& a);
Value convertTo(const Value& x, const Value& unit);
std::string formatQuantity(const ObjQuantity* q);
std::string dimToString(const Dim& d);

// Returns the SI magnitude of a dimensionless quantity, or fails with a message naming fname.
Value dimensionlessMagnitude(const Value& q, const char* fname);

class VM;
void registerUnits(VM& vm);
