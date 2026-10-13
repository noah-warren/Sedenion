#include "framework.h"
#include "numeric/bigint.h"
#include "numeric/rational.h"

static BigInt B(const char* s) { return BigInt::fromString(s); }

TEST(bigint_parse_and_print) {
    CHECK_EQ(B("0").toString(), "0");
    CHECK_EQ(B("-12345678901234567890123").toString(), "-12345678901234567890123");
    CHECK_EQ(BigInt(INT64_MIN).toString(), "-9223372036854775808");
    CHECK(B("9223372036854775807").fitsInt64());
    CHECK(!B("9223372036854775808").fitsInt64());
}

TEST(bigint_arithmetic) {
    BigInt a = B("123456789012345678901234567890");
    BigInt b = B("987654321098765432109876543210");
    CHECK_EQ((a + b).toString(), "1111111110111111111011111111100");
    CHECK_EQ((a - b).toString(), "-864197532086419753208641975320");
    CHECK_EQ((a * b).toString(), "121932631137021795226185032733622923332237463801111263526900");
    CHECK_EQ((b / a).toString(), "8");
    CHECK_EQ((b % a).toString(), "9000000000900000000090");
    CHECK_EQ((B("-7") / B("2")).toString(), "-3");
    CHECK_EQ((B("-7") % B("2")).toString(), "-1");
}

TEST(bigint_long_division) {
    BigInt x = BigInt::pow(BigInt(3), 200);
    BigInt y = BigInt::pow(BigInt(7), 50) + BigInt(12345);
    BigInt q, r;
    BigInt::divMod(x, y, q, r);
    CHECK(q * y + r == x);
    CHECK(r < y);
    CHECK(r.sign() >= 0);
}

TEST(bigint_factorial_50) {
    BigInt f(1);
    for (int i = 2; i <= 50; i++) f = f * BigInt(i);
    CHECK_EQ(f.toString(), "30414093201713378043612608166064768844377641568960512000000000000");
}

TEST(bigint_roots_and_gcd) {
    CHECK_EQ(BigInt::isqrt(B("1000000000000000000000000000000")).toString(), "1000000000000000");
    CHECK_EQ(BigInt::isqrt(B("99")).toString(), "9");
    CHECK_EQ(BigInt::iroot(B("1000000000000000000000000000"), 3).toString(), "1000000000");
    CHECK_EQ(BigInt::gcd(B("-48"), B("180")).toString(), "12");
    CHECK_EQ(B("12345678901234567890").shl(70).shr(70).toString(), "12345678901234567890");
}

TEST(rational_normalizes) {
    Rational r(BigInt(6), BigInt(-4));
    CHECK_EQ(r.toString(), "-3/2");
    CHECK_EQ((Rational(BigInt(1), BigInt(3)) + Rational(BigInt(1), BigInt(6))).toString(), "1/2");
    CHECK_EQ((Rational(BigInt(2), BigInt(3)) / Rational(BigInt(4), BigInt(9))).toString(), "3/2");
    CHECK_EQ(Rational::pow(Rational(BigInt(2), BigInt(3)), -2).toString(), "9/4");
    CHECK_EQ(Rational(BigInt(-7), BigInt(2)).floor().toString(), "-4");
    CHECK_EQ(Rational(BigInt(-7), BigInt(2)).ceil().toString(), "-3");
    CHECK(Rational(BigInt(1), BigInt(3)) < Rational(BigInt(1), BigInt(2)));
}
