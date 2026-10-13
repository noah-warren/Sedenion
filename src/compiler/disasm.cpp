#include <iomanip>
#include <ostream>

#include "compiler/compiler.h"
#include "compiler/opcodes.h"
#include "vm/object.h"

const char* opName(Op op) {
    static const char* names[] = {
        "CONST",       "NIL",          "TRUE",        "FALSE",       "POP",         "GET_LOCAL",   "SET_LOCAL",
        "GET_GLOBAL",  "SET_GLOBAL",   "GET_UPVAL",   "SET_UPVAL",   "ADD",         "SUB",         "MUL",
        "DIV",         "MOD",          "POW",         "NEG",         "NOT",         "LDIV",        "DOT",
        "CROSS",       "UNION",        "INTERSECT",   "IN",          "NOT_IN",      "SUBSET",      "SUBSET_EQ",
        "SUPERSET",    "SUPERSET_EQ",  "EQ",          "NE",          "LT",          "LE",          "GT",
        "GE",          "FACT",         "TRANSPOSE",   "UNARY_FN",    "RANGE",       "RANGE_STEP",  "TO",          "JUMP",
        "JUMP_IF_FALSE", "JUMP_IF_FALSE_KEEP", "JUMP_IF_TRUE_KEEP", "LOOP", "CALL",  "CLOSURE",     "CLOSE_SLOT",
        "RETURN",      "LIST",         "MATRIX",      "SET",         "LIST_APPEND", "SET_ADD",     "INDEX",
        "SET_INDEX",   "ITER_NEXT",    "PRINT_EXPR",  "TRY_BEGIN",   "TRY_END",     "IMPORT",
        "RECORD",      "BIND_EXPORTS", "DUP",         "SWAP",        "IS_UNDEF",    "CALL_METHOD",
    };
    size_t i = static_cast<size_t>(op);
    return i < sizeof names / sizeof names[0] ? names[i] : "?";
}

namespace {

void disassembleOne(const ObjFunction* fn, std::ostream& os, std::vector<const ObjFunction*>& nested) {
    const Chunk& c = fn->chunk;
    os << "== " << fn->name << " (";
    if (fn->hasRest)
        os << "at least " << fn->minArity << " args";
    else if (fn->minArity != fn->arity)
        os << fn->minArity << ".." << fn->arity << " args";
    else
        os << "arity " << fn->arity;
    os << ", slots " << fn->numSlots << ") ==\n";
    size_t ip = 0;
    auto u16 = [&](size_t at) { return (c.code[at] << 8) | c.code[at + 1]; };
    while (ip < c.code.size()) {
        Op op = static_cast<Op>(c.code[ip]);
        os << std::setw(5) << ip << "  line " << std::setw(3) << c.lines[ip] << "  " << std::left << std::setw(20)
           << opName(op) << std::right;
        ip++;
        switch (op) {
            case Op::Const:
            case Op::Import: {
                int k = u16(ip);
                ip += 2;
                os << k << "  " << repr(c.constants[static_cast<size_t>(k)]);
                break;
            }
            case Op::GetLocal:
            case Op::SetLocal:
            case Op::CloseSlot:
            case Op::IsUndef: {
                int s = u16(ip);
                ip += 2;
                os << s;
                if (static_cast<size_t>(s) < fn->slotNames.size()) os << " (" << fn->slotNames[static_cast<size_t>(s)] << ")";
                break;
            }
            case Op::GetGlobal:
            case Op::SetGlobal:
            case Op::GetUpval:
            case Op::SetUpval:
            case Op::List:
            case Op::Set:
            case Op::Record:
                os << u16(ip);
                ip += 2;
                break;
            case Op::Matrix:
                os << u16(ip) << "x" << u16(ip + 2);
                ip += 4;
                break;
            case Op::Jump:
            case Op::JumpIfFalse:
            case Op::JumpIfFalseKeep:
            case Op::JumpIfTrueKeep:
            case Op::TryBegin: {
                int off = u16(ip);
                ip += 2;
                os << "-> " << ip + static_cast<size_t>(off);
                break;
            }
            case Op::Loop: {
                int off = u16(ip);
                ip += 2;
                os << "-> " << ip - static_cast<size_t>(off);
                break;
            }
            case Op::Call:
            case Op::CallMethod:
            case Op::Index:
            case Op::SetIndex:
            case Op::UnaryFn: os << static_cast<int>(c.code[ip++]); break;
            case Op::IterNext: {
                int it = u16(ip), cur = u16(ip + 2), off = u16(ip + 4);
                ip += 6;
                os << it << " " << cur << " exit -> " << ip + static_cast<size_t>(off);
                break;
            }
            case Op::Closure: {
                int k = u16(ip);
                ip += 2;
                const ObjFunction* f = static_cast<const ObjFunction*>(c.constants[static_cast<size_t>(k)].as.o);
                os << f->name;
                for (int i = 0; i < f->upvalueCount; i++) {
                    os << (c.code[ip] ? " local " : " upval ") << u16(ip + 1);
                    ip += 3;
                }
                nested.push_back(f);
                break;
            }
            default: break;
        }
        os << "\n";
    }
}

}  // namespace

void disassemble(const ObjFunction* fn, std::ostream& os) {
    std::vector<const ObjFunction*> todo{fn};
    for (size_t i = 0; i < todo.size(); i++) {
        std::vector<const ObjFunction*> nested;
        disassembleOne(todo[i], os, nested);
        todo.insert(todo.end(), nested.begin(), nested.end());
        os << "\n";
    }
}
