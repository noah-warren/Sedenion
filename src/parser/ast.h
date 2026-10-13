#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lexer/token.h"
#include "vm/value.h"

enum class NK {
    // expressions
    Number,         // lit
    String,         // lit
    True,
    False,
    Nil,
    Ident,          // name
    Unary,          // op, kids[0]
    Binary,         // op, kids[0], kids[1]
    Logical,        // op (KwAnd/KwOr), kids[0], kids[1]
    Compare,        // ops, kids (operands)
    Call,           // kids[0] callee, kids[1..] args
                    // flag: this call is a function prototype (names, defaults, rest)
    Index,          // kids[0] object, kids[1..] indices
                    // flag: field access written as obj.name
    Lambda,         // names params, kids[0] body
    Tuple,          // kids; only valid as lambda parameters
    ListLit,        // kids
    MatrixLit,      // kids row-major, rows, cols
    SetLit,         // kids
    Comprehension,  // flag = set, kids[0] element, kids[1..] Generator or condition
    Generator,      // name, kids[0] iterable
    Abs,            // kids[0]
    SumProd,        // op (Sum/Prod), kids[0] body, kids[1..] Generator
    IfExpr,         // kids = cond, value, cond, value, ..., else value
    UnitConst,      // lit (quantity)
    To,             // kids[0] value, lit (unit quantity)
    Postfix,        // op (Bang/Quote), kids[0]
    Range,          // kids[0], kids[1]
    // statements
    ExprStmt,       // kids[0]
    Assign,         // op, kids[0] target, kids[1] value
    FuncDef,        // name, names params; defaults[i] is the default expression, or null
                    // rest: the last parameter is ...name
                    // flag: kids[0] is an expression body, else blocks[0]
    If,             // kids conditions, blocks bodies (+ else block)
    While,          // kids[0] condition, blocks[0]
    For,            // name, kids[0] iterable, blocks[0]
    Return,         // kids optional
    Break,
    Continue,
    SymDecl,        // names
    Try,            // blocks[0] body, blocks[1] handler; name = error variable (may be empty)
    Import,         // name = path; flag: `as`, names[0] = alias
    Export,         // names
    Rule,           // name; kids are pattern, replacement, pattern, replacement, ...
    Assume,         // op is the relation; kids are the two sides
    RecordLit,      // kids are key, value, key, value, ...
    Interp,         // kids are the pieces of an interpolated string
};

struct Node;
using NodeP = std::unique_ptr<Node>;
using Block = std::vector<NodeP>;

struct Node {
    NK kind;
    int line = 0;
    int col = 0;
    Tok op = Tok::Eof;
    std::string name;
    Value lit;
    std::vector<NodeP> kids;
    std::vector<std::string> names;
    std::vector<Tok> ops;
    std::vector<Block> blocks;
    int rows = 0;
    int cols = 0;
    bool flag = false;
    bool rest = false;             // FuncDef / prototype Call: last parameter gathers extra arguments
    std::vector<NodeP> defaults;   // parallel to names; null means the parameter is required
    Node(NK k, int l, int c) : kind(k), line(l), col(c) {}
};
