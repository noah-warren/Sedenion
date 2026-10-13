#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Failure {
    std::string message;
};

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

template <class A, class B>
void checkEqual(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    if (a == b) return;
    std::ostringstream s;
    s << file << ":" << line << ": " << ea << " == " << eb << "\n      left:  " << a << "\n      right: " << b;
    throw Failure{s.str()};
}

}  // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                                        \
    static void name();                                                                   \
    static test::Registrar TEST_CAT(registrar_, name)(#name, name);                       \
    static void name()

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) {                                                                                \
            std::ostringstream s_;                                                                    \
            s_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed";                         \
            throw test::Failure{s_.str()};                                                            \
        }                                                                                             \
    } while (0)

#define CHECK_EQ(a, b) test::checkEqual((a), (b), #a, #b, __FILE__, __LINE__)

#define CHECK_THROWS(expr)                                                                       \
    do {                                                                                         \
        bool threw_ = false;                                                                     \
        try {                                                                                    \
            (void)(expr);                                                                        \
        } catch (...) {                                                                          \
            threw_ = true;                                                                       \
        }                                                                                        \
        if (!threw_) throw test::Failure{std::string(__FILE__) + ": expected exception: " #expr}; \
    } while (0)
