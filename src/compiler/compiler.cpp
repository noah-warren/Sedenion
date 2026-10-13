#include "compiler/compiler.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

#include "cas/expr.h"
#include "common/error.h"
#include "compiler/opcodes.h"
#include "numeric/ops.h"
#include "vm/object.h"
#include "vm/vm.h"

namespace {

struct UpvalDesc {
    bool isLocal;
    int index;
};

struct LoopCtx {
    std::vector<size_t> breaks;
    std::vector<size_t> continues;
    int tryDepth = 0;  // try blocks open when the loop started; break/continue close the newer ones
};

struct FnState {
    FnState* parent = nullptr;
    ObjFunction* fn = nullptr;
    bool isScript = false;
    std::vector<std::unordered_map<std::string, int>> scopes;
    int nextSlot = 1;
    int maxSlot = 1;
    std::vector<UpvalDesc> upvals;
    std::vector<LoopCtx> loops;
    int blockDepth = 0;
    int tryDepth = 0;
};

int resolveLocal(FnState* f, const std::string& name) {
    for (size_t i = f->scopes.size(); i-- > 0;) {
        auto it = f->scopes[i].find(name);
        if (it != f->scopes[i].end()) return it->second;
    }
    return -1;
}

int addUpvalue(FnState* f, bool isLocal, int index) {
    for (size_t i = 0; i < f->upvals.size(); i++)
        if (f->upvals[i].isLocal == isLocal && f->upvals[i].index == index) return static_cast<int>(i);
    f->upvals.push_back({isLocal, index});
    return static_cast<int>(f->upvals.size()) - 1;
}

int resolveUpvalue(FnState* f, const std::string& name) {
    if (!f->parent) return -1;
    int local = resolveLocal(f->parent, name);
    if (local >= 0) return addUpvalue(f, true, local);
    int up = resolveUpvalue(f->parent, name);
    if (up >= 0) return addUpvalue(f, false, up);
    return -1;
}

void collectAssigned(const Block& b, std::vector<std::string>& out) {
    for (const auto& s : b) {
        switch (s->kind) {
            case NK::Assign:
                if (!s->names.empty())
                    for (const auto& n : s->names) out.push_back(n);
                else if (s->kids[0]->kind == NK::Ident)
                    out.push_back(s->kids[0]->name);
                break;
            case NK::FuncDef:
                if (!s->name.empty()) out.push_back(s->name);
                break;
            case NK::For:
                out.push_back(s->name);
                collectAssigned(s->blocks[0], out);
                break;
            case NK::If:
                for (const auto& bl : s->blocks) collectAssigned(bl, out);
                break;
            case NK::While: collectAssigned(s->blocks[0], out); break;
            case NK::Try:
                collectAssigned(s->blocks[0], out);
                if (!s->name.empty()) out.push_back(s->name);
                collectAssigned(s->blocks[1], out);
                break;
            case NK::SymDecl:
                for (const auto& n : s->names) out.push_back(n);
                break;
            default: break;
        }
    }
}

Op binaryOpcode(Tok t) {
    switch (t) {
        case Tok::Plus: return Op::Add;
        case Tok::Minus: return Op::Sub;
        case Tok::Star: return Op::Mul;
        case Tok::Slash: return Op::Div;
        case Tok::Percent: return Op::Mod;
        case Tok::Caret: return Op::Pow;
        case Tok::Backslash: return Op::LDiv;
        case Tok::Dot: return Op::DotOp;
        case Tok::Cross: return Op::CrossOp;
        case Tok::Union: return Op::Union;
        case Tok::Intersect: return Op::Intersect;
        case Tok::EqEq: return Op::Eq;
        case Tok::NotEq: return Op::Ne;
        case Tok::Lt: return Op::Lt;
        case Tok::Le: return Op::Le;
        case Tok::Gt: return Op::Gt;
        case Tok::Ge: return Op::Ge;
        case Tok::In: return Op::In;
        case Tok::NotIn: return Op::NotIn;
        case Tok::Subset: return Op::Subset;
        case Tok::SubsetEq: return Op::SubsetEq;
        case Tok::Superset: return Op::Superset;
        case Tok::SupersetEq: return Op::SupersetEq;
        case Tok::PlusAssign: return Op::Add;
        case Tok::MinusAssign: return Op::Sub;
        case Tok::StarAssign: return Op::Mul;
        case Tok::SlashAssign: return Op::Div;
        default: throw SedError("internal error: no opcode for operator");
    }
}

const char* cmpName(Tok t) {
    switch (t) {
        case Tok::Lt: return "<";
        case Tok::Le: return "<=";
        case Tok::Gt: return ">";
        case Tok::Ge: return ">=";
        case Tok::NotEq: return "!=";
        default: return "?";
    }
}

// Rules are patterns, not programs: names are symbols and the expression is not evaluated.
cas::E astToCas(const Node* n) {
    auto fail = [&](const std::string& msg) -> cas::E {
        throw SyntaxError(msg, n->line, n->col);
    };
    switch (n->kind) {
        case NK::Number: {
            const Value& v = n->lit;
            if (v.isInt()) return cas::num(v.as.i);
            if (v.isReal()) return cas::real(v.as.d);
            if (isObjType(v, ObjType::BigInt)) return cas::num(Rational(as<ObjBigInt>(v)->v));
            if (isObjType(v, ObjType::Rational)) return cas::num(as<ObjRational>(v)->v);
            return fail("rules cannot contain this number");
        }
        case NK::Ident:
            if (n->name == "pi" || n->name == "π") return cas::piConst();
            if (n->name == "e") return cas::eConst();
            if (n->name == "i") return cas::imagUnit();
            return cas::sym(n->name);
        case NK::Unary:
            if (n->op == Tok::Minus) return cas::neg(astToCas(n->kids[0].get()));
            if (n->op == Tok::Plus) return astToCas(n->kids[0].get());
            if (n->op == Tok::Sqrt) return cas::sqrt(astToCas(n->kids[0].get()));
            return fail("rules cannot use this operator");
        case NK::Binary:
            switch (n->op) {
                case Tok::Plus: return cas::add(astToCas(n->kids[0].get()), astToCas(n->kids[1].get()));
                case Tok::Minus: return cas::sub(astToCas(n->kids[0].get()), astToCas(n->kids[1].get()));
                case Tok::Star:
                case Tok::Dot: return cas::mul(astToCas(n->kids[0].get()), astToCas(n->kids[1].get()));
                case Tok::Slash: return cas::div(astToCas(n->kids[0].get()), astToCas(n->kids[1].get()));
                case Tok::Caret: return cas::pow(astToCas(n->kids[0].get()), astToCas(n->kids[1].get()));
                default: return fail("rules cannot use this operator");
            }
        case NK::Call: {
            if (n->kids[0]->kind != NK::Ident) return fail("rules can only call functions by name");
            std::vector<cas::E> args;
            for (size_t i = 1; i < n->kids.size(); i++) args.push_back(astToCas(n->kids[i].get()));
            return cas::fn(n->kids[0]->name, args);
        }
        default: return fail("rules can only contain arithmetic, names and function calls");
    }
}

class Compiler {
public:
    Compiler(VM& vm, bool printExprs, const std::string& source) : vm_(vm), printExprs_(printExprs), source_(source) {}

    ObjFunction* compileScript(std::vector<NodeP>& prog) {
        FnState fs;
        fs.isScript = true;
        fs.fn = Heap::instance().make<ObjFunction>();
        fs.fn->name = "<script>";
        fs.fn->source = source_;
        fs.fn->slotNames.push_back(fs.fn->name);
        fs.scopes.emplace_back();
        fs_ = &fs;
        for (auto& s : prog) statement(s.get());
        for (const auto& name : exports_) {
            emitConst(newString(name));
            emitGet(name);
        }
        emitOp(Op::Record);
        emitU16(static_cast<int>(exports_.size()));
        emitOp(Op::Return);
        fs.fn->numSlots = fs.maxSlot;
        fs_ = nullptr;
        return fs.fn;
    }

private:
    VM& vm_;
    std::vector<std::string> exports_;
    bool printExprs_;
    std::string source_;
    FnState* fs_ = nullptr;
    int line_ = 0;
    int col_ = 0;

    [[noreturn]] void error(const std::string& msg) { throw SyntaxError(msg, line_, col_); }

    void pos(const Node* n) {
        line_ = n->line;
        col_ = n->col;
    }

    Chunk& chunk() { return fs_->fn->chunk; }
    std::vector<uint8_t>& code() { return fs_->fn->chunk.code; }

    void emitByte(int b) { chunk().write(static_cast<uint8_t>(b), line_, col_); }
    void emitOp(Op op) { emitByte(static_cast<int>(op)); }
    void emitU16(int v) {
        if (v < 0 || v > 0xFFFF) error("function too large");
        emitByte((v >> 8) & 0xFF);
        emitByte(v & 0xFF);
    }
    void emitConst(Value v) {
        emitOp(Op::Const);
        emitU16(chunk().addConstant(v));
    }
    size_t placeholder16() {
        emitByte(0xFF);
        emitByte(0xFF);
        return code().size() - 2;
    }
    size_t emitJump(Op op) {
        emitOp(op);
        return placeholder16();
    }
    void patchJump(size_t at) {
        size_t jump = code().size() - at - 2;
        if (jump > 0xFFFF) error("jump too large");
        code()[at] = static_cast<uint8_t>((jump >> 8) & 0xFF);
        code()[at + 1] = static_cast<uint8_t>(jump & 0xFF);
    }
    void emitLoop(size_t start) {
        emitOp(Op::Loop);
        size_t off = code().size() + 2 - start;
        if (off > 0xFFFF) error("loop body too large");
        emitU16(static_cast<int>(off));
    }
    void emitSlotOp(Op op, int slot) {
        emitOp(op);
        emitU16(slot);
    }

    int allocSlot(const std::string& debugName) {
        int s = fs_->nextSlot++;
        if (fs_->nextSlot > fs_->maxSlot) fs_->maxSlot = fs_->nextSlot;
        if (s > 0xFFFF) error("too many local variables");
        auto& names = fs_->fn->slotNames;
        if (static_cast<int>(names.size()) <= s) names.resize(static_cast<size_t>(s) + 1);
        names[static_cast<size_t>(s)] = debugName;
        return s;
    }

    int declareLocal(const std::string& name) {
        int s = allocSlot(name);
        fs_->scopes.back()[name] = s;
        return s;
    }

    bool resolvesInEnclosingFunction(const std::string& name) {
        for (FnState* f = fs_->parent; f && !f->isScript; f = f->parent)
            if (resolveLocal(f, name) >= 0) return true;
        return false;
    }

    void emitGet(const std::string& name) {
        int s = resolveLocal(fs_, name);
        if (s >= 0) return emitSlotOp(Op::GetLocal, s);
        int u = resolveUpvalue(fs_, name);
        if (u >= 0) return emitSlotOp(Op::GetUpval, u);
        emitSlotOp(Op::GetGlobal, vm_.globalSlot(name));
    }

    void emitSet(const std::string& name) {
        int s = resolveLocal(fs_, name);
        if (s >= 0) return emitSlotOp(Op::SetLocal, s);
        int u = resolveUpvalue(fs_, name);
        if (u >= 0) return emitSlotOp(Op::SetUpval, u);
        emitSlotOp(Op::SetGlobal, vm_.globalSlot(name));
    }

    // ---------- statements ----------

    void block(const Block& b) {
        fs_->blockDepth++;
        for (const auto& s : b) statement(s.get());
        fs_->blockDepth--;
    }

    void statement(Node* n) {
        pos(n);
        switch (n->kind) {
            case NK::ExprStmt: {
                expr(n->kids[0].get());
                pos(n);
                bool print = printExprs_ && fs_->isScript && fs_->blockDepth == 0;
                emitOp(print ? Op::PrintExpr : Op::Pop);
                break;
            }
            case NK::Assign: assign(n); break;
            case NK::FuncDef:
                function(n);
                emitSet(n->name);
                emitOp(Op::Pop);
                break;
            case NK::If: ifStatement(n); break;
            case NK::While: whileStatement(n); break;
            case NK::For: forStatement(n); break;
            case NK::Return:
                if (fs_->isScript) error("'return' outside a function");
                if (n->kids.empty())
                    emitOp(Op::Nil);
                else
                    expr(n->kids[0].get());
                emitOp(Op::Return);
                break;
            case NK::Break:
                if (fs_->loops.empty()) error("'break' outside a loop");
                closeTries();
                fs_->loops.back().breaks.push_back(emitJump(Op::Jump));
                break;
            case NK::Continue:
                if (fs_->loops.empty()) error("'continue' outside a loop");
                closeTries();
                fs_->loops.back().continues.push_back(emitJump(Op::Jump));
                break;
            case NK::Try: tryStatement(n); break;
            case NK::Import:
                emitOp(Op::Import);
                emitU16(chunk().addConstant(newString(n->name)));
                if (n->flag) {
                    emitSet(n->names[0]);
                    emitOp(Op::Pop);
                } else {
                    emitOp(Op::BindExports);
                }
                break;
            case NK::Export:
                if (!fs_->isScript) error("'export' is only allowed at the top level of a file");
                for (const auto& name : n->names)
                    if (std::find(exports_.begin(), exports_.end(), name) == exports_.end()) exports_.push_back(name);
                break;
            case NK::Rule:
                for (size_t i = 0; i + 1 < n->kids.size(); i += 2) {
                    cas::E pat = astToCas(n->kids[i].get());
                    cas::E rep = astToCas(n->kids[i + 1].get());
                    emitGet("defrule");
                    emitConst(newString(n->name));
                    emitConst(newSym(pat));
                    emitConst(newSym(rep));
                    pos(n);
                    emitOp(Op::Call);
                    emitByte(3);
                    emitOp(Op::Pop);
                }
                break;
            case NK::Assume:
                emitGet("assume");
                expr(n->kids[0].get());
                emitConst(newString(cmpName(n->op)));
                expr(n->kids[1].get());
                pos(n);
                emitOp(Op::Call);
                emitByte(3);
                emitOp(Op::Pop);
                break;
            case NK::SymDecl:
                for (const auto& name : n->names) {
                    emitConst(newSym(cas::sym(name)));
                    emitSet(name);
                    emitOp(Op::Pop);
                }
                break;
            default: error("unexpected statement");
        }
    }

    void assign(Node* n) {
        if (!n->names.empty()) {
            for (const auto& v : n->kids) expr(v.get());
            pos(n);
            for (size_t k = n->names.size(); k-- > 0;) {
                emitSet(n->names[k]);
                emitOp(Op::Pop);
            }
            return;
        }
        Node* target = n->kids[0].get();
        Node* value = n->kids[1].get();
        if (target->kind == NK::Ident) {
            if (n->op == Tok::Assign) {
                expr(value);
            } else {
                emitGet(target->name);
                expr(value);
                pos(n);
                emitOp(binaryOpcode(n->op));
            }
            pos(n);
            emitSet(target->name);
            emitOp(Op::Pop);
            return;
        }
        Node* obj = target->kids[0].get();
        size_t nidx = target->kids.size() - 1;
        if (nidx > 255) error("too many indices");
        expr(obj);
        for (size_t i = 1; i < target->kids.size(); i++) expr(target->kids[i].get());
        if (n->op != Tok::Assign) {
            expr(obj);
            for (size_t i = 1; i < target->kids.size(); i++) expr(target->kids[i].get());
            pos(target);
            emitOp(Op::Index);
            emitByte(static_cast<int>(nidx));
            expr(value);
            pos(n);
            emitOp(binaryOpcode(n->op));
        } else {
            expr(value);
        }
        pos(n);
        emitOp(Op::SetIndex);
        emitByte(static_cast<int>(nidx));
        emitOp(Op::Pop);
    }

    void ifStatement(Node* n) {
        std::vector<size_t> ends;
        size_t nc = n->kids.size();
        for (size_t i = 0; i < nc; i++) {
            expr(n->kids[i].get());
            pos(n);
            size_t jf = emitJump(Op::JumpIfFalse);
            block(n->blocks[i]);
            ends.push_back(emitJump(Op::Jump));
            patchJump(jf);
        }
        if (n->blocks.size() > nc) block(n->blocks[nc]);
        for (size_t e : ends) patchJump(e);
    }

    void closeTries() {
        for (int i = fs_->tryDepth; i > fs_->loops.back().tryDepth; i--) emitOp(Op::TryEnd);
    }

    void openLoop() {
        fs_->loops.emplace_back();
        fs_->loops.back().tryDepth = fs_->tryDepth;
    }

    // try <body> catch e <handler> end. The VM jumps to the handler with the error on the stack.
    void tryStatement(Node* n) {
        size_t toHandler = emitJump(Op::TryBegin);
        fs_->tryDepth++;
        block(n->blocks[0]);
        fs_->tryDepth--;
        pos(n);
        emitOp(Op::TryEnd);
        size_t over = emitJump(Op::Jump);
        patchJump(toHandler);
        if (!n->name.empty()) emitSet(n->name);
        emitOp(Op::Pop);
        block(n->blocks[1]);
        patchJump(over);
    }

    void whileStatement(Node* n) {
        size_t start = code().size();
        expr(n->kids[0].get());
        pos(n);
        size_t exitJump = emitJump(Op::JumpIfFalse);
        openLoop();
        block(n->blocks[0]);
        for (size_t c : fs_->loops.back().continues) patchJump(c);
        emitLoop(start);
        patchJump(exitJump);
        for (size_t b : fs_->loops.back().breaks) patchJump(b);
        fs_->loops.pop_back();
    }

    void forStatement(Node* n) {
        int it = allocSlot("(iter)");
        int cur = allocSlot("(cursor)");
        expr(n->kids[0].get());
        pos(n);
        emitSlotOp(Op::SetLocal, it);
        emitOp(Op::Pop);
        emitConst(Value::integer(0));
        emitSlotOp(Op::SetLocal, cur);
        emitOp(Op::Pop);
        size_t start = code().size();
        emitOp(Op::IterNext);
        emitU16(it);
        emitU16(cur);
        size_t exitJump = placeholder16();
        emitSet(n->name);
        emitOp(Op::Pop);
        openLoop();
        block(n->blocks[0]);
        for (size_t c : fs_->loops.back().continues) patchJump(c);
        int varSlot = resolveLocal(fs_, n->name);
        if (varSlot >= 0) emitSlotOp(Op::CloseSlot, varSlot);
        emitLoop(start);
        patchJump(exitJump);
        for (size_t b : fs_->loops.back().breaks) patchJump(b);
        fs_->loops.pop_back();
        emitOp(Op::Nil);
        emitSlotOp(Op::SetLocal, it);
        emitOp(Op::Pop);
    }

    void function(Node* n) {
        compileFunction(n->name, n->names, &n->defaults, n->rest, n->flag ? n->kids[0].get() : nullptr,
                        n->flag ? nullptr : &n->blocks[0]);
    }

    void compileFunction(const std::string& name, const std::vector<std::string>& params,
                         const std::vector<NodeP>* defaults, bool rest, Node* body, const Block* stmts) {
        if (params.size() > 255) error("too many parameters");
        if (rest && params.empty()) error("a rest parameter needs a name");
        FnState fs;
        fs.parent = fs_;
        fs.fn = Heap::instance().make<ObjFunction>();
        fs.fn->name = name.empty() ? "<lambda>" : name;
        fs.fn->source = source_;
        int count = static_cast<int>(params.size());
        int fixed = rest ? count - 1 : count;
        int minArity = 0;
        bool seenDefault = false;
        for (int i = 0; i < fixed; i++) {
            Node* d = defaults && i < static_cast<int>(defaults->size()) ? (*defaults)[static_cast<size_t>(i)].get()
                                                                         : nullptr;
            if (d) {
                seenDefault = true;
            } else if (seenDefault) {
                error("parameters with defaults must follow the required parameters");
            } else {
                minArity++;
            }
        }
        fs.fn->arity = count;
        fs.fn->minArity = minArity;
        fs.fn->hasRest = rest;
        fs.fn->slotNames.push_back(fs.fn->name);
        fs.scopes.emplace_back();
        fs_ = &fs;
        std::vector<int> slots;
        slots.reserve(params.size());
        for (const auto& p : params) {
            if (fs.scopes[0].count(p)) error("duplicate parameter '" + p + "'");
            declareLocal(p);
            slots.push_back(resolveLocal(fs_, p));
        }
        // A missing argument arrives as undef. The default runs inside the function, so it
        // sees earlier parameters and this function's own variables. nil is a real argument.
        for (int i = 0; i < fixed; i++) {
            Node* d = defaults && i < static_cast<int>(defaults->size()) ? (*defaults)[static_cast<size_t>(i)].get()
                                                                         : nullptr;
            if (!d) continue;
            emitSlotOp(Op::IsUndef, slots[static_cast<size_t>(i)]);
            size_t skip = emitJump(Op::JumpIfFalse);
            pos(d);
            expr(d);
            emitSlotOp(Op::SetLocal, slots[static_cast<size_t>(i)]);
            emitOp(Op::Pop);
            patchJump(skip);
        }
        if (body) {
            expr(body);
            emitOp(Op::Return);
        } else {
            std::vector<std::string> assigned;
            collectAssigned(*stmts, assigned);
            for (const auto& a : assigned)
                if (resolveLocal(fs_, a) < 0 && !resolvesInEnclosingFunction(a)) declareLocal(a);
            bool returned = false;
            for (size_t i = 0; i < stmts->size(); i++) {
                Node* s = (*stmts)[i].get();
                if (i + 1 == stmts->size() && s->kind == NK::ExprStmt) {
                    expr(s->kids[0].get());
                    emitOp(Op::Return);
                    returned = true;
                } else {
                    statement(s);
                }
            }
            if (!returned) {
                emitOp(Op::Nil);
                emitOp(Op::Return);
            }
        }
        fs.fn->numSlots = fs.maxSlot;
        fs.fn->upvalueCount = static_cast<int>(fs.upvals.size());
        fs_ = fs.parent;
        int idx = chunk().addConstant(Value::object(fs.fn));
        emitOp(Op::Closure);
        emitU16(idx);
        for (const auto& u : fs.upvals) {
            emitByte(u.isLocal ? 1 : 0);
            emitU16(u.index);
        }
    }

    // ---------- expressions ----------

    void expr(Node* n) {
        pos(n);
        switch (n->kind) {
            case NK::Number:
            case NK::String:
            case NK::UnitConst: emitConst(n->lit); break;
            case NK::True: emitOp(Op::True); break;
            case NK::False: emitOp(Op::False); break;
            case NK::Nil: emitOp(Op::Nil); break;
            case NK::Ident: emitGet(n->name); break;
            case NK::Unary:
                expr(n->kids[0].get());
                pos(n);
                switch (n->op) {
                    case Tok::Minus: emitOp(Op::Neg); break;
                    case Tok::Plus: break;
                    case Tok::KwNot: emitOp(Op::Not); break;
                    case Tok::Sqrt:
                        emitOp(Op::UnaryFn);
                        emitByte(0);
                        break;
                    case Tok::Cbrt:
                        emitOp(Op::UnaryFn);
                        emitByte(1);
                        break;
                    default: error("unknown unary operator");
                }
                break;
            case NK::Abs:
                expr(n->kids[0].get());
                pos(n);
                emitOp(Op::UnaryFn);
                emitByte(2);
                break;
            case NK::Binary:
                expr(n->kids[0].get());
                expr(n->kids[1].get());
                pos(n);
                emitOp(binaryOpcode(n->op));
                break;
            case NK::Logical: {
                expr(n->kids[0].get());
                pos(n);
                size_t j = emitJump(n->op == Tok::KwAnd ? Op::JumpIfFalseKeep : Op::JumpIfTrueKeep);
                emitOp(Op::Pop);
                expr(n->kids[1].get());
                patchJump(j);
                break;
            }
            case NK::Compare: comparison(n); break;
            case NK::Call: {
                Node* callee = n->kids[0].get();
                // receiver.name(args) is a method call. The VM passes the record when it is one
                // you built, and passes only the written arguments when it is an imported module.
                bool method = callee->kind == NK::Index && callee->flag && callee->kids.size() == 2;
                if (method) {
                    size_t argc = n->kids.size() - 1;
                    if (argc > 255) error("too many arguments");
                    expr(callee->kids[0].get());
                    emitOp(Op::Dup);
                    expr(callee->kids[1].get());
                    pos(n);
                    emitOp(Op::Index);
                    emitByte(1);
                    for (size_t i = 1; i < n->kids.size(); i++) expr(n->kids[i].get());
                    emitOp(Op::CallMethod);
                    emitByte(static_cast<int>(argc));
                    break;
                }
                size_t argc = n->kids.size() - 1;
                if (argc > 255) error("too many arguments");
                for (auto& k : n->kids) expr(k.get());
                pos(n);
                emitOp(Op::Call);
                emitByte(static_cast<int>(argc));
                break;
            }
            case NK::Index: {
                size_t count = n->kids.size() - 1;
                if (count > 255) error("too many indices");
                for (auto& k : n->kids) expr(k.get());
                pos(n);
                emitOp(Op::Index);
                emitByte(static_cast<int>(count));
                break;
            }
            case NK::Lambda: compileFunction("", n->names, nullptr, false, n->kids[0].get(), nullptr); break;
            case NK::FuncDef: function(n); break;
            case NK::Tuple: error("unexpected tuple; use a list [a, b] instead");
            case NK::ListLit:
                for (auto& k : n->kids) expr(k.get());
                pos(n);
                emitOp(Op::List);
                emitU16(static_cast<int>(n->kids.size()));
                break;
            case NK::MatrixLit:
                for (auto& k : n->kids) expr(k.get());
                pos(n);
                emitOp(Op::Matrix);
                emitU16(n->rows);
                emitU16(n->cols);
                break;
            case NK::SetLit:
                for (auto& k : n->kids) expr(k.get());
                pos(n);
                emitOp(Op::Set);
                emitU16(static_cast<int>(n->kids.size()));
                break;
            case NK::RecordLit:
                // A bare name is the key itself: {a: 1} stores "a", not the value of a.
                // record(k, v) and any other expression ({k + 1: v}, {"a b": v}) evaluate.
                for (size_t i = 0; i < n->kids.size(); i++) {
                    if ((i % 2) == 0 && n->kids[i]->kind == NK::Ident)
                        emitConst(newString(n->kids[i]->name));
                    else
                        expr(n->kids[i].get());
                }
                pos(n);
                emitOp(Op::Record);
                emitU16(static_cast<int>(n->kids.size() / 2));
                break;
            case NK::Interp:
                emitGet("str");
                for (auto& k : n->kids) expr(k.get());
                pos(n);
                emitOp(Op::Call);
                emitByte(static_cast<int>(n->kids.size()));
                break;
            case NK::Comprehension: comprehension(n); break;
            case NK::SumProd: sumProd(n); break;
            case NK::IfExpr: ifExpression(n); break;
            case NK::To:
                expr(n->kids[0].get());
                emitConst(n->lit);
                pos(n);
                emitOp(Op::To);
                break;
            case NK::Postfix:
                expr(n->kids[0].get());
                pos(n);
                emitOp(n->op == Tok::Bang ? Op::Fact : Op::Transpose);
                break;
            case NK::Range:
                expr(n->kids[0].get());
                expr(n->kids[1].get());
                if (n->kids.size() > 2) expr(n->kids[2].get());
                pos(n);
                emitOp(n->kids.size() > 2 ? Op::RangeStep : Op::Range);
                break;
            default: error("unexpected expression");
        }
    }

    void comparison(Node* n) {
        if (n->ops.size() == 1) {
            expr(n->kids[0].get());
            expr(n->kids[1].get());
            pos(n);
            emitOp(binaryOpcode(n->ops[0]));
            return;
        }
        int tmp = allocSlot("(compare)");
        std::vector<size_t> ends;
        expr(n->kids[0].get());
        for (size_t i = 0; i < n->ops.size(); i++) {
            expr(n->kids[i + 1].get());
            pos(n);
            bool last = i + 1 == n->ops.size();
            if (!last) emitSlotOp(Op::SetLocal, tmp);
            emitOp(binaryOpcode(n->ops[i]));
            if (!last) {
                ends.push_back(emitJump(Op::JumpIfFalseKeep));
                emitOp(Op::Pop);
                emitSlotOp(Op::GetLocal, tmp);
            }
        }
        for (size_t e : ends) patchJump(e);
    }

    void generatorLoop(Node* g, const std::function<void()>& body) {
        int it = allocSlot("(iter)");
        int cur = allocSlot("(cursor)");
        expr(g->kids[0].get());
        pos(g);
        emitSlotOp(Op::SetLocal, it);
        emitOp(Op::Pop);
        emitConst(Value::integer(0));
        emitSlotOp(Op::SetLocal, cur);
        emitOp(Op::Pop);
        int var = declareLocal(g->name);
        size_t start = code().size();
        emitOp(Op::IterNext);
        emitU16(it);
        emitU16(cur);
        size_t exitJump = placeholder16();
        emitSlotOp(Op::SetLocal, var);
        emitOp(Op::Pop);
        body();
        emitSlotOp(Op::CloseSlot, var);
        emitLoop(start);
        patchJump(exitJump);
    }

    void comprehensionClauses(Node* n, size_t i, int acc, bool isSet) {
        if (i == n->kids.size()) {
            emitSlotOp(Op::GetLocal, acc);
            expr(n->kids[0].get());
            pos(n);
            emitOp(isSet ? Op::SetAdd : Op::ListAppend);
            return;
        }
        Node* c = n->kids[i].get();
        if (c->kind == NK::Generator) {
            generatorLoop(c, [&] { comprehensionClauses(n, i + 1, acc, isSet); });
        } else {
            expr(c);
            size_t skip = emitJump(Op::JumpIfFalse);
            comprehensionClauses(n, i + 1, acc, isSet);
            patchJump(skip);
        }
    }

    void comprehension(Node* n) {
        bool isSet = n->flag;
        fs_->scopes.emplace_back();
        int acc = allocSlot("(acc)");
        emitOp(isSet ? Op::Set : Op::List);
        emitU16(0);
        emitSlotOp(Op::SetLocal, acc);
        emitOp(Op::Pop);
        comprehensionClauses(n, 1, acc, isSet);
        emitSlotOp(Op::GetLocal, acc);
        fs_->scopes.pop_back();
    }

    void sumLoops(Node* n, size_t i, int acc, bool isSum) {
        if (i == n->kids.size()) {
            emitSlotOp(Op::GetLocal, acc);
            expr(n->kids[0].get());
            pos(n);
            emitOp(isSum ? Op::Add : Op::Mul);
            emitSlotOp(Op::SetLocal, acc);
            emitOp(Op::Pop);
            return;
        }
        generatorLoop(n->kids[i].get(), [&] { sumLoops(n, i + 1, acc, isSum); });
    }

    void sumProd(Node* n) {
        bool isSum = n->op == Tok::Sum;
        fs_->scopes.emplace_back();
        int acc = allocSlot("(acc)");
        emitConst(Value::integer(isSum ? 0 : 1));
        emitSlotOp(Op::SetLocal, acc);
        emitOp(Op::Pop);
        sumLoops(n, 1, acc, isSum);
        emitSlotOp(Op::GetLocal, acc);
        fs_->scopes.pop_back();
    }

    void ifExpression(Node* n) {
        std::vector<size_t> ends;
        size_t k = 0;
        for (; k + 1 < n->kids.size(); k += 2) {
            expr(n->kids[k].get());
            size_t jf = emitJump(Op::JumpIfFalse);
            expr(n->kids[k + 1].get());
            ends.push_back(emitJump(Op::Jump));
            patchJump(jf);
        }
        expr(n->kids[k].get());
        for (size_t e : ends) patchJump(e);
    }
};

}  // namespace

ObjFunction* compileProgram(VM& vm, std::vector<NodeP>& program, bool printExprs, const std::string& source) {
    return Compiler(vm, printExprs, source).compileScript(program);
}
