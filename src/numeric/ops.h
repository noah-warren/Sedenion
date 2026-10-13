#pragma once

#include <complex>
#include <string>

#include "numeric/bigint.h"
#include "numeric/rational.h"
#include "vm/value.h"

// The numeric tower, ordered by promotion: Int -> Big -> Rat -> Real -> Cplx.
enum class NumKind { Int = 0, Big = 1, Rat = 2, Real = 3, Cplx = 4, None = 5 };

NumKind numKind(const Value& v);
inline bool isNumber(const Value& v) { return numKind(v) != NumKind::None; }
inline bool isRealNumber(const Value& v) {
    NumKind k = numKind(v);
    return k != NumKind::None && k != NumKind::Cplx;
}
inline bool isExactNumber(const Value& v) { return numKind(v) <= NumKind::Rat; }
bool isIntegerValue(const Value& v);  // exact integer, or a real with an integral value

BigInt toBigInt(const Value& v);
Rational toRational(const Value& v);
double toDouble(const Value& v);
std::complex<double> toComplex(const Value& v);
int64_t toInt64(const Value& v, const char* what = "integer");

Value fromBigInt(BigInt b);
Value fromRational(Rational r);
Value fromComplex(std::complex<double> c);

Value numAdd(const Value& a, const Value& b);
Value numSub(const Value& a, const Value& b);
Value numMul(const Value& a, const Value& b);
Value numDiv(const Value& a, const Value& b);
Value numMod(const Value& a, const Value& b);
Value numPow(const Value& a, const Value& b);
Value numNeg(const Value& a);
Value numFloorDiv(const Value& a, const Value& b);

int numCompare(const Value& a, const Value& b);  // real numbers only
bool numEqual(const Value& a, const Value& b);
bool numIsZero(const Value& a);
bool numIsOne(const Value& a);
int numSign(const Value& a);

Value numAbs(const Value& a);
Value numFloor(const Value& a);
Value numCeil(const Value& a);
Value numRound(const Value& a);
Value numTrunc(const Value& a);
Value numSqrt(const Value& a);  // exact for perfect squares
Value numCbrt(const Value& a);
Value numFactorial(const Value& a);

std::string formatDouble(double d, bool forceDecimal);
std::string formatComplex(std::complex<double> c);
