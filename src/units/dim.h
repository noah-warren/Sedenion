#pragma once

#include <cstdint>

// Exponents of the seven SI base dimensions: m, kg, s, A, K, mol, cd.
struct Dim {
    static const int N = 7;
    int8_t e[N] = {0, 0, 0, 0, 0, 0, 0};

    bool isNone() const {
        for (int i = 0; i < N; i++)
            if (e[i]) return false;
        return true;
    }
    bool operator==(const Dim& o) const {
        for (int i = 0; i < N; i++)
            if (e[i] != o.e[i]) return false;
        return true;
    }
    bool operator!=(const Dim& o) const { return !(*this == o); }
    Dim operator+(const Dim& o) const {
        Dim r;
        for (int i = 0; i < N; i++) r.e[i] = static_cast<int8_t>(e[i] + o.e[i]);
        return r;
    }
    Dim operator-(const Dim& o) const {
        Dim r;
        for (int i = 0; i < N; i++) r.e[i] = static_cast<int8_t>(e[i] - o.e[i]);
        return r;
    }
    Dim scaled(int k) const {
        Dim r;
        for (int i = 0; i < N; i++) r.e[i] = static_cast<int8_t>(e[i] * k);
        return r;
    }
    bool divisibleBy(int k) const {
        for (int i = 0; i < N; i++)
            if (e[i] % k) return false;
        return true;
    }
    Dim divided(int k) const {
        Dim r;
        for (int i = 0; i < N; i++) r.e[i] = static_cast<int8_t>(e[i] / k);
        return r;
    }
};
