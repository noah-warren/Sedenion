#pragma once

#include <cstdint>

// Operands are big-endian u16 unless noted.
enum class Op : uint8_t {
    Const,        // u16 constant index
    Nil,
    True,
    False,
    Pop,
    GetLocal,     // u16 slot
    SetLocal,     // u16 slot (leaves value on stack)
    GetGlobal,    // u16 global index
    SetGlobal,    // u16 global index (leaves value on stack)
    GetUpval,     // u16 upvalue index
    SetUpval,     // u16 upvalue index (leaves value on stack)
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Pow,
    Neg,
    Not,
    LDiv,
    DotOp,
    CrossOp,
    Union,
    Intersect,
    In,
    NotIn,
    Subset,
    SubsetEq,
    Superset,
    SupersetEq,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    Fact,
    Transpose,
    UnaryFn,          // u8: 0 sqrt, 1 cbrt, 2 abs
    Range,
    RangeStep,
    To,
    Jump,             // u16 forward offset
    JumpIfFalse,      // u16 forward offset, pops condition
    JumpIfFalseKeep,  // u16 forward offset, keeps condition
    JumpIfTrueKeep,   // u16 forward offset, keeps condition
    Loop,             // u16 backward offset
    Call,             // u8 argument count
    Closure,          // u16 function constant, then per upvalue: u8 isLocal, u16 index
    CloseSlot,        // u16 slot whose captured upvalue (if any) is closed
    Return,
    List,             // u16 count
    Matrix,           // u16 rows, u16 cols
    Set,              // u16 count
    ListAppend,       // pops value and list
    SetAdd,           // pops value and set
    Index,            // u8 index count
    SetIndex,         // u8 index count (leaves value on stack)
    IterNext,         // u16 iterable slot, u16 cursor slot, u16 exit offset
    PrintExpr,
    TryBegin,         // u16 forward offset to the handler
    TryEnd,
    Import,           // u16 constant index of the path; pushes the module's result
    Record,           // u16 pair count; stack is key, value, key, value, ...
    BindExports,      // copy a module record's string keys into the current globals
    Dup,              // duplicate the top of the stack
    Swap,             // exchange the top two values
    IsUndef,          // u16 slot; push whether that slot is the missing-argument sentinel
    CallMethod,       // u8 explicit argument count; receiver, callee, args are on the stack
};

const char* opName(Op op);
