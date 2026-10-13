#include "vm/vm.h"

#include <filesystem>
#include <iostream>
#include <sstream>

#include "common/error.h"
#include "compiler/opcodes.h"
#include "linalg/matrix.h"
#include "numeric/ops.h"
#include "sets/set.h"
#include "stdlib/stdlib.h"
#include "units/units.h"
#include "vm/arith.h"
#include "vm/indexing.h"

VM::VM() : out(&std::cout) {
    stack_.reset(new Value[STACK_MAX]);
    sp_ = stack_.get();
    frames_.reset(new CallFrame[FRAMES_MAX]);
    registerStdlib(*this);
    userEnvReady_ = true;
}

Value VM::readSlot(GlobalEnv* env, uint16_t idx) const {
    if (env) {
        auto it = env->slots.find(idx);
        if (it != env->slots.end()) return it->second;
    }
    if (idx < globals.size()) return globals[static_cast<size_t>(idx)];
    return Value::undef();
}

void VM::writeSlot(GlobalEnv* env, uint16_t idx, const Value& v) {
    if (env) env->slots[idx] = v;
    else if (idx < globals.size()) globals[static_cast<size_t>(idx)] = v;
}

int VM::globalSlot(const std::string& name) {
    auto it = globalIndex_.find(name);
    if (it != globalIndex_.end()) return it->second;
    int idx = static_cast<int>(globals.size());
    globals.push_back(Value::undef());
    globalNames.push_back(name);
    globalIndex_[name] = idx;
    return idx;
}

void VM::setGlobal(const std::string& name, Value v) {
    int idx = globalSlot(name);
    if (userEnvReady_) userEnv_.slots[idx] = v;
    else globals[static_cast<size_t>(idx)] = v;
}

Value VM::getGlobal(const std::string& name) {
    auto it = globalIndex_.find(name);
    if (it == globalIndex_.end()) return Value::undef();
    return readSlot(userEnvReady_ ? &userEnv_ : nullptr, static_cast<uint16_t>(it->second));
}

void VM::defineNative(const std::string& name, int minArgs, int maxArgs, NativeFn fn, const std::string& doc,
                      const std::string& category) {
    ObjNative* n = Heap::instance().make<ObjNative>(name, minArgs, maxArgs, fn, doc, category);
    natives_.push_back(n);
    setGlobal(name, Value::object(n));
}

Value VM::execute(ObjFunction* script) {
    ObjClosure* closure = Heap::instance().make<ObjClosure>(script);
    closure->env = &userEnv_;
    push(Value::object(closure));
    callValue(Value::object(closure), 0);
    return run(0);
}

Value VM::call(const Value& callee, const std::vector<Value>& args) {
    if (sp_ + args.size() + 1 >= stack_.get() + STACK_MAX) fail("stack overflow");
    push(callee);
    for (const auto& a : args) push(a);
    int argc = static_cast<int>(args.size());
    if (isClosure(callee)) {
        int base = frameCount_;
        callValue(callee, argc);
        return run(base);
    }
    callValue(callee, argc);
    return pop();
}

void VM::callValue(const Value& callee, int argc) {
    if (isClosure(callee)) {
        ObjClosure* closure = as<ObjClosure>(callee);
        ObjFunction* fn = closure->fn;
        int fixed = fn->hasRest ? fn->arity - 1 : fn->arity;
        if (argc < fn->minArity || (!fn->hasRest && argc > fn->arity)) {
            bool one = false;
            std::string expected;
            if (fn->hasRest) {
                expected = "at least " + std::to_string(fn->minArity);
                one = fn->minArity == 1;
            } else if (fn->minArity == fn->arity) {
                expected = std::to_string(fn->arity);
                one = fn->arity == 1;
            } else {
                expected = std::to_string(fn->minArity) + " to " + std::to_string(fn->arity);
            }
            fail(fn->name + " expects " + expected + (one ? " argument" : " arguments") + ", got " +
                 std::to_string(argc));
        }
        // Missing fixed parameters are undef so the prologue can fill their defaults.
        // Extra arguments, when a rest parameter exists, become one list in the last slot.
        if (fn->hasRest) {
            if (argc > fixed) {
                int extra = argc - fixed;
                std::vector<Value> items(sp_ - extra, sp_);
                sp_ -= extra;
                push(newList(std::move(items)));
            } else {
                for (int i = argc; i < fixed; i++) push(Value::undef());
                push(newList());
            }
        } else if (argc < fn->arity) {
            for (int i = argc; i < fn->arity; i++) push(Value::undef());
        }
        if (frameCount_ >= FRAMES_MAX) fail("stack overflow (recursion too deep)");
        if (sp_ + (fn->numSlots - fn->arity) + 256 >= stack_.get() + STACK_MAX) fail("stack overflow");
        CallFrame& frame = frames_[frameCount_++];
        frame.closure = closure;
        frame.ip = fn->chunk.code.data();
        frame.slots = sp_ - fn->arity - 1;
        for (int i = fn->arity + 1; i < fn->numSlots; i++) push(Value::undef());
        return;
    }
    if (isNative(callee)) {
        ObjNative* n = as<ObjNative>(callee);
        if (argc < n->minArgs || (n->maxArgs >= 0 && argc > n->maxArgs)) {
            std::string expected = n->minArgs == n->maxArgs ? std::to_string(n->minArgs)
                                   : n->maxArgs < 0         ? "at least " + std::to_string(n->minArgs)
                                                            : std::to_string(n->minArgs) + " to " + std::to_string(n->maxArgs);
            fail(n->name + " expects " + expected + " argument(s), got " + std::to_string(argc));
        }
        nativeDepth_++;
        Value result = n->fn(*this, argc, sp_ - argc);
        nativeDepth_--;
        sp_ -= argc + 1;
        push(result);
        return;
    }
    // Juxtaposition such as 2(x + 1) or x(y + 1) compiles to a call; for non-functions it multiplies.
    if (argc == 1 && (isNumber(callee) || isSym(callee) || isQuantity(callee) || isMatrix(callee) || isList(callee))) {
        Value r = binaryOp(BinOp::Mul, callee, sp_[-1]);
        sp_ -= 2;
        push(r);
        return;
    }
    fail("cannot call a value of type " + typeName(callee));
}

ObjUpvalue* VM::captureUpvalue(Value* local) {
    ObjUpvalue* prev = nullptr;
    ObjUpvalue* up = openUpvalues_;
    while (up && up->location > local) {
        prev = up;
        up = up->nextOpen;
    }
    if (up && up->location == local) return up;
    ObjUpvalue* created = Heap::instance().make<ObjUpvalue>(local);
    created->nextOpen = up;
    if (prev)
        prev->nextOpen = created;
    else
        openUpvalues_ = created;
    return created;
}

void VM::closeUpvalues(Value* last) {
    while (openUpvalues_ && openUpvalues_->location >= last) {
        ObjUpvalue* up = openUpvalues_;
        up->closed = *up->location;
        up->location = &up->closed;
        openUpvalues_ = up->nextOpen;
    }
}

void VM::closeSlot(Value* slot) {
    ObjUpvalue* prev = nullptr;
    for (ObjUpvalue* up = openUpvalues_; up && up->location >= slot; prev = up, up = up->nextOpen) {
        if (up->location == slot) {
            up->closed = *up->location;
            up->location = &up->closed;
            if (prev)
                prev->nextOpen = up->nextOpen;
            else
                openUpvalues_ = up->nextOpen;
            return;
        }
    }
}

std::string VM::stackTrace() const {
    std::ostringstream os;
    int shown = 0;
    const std::string mainSource = frameCount_ > 0 ? frames_[0].closure->fn->source : "";
    for (int i = frameCount_ - 1; i >= 0; i--) {
        const CallFrame& f = frames_[i];
        const ObjFunction* fn = f.closure->fn;
        size_t off = static_cast<size_t>(f.ip - fn->chunk.code.data());
        if (off > 0) off--;
        int line = off < fn->chunk.lines.size() ? fn->chunk.lines[off] : 0;
        int col = off < fn->chunk.cols.size() ? fn->chunk.cols[off] : 0;
        if (++shown > 12) {
            os << "  ... (" << (i + 1) << " more frames)\n";
            break;
        }
        os << "  at line " << line << ", col " << col << " in " << fn->name;
        if (!fn->source.empty() && fn->source != mainSource)
            os << " (" << std::filesystem::u8path(fn->source).filename().u8string() << ")";
        os << "\n";
    }
    return os.str();
}

void VM::resetAfterError() {
    closeUpvalues(stack_.get());
    sp_ = stack_.get();
    frameCount_ = 0;
    nativeDepth_ = 0;
    handlers_.clear();
}

void VM::importModule(const std::string& path, const ObjFunction* from) {
    namespace fs = std::filesystem;
    if (!importLoader) fail("import is not available here");
    fs::path p = fs::u8path(path);
    if (p.is_relative()) {
        const std::string& src = from->source;
        fs::path base = !src.empty() && src[0] != '<' ? fs::u8path(src).parent_path() : fs::current_path();
        p = base / p;
    }
    std::error_code ec;
    if (!fs::exists(p, ec) && !p.has_extension()) p += ".sed";
    if (!fs::exists(p, ec)) fail("import: cannot find '" + path + "'");
    fs::path canon = fs::weakly_canonical(p, ec);
    std::string key = (ec ? p.lexically_normal() : canon).u8string();
    auto done = moduleValues_.find(key);
    if (done != moduleValues_.end()) {
        push(done->second);
        return;
    }
    if (imported_.count(key)) {
        Value partial = newRecord();  // a cycle: the module is still loading
        as<ObjRecord>(partial)->isModule = true;
        push(partial);
        return;
    }
    imported_.insert(key);  // before running, so circular imports stop here
    ObjFunction* fn;
    try {
        fn = importLoader(key);
    } catch (...) {
        imported_.erase(key);
        throw;
    }
    moduleEnvs_.push_back(std::make_unique<GlobalEnv>());
    ObjClosure* closure = Heap::instance().make<ObjClosure>(fn);
    closure->env = moduleEnvs_.back().get();
    Value closureVal = Value::object(closure);
    push(closureVal);
    callValue(closureVal, 0);
}

Value VM::run(int stopAt) {
    for (;;) {
        try {
            return runLoop(stopAt);
        } catch (const std::exception& e) {
            if (!catchError(e, stopAt)) throw;
        }
    }
}

// Resumes at the innermost try handler if it belongs to this activation of run (frames above
// stopAt); errors with no handler here propagate to the caller, which may be an outer run.
bool VM::catchError(const std::exception& e, int stopAt) {
    if (handlers_.empty() || handlers_.back().frameCount <= stopAt) return false;
    Handler h = handlers_.back();
    handlers_.pop_back();
    const SedThrow* thrown = dynamic_cast<const SedThrow*>(&e);
    Value err = thrown ? thrown->value : newString(e.what());
    closeUpvalues(stack_.get() + h.sp);
    frameCount_ = h.frameCount;
    sp_ = stack_.get() + h.sp;
    nativeDepth_ = h.nativeDepth;
    frames_[frameCount_ - 1].ip = h.catchIp;
    push(err);
    return true;
}

namespace {

CmpOp cmpFor(Op op) {
    switch (op) {
        case Op::Eq: return CmpOp::Eq;
        case Op::Ne: return CmpOp::Ne;
        case Op::Lt: return CmpOp::Lt;
        case Op::Le: return CmpOp::Le;
        case Op::Gt: return CmpOp::Gt;
        default: return CmpOp::Ge;
    }
}

BinOp binFor(Op op) {
    switch (op) {
        case Op::Add: return BinOp::Add;
        case Op::Sub: return BinOp::Sub;
        case Op::Mul: return BinOp::Mul;
        case Op::Div: return BinOp::Div;
        case Op::Mod: return BinOp::Mod;
        case Op::Pow: return BinOp::Pow;
        case Op::LDiv: return BinOp::LDiv;
        case Op::DotOp: return BinOp::Dot;
        default: return BinOp::Cross;
    }
}

}  // namespace

Value VM::runLoop(int stopAt) {
    CallFrame* frame = &frames_[frameCount_ - 1];
    Heap& heap = Heap::instance();

#define READ_BYTE() (*frame->ip++)
#define READ_U16() (frame->ip += 2, static_cast<uint16_t>((frame->ip[-2] << 8) | frame->ip[-1]))

    for (;;) {
        if (heap.count > heap.threshold && nativeDepth_ == 0) collectGarbage();
        Op op = static_cast<Op>(READ_BYTE());
        switch (op) {
            case Op::Const: push(frame->closure->fn->chunk.constants[READ_U16()]); break;
            case Op::Nil: push(Value::nil()); break;
            case Op::True: push(Value::boolean(true)); break;
            case Op::False: push(Value::boolean(false)); break;
            case Op::Pop: sp_--; break;
            case Op::GetLocal: {
                uint16_t slot = READ_U16();
                const Value& v = frame->slots[slot];
                if (v.isUndef()) {
                    const auto& names = frame->closure->fn->slotNames;
                    std::string name = slot < names.size() ? names[slot] : "?";
                    fail("variable '" + name + "' is used before it is assigned");
                }
                push(v);
                break;
            }
            case Op::SetLocal: frame->slots[READ_U16()] = peek(0); break;
            case Op::GetGlobal: {
                uint16_t idx = READ_U16();
                Value v = readSlot(frame->closure->env, idx);
                if (v.isUndef())
                    fail("undefined variable '" + globalNames[idx] + "' (declare symbols with: sym " + globalNames[idx] +
                         ")");
                push(v);
                break;
            }
            case Op::SetGlobal: writeSlot(frame->closure->env, READ_U16(), peek(0)); break;
            case Op::GetUpval: push(*frame->closure->upvalues[READ_U16()]->location); break;
            case Op::SetUpval: *frame->closure->upvalues[READ_U16()]->location = peek(0); break;

            case Op::Add:
            case Op::Sub:
            case Op::Mul: {
                Value b = pop();
                Value a = pop();
                if (a.isInt() && b.isInt()) {
                    push(op == Op::Add ? numAdd(a, b) : op == Op::Sub ? numSub(a, b) : numMul(a, b));
                } else {
                    push(binaryOp(binFor(op), a, b));
                }
                break;
            }
            case Op::Div:
            case Op::Mod:
            case Op::Pow:
            case Op::LDiv:
            case Op::DotOp:
            case Op::CrossOp: {
                Value b = pop();
                Value a = pop();
                push(binaryOp(binFor(op), a, b));
                break;
            }
            case Op::Neg: {
                Value a = pop();
                push(negateValue(a));
                break;
            }
            case Op::Not: {
                Value a = pop();
                push(Value::boolean(!isTruthy(a)));
                break;
            }
            case Op::Union: {
                Value b = pop();
                Value a = pop();
                push(setUnion(a, b));
                break;
            }
            case Op::Intersect: {
                Value b = pop();
                Value a = pop();
                push(setIntersect(a, b));
                break;
            }
            case Op::In:
            case Op::NotIn: {
                Value b = pop();
                Value a = pop();
                bool r = containsValue(b, a);
                push(Value::boolean(op == Op::In ? r : !r));
                break;
            }
            case Op::Subset:
            case Op::SubsetEq: {
                Value b = pop();
                Value a = pop();
                push(Value::boolean(isSubset(a, b, op == Op::Subset)));
                break;
            }
            case Op::Superset:
            case Op::SupersetEq: {
                Value b = pop();
                Value a = pop();
                push(Value::boolean(isSubset(b, a, op == Op::Superset)));
                break;
            }
            case Op::Eq:
            case Op::Ne:
            case Op::Lt:
            case Op::Le:
            case Op::Gt:
            case Op::Ge: {
                Value b = pop();
                Value a = pop();
                if (a.isInt() && b.isInt()) {
                    int64_t x = a.as.i, y = b.as.i;
                    bool r = op == Op::Eq   ? x == y
                             : op == Op::Ne ? x != y
                             : op == Op::Lt ? x < y
                             : op == Op::Le ? x <= y
                             : op == Op::Gt ? x > y
                                            : x >= y;
                    push(Value::boolean(r));
                } else {
                    push(compareOp(cmpFor(op), a, b));
                }
                break;
            }
            case Op::Fact: {
                Value a = pop();
                push(factorialValue(a));
                break;
            }
            case Op::Transpose: {
                Value a = pop();
                push(transposeValue(a));
                break;
            }
            case Op::UnaryFn: {
                uint8_t which = READ_BYTE();
                Value a = pop();
                push(which == 0 ? sqrtValue(a) : which == 1 ? cbrtValue(a) : absValue(a));
                break;
            }
            case Op::Range: {
                Value b = pop();
                Value a = pop();
                push(makeRange(a, b, Value::integer(1)));
                break;
            }
            case Op::RangeStep: {
                Value s = pop();
                Value b = pop();
                Value a = pop();
                push(makeRange(a, b, s));
                break;
            }
            case Op::To: {
                Value unit = pop();
                Value a = pop();
                push(convertTo(a, unit));
                break;
            }
            case Op::Jump: {
                uint16_t off = READ_U16();
                frame->ip += off;
                break;
            }
            case Op::JumpIfFalse: {
                uint16_t off = READ_U16();
                Value c = pop();
                if (!isTruthy(c)) frame->ip += off;
                break;
            }
            case Op::JumpIfFalseKeep: {
                uint16_t off = READ_U16();
                if (!isTruthy(peek(0))) frame->ip += off;
                break;
            }
            case Op::JumpIfTrueKeep: {
                uint16_t off = READ_U16();
                if (isTruthy(peek(0))) frame->ip += off;
                break;
            }
            case Op::Loop: {
                uint16_t off = READ_U16();
                frame->ip -= off;
                break;
            }
            case Op::Call: {
                int argc = READ_BYTE();
                callValue(peek(argc), argc);
                frame = &frames_[frameCount_ - 1];
                break;
            }
            case Op::CallMethod: {
                // Stack: receiver, callee, explicit args. A record you built is passed as the
                // first argument. An imported module is a namespace, so only the written
                // arguments are passed.
                int n = READ_BYTE();
                Value* base = sp_ - n - 2;
                Value receiver = base[0];
                Value callee = base[1];
                bool inject = !(isRecord(receiver) && as<ObjRecord>(receiver)->isModule);
                if (inject) {
                    base[0] = callee;
                    base[1] = receiver;
                    callValue(callee, n + 1);
                } else {
                    base[0] = callee;
                    for (int i = 0; i < n; i++) base[1 + i] = base[2 + i];
                    sp_ = base + 1 + n;
                    callValue(callee, n);
                }
                frame = &frames_[frameCount_ - 1];
                break;
            }
            case Op::Closure: {
                ObjFunction* fn = static_cast<ObjFunction*>(frame->closure->fn->chunk.constants[READ_U16()].as.o);
                ObjClosure* closure = heap.make<ObjClosure>(fn);
                closure->env = frame->closure->env;
                for (int i = 0; i < fn->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint16_t index = READ_U16();
                    closure->upvalues[static_cast<size_t>(i)] =
                        isLocal ? captureUpvalue(frame->slots + index) : frame->closure->upvalues[index];
                }
                push(Value::object(closure));
                break;
            }
            case Op::CloseSlot: closeSlot(frame->slots + READ_U16()); break;
            case Op::Return: {
                Value result = pop();
                ObjFunction* leaving = frame->closure->fn;
                if (leaving->name == "<script>" && !leaving->source.empty() && imported_.count(leaving->source)) {
                    if (isRecord(result)) as<ObjRecord>(result)->isModule = true;
                    moduleValues_[leaving->source] = result;
                }
                closeUpvalues(frame->slots);
                frameCount_--;
                while (!handlers_.empty() && handlers_.back().frameCount > frameCount_) handlers_.pop_back();
                sp_ = frame->slots;
                if (frameCount_ == stopAt) return result;
                push(result);
                frame = &frames_[frameCount_ - 1];
                break;
            }
            case Op::List: {
                uint16_t n = READ_U16();
                std::vector<Value> items(sp_ - n, sp_);
                sp_ -= n;
                push(newList(std::move(items)));
                break;
            }
            case Op::Matrix: {
                uint16_t rows = READ_U16();
                uint16_t cols = READ_U16();
                size_t n = static_cast<size_t>(rows) * cols;
                std::vector<Value> items(sp_ - n, sp_);
                sp_ -= n;
                for (const auto& v : items)
                    if (isList(v) || isMatrix(v) || isSet(v)) fail("matrix entries must be scalars");
                push(newMatrix(rows, cols, std::move(items)));
                break;
            }
            case Op::Set: {
                uint16_t n = READ_U16();
                std::vector<Value> items(sp_ - n, sp_);
                sp_ -= n;
                push(newSet(std::move(items)));
                break;
            }
            case Op::Record: {
                uint16_t n = READ_U16();
                Value rec = newRecord();
                Value* base = sp_ - static_cast<int>(n) * 2;
                for (uint16_t i = 0; i < n; i++) recordSet(rec, base[2 * i], base[2 * i + 1]);
                sp_ = base;
                push(rec);
                break;
            }
            case Op::Dup: push(peek(0)); break;
            case Op::Swap: {
                Value a = pop();
                Value b = pop();
                push(a);
                push(b);
                break;
            }
            case Op::IsUndef: {
                uint16_t slot = READ_U16();
                push(Value::boolean(frame->slots[slot].isUndef()));
                break;
            }
            case Op::BindExports: {
                Value rec = pop();
                if (isRecord(rec)) {
                    const ObjRecord* r = as<ObjRecord>(rec);
                    for (size_t i = 0; i < r->keys.size(); i++) {
                        if (!isString(r->keys[i])) continue;
                        int slot = globalSlot(as<ObjString>(r->keys[i])->s);
                        writeSlot(frame->closure->env, static_cast<uint16_t>(slot), r->vals[i]);
                    }
                }
                break;
            }
            case Op::ListAppend: {
                Value v = pop();
                Value list = pop();
                as<ObjList>(list)->items.push_back(v);
                break;
            }
            case Op::SetAdd: {
                Value v = pop();
                Value set = pop();
                setInsert(as<ObjSet>(set), v);
                break;
            }
            case Op::Index: {
                int n = READ_BYTE();
                Value r = indexValue(sp_[-n - 1], sp_ - n, n);
                sp_ -= n + 1;
                push(r);
                break;
            }
            case Op::SetIndex: {
                int n = READ_BYTE();
                Value v = pop();
                setIndexValue(sp_[-n - 1], sp_ - n, n, v);
                sp_ -= n + 1;
                push(v);
                break;
            }
            case Op::IterNext: {
                uint16_t itSlot = READ_U16();
                uint16_t curSlot = READ_U16();
                uint16_t off = READ_U16();
                int64_t k = frame->slots[curSlot].as.i;
                Value element;
                if (!iterNext(frame->slots[itSlot], k, element)) {
                    frame->ip += off;
                } else {
                    frame->slots[curSlot] = Value::integer(k);
                    push(element);
                }
                break;
            }
            case Op::PrintExpr: {
                Value v = pop();
                if (!v.isNil()) *out << display(v) << "\n";
                break;
            }
            case Op::TryBegin: {
                uint16_t off = READ_U16();
                handlers_.push_back({frameCount_, static_cast<size_t>(sp_ - stack_.get()), frame->ip + off, nativeDepth_});
                break;
            }
            case Op::TryEnd: handlers_.pop_back(); break;
            case Op::Import: {
                const Value& path = frame->closure->fn->chunk.constants[READ_U16()];
                importModule(as<ObjString>(path)->s, frame->closure->fn);
                frame = &frames_[frameCount_ - 1];
                break;
            }
            default: fail("internal error: unknown opcode " + std::to_string(static_cast<int>(op)));
        }
    }
#undef READ_BYTE
#undef READ_U16
}
