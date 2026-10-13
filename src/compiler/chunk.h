#pragma once

#include <cstdint>
#include <vector>

#include "vm/value.h"

// A compiled function body: bytecode plus constants and a source position for every byte.
struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<int> lines;
    std::vector<int> cols;

    void write(uint8_t b, int line, int col) {
        code.push_back(b);
        lines.push_back(line);
        cols.push_back(col);
    }
    int addConstant(Value v) {
        constants.push_back(v);
        return static_cast<int>(constants.size()) - 1;
    }
};
