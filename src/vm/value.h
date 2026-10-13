#pragma once

#include <cstdint>

struct Obj;

// Undef marks local slots that have been reserved but not yet assigned.
enum class ValType : uint8_t { Nil, Bool, Int, Real, Obj, Undef };

struct Value {
    ValType type = ValType::Nil;
    union {
        bool b;
        int64_t i;
        double d;
        Obj* o;
    } as;

    Value() { as.i = 0; }

    static Value nil() { return Value(); }
    static Value undef() {
        Value v;
        v.type = ValType::Undef;
        return v;
    }
    static Value boolean(bool b) {
        Value v;
        v.type = ValType::Bool;
        v.as.b = b;
        return v;
    }
    static Value integer(int64_t i) {
        Value v;
        v.type = ValType::Int;
        v.as.i = i;
        return v;
    }
    static Value real(double d) {
        Value v;
        v.type = ValType::Real;
        v.as.d = d;
        return v;
    }
    static Value object(Obj* o) {
        Value v;
        v.type = ValType::Obj;
        v.as.o = o;
        return v;
    }

    bool isNil() const { return type == ValType::Nil; }
    bool isUndef() const { return type == ValType::Undef; }
    bool isBool() const { return type == ValType::Bool; }
    bool isInt() const { return type == ValType::Int; }
    bool isReal() const { return type == ValType::Real; }
    bool isObj() const { return type == ValType::Obj; }
};
