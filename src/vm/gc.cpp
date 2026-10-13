#include <algorithm>

#include "vm/vm.h"

// Collection only runs between instructions while no native function is active, so every
// live value is reachable from the stack, the call frames, open upvalues or the globals.

void VM::markObject(Obj* o, std::vector<Obj*>& gray) {
    if (!o || o->marked) return;
    o->marked = true;
    gray.push_back(o);
}

void VM::markValue(const Value& v, std::vector<Obj*>& gray) {
    if (v.isObj()) markObject(v.as.o, gray);
}

void VM::traceObject(Obj* o, std::vector<Obj*>& gray) {
    switch (o->type) {
        case ObjType::List:
            for (const auto& v : static_cast<ObjList*>(o)->items) markValue(v, gray);
            break;
        case ObjType::Matrix:
            for (const auto& v : static_cast<ObjMatrix*>(o)->data) markValue(v, gray);
            break;
        case ObjType::Set:
            for (const auto& v : static_cast<ObjSet*>(o)->items) markValue(v, gray);
            break;
        case ObjType::Range: {
            auto* r = static_cast<ObjRange*>(o);
            markValue(r->start, gray);
            markValue(r->stop, gray);
            markValue(r->step, gray);
            break;
        }
        case ObjType::Function:
            for (const auto& v : static_cast<ObjFunction*>(o)->chunk.constants) markValue(v, gray);
            break;
        case ObjType::Closure: {
            auto* c = static_cast<ObjClosure*>(o);
            markObject(c->fn, gray);
            for (ObjUpvalue* u : c->upvalues) markObject(u, gray);
            break;
        }
        case ObjType::Hyper:
            for (const auto& v : static_cast<ObjHyper*>(o)->c) markValue(v, gray);
            break;
        case ObjType::Record: {
            auto* r = static_cast<ObjRecord*>(o);
            for (const auto& v : r->keys) markValue(v, gray);
            for (const auto& v : r->vals) markValue(v, gray);
            break;
        }
        case ObjType::Multi:
            for (const auto& v : static_cast<ObjMulti*>(o)->c) markValue(v, gray);
            break;
        case ObjType::Upvalue: markValue(static_cast<ObjUpvalue*>(o)->closed, gray); break;
        case ObjType::Quantity: {
            auto* q = static_cast<ObjQuantity*>(o);
            markValue(q->mag, gray);
            markValue(q->labelFactor, gray);
            break;
        }
        default: break;
    }
}

void VM::collectGarbage() {
    std::vector<Obj*> gray;
    for (Value* v = stack_.get(); v < sp_; v++) markValue(*v, gray);
    for (int i = 0; i < frameCount_; i++) markObject(frames_[i].closure, gray);
    for (ObjUpvalue* u = openUpvalues_; u; u = u->nextOpen) markObject(u, gray);
    for (const auto& g : globals) markValue(g, gray);
    for (const auto& kv : userEnv_.slots) markValue(kv.second, gray);
    for (const auto& env : moduleEnvs_)
        for (const auto& kv : env->slots) markValue(kv.second, gray);
    for (const auto& m : moduleValues_) markValue(m.second, gray);
    for (ObjNative* n : natives_) markObject(n, gray);
    while (!gray.empty()) {
        Obj* o = gray.back();
        gray.pop_back();
        traceObject(o, gray);
    }
    Heap& heap = Heap::instance();
    Obj** link = &heap.head;
    while (*link) {
        Obj* o = *link;
        if (o->marked) {
            o->marked = false;
            link = &o->next;
        } else {
            *link = o->next;
            delete o;
            heap.count--;
        }
    }
    heap.threshold = std::max<size_t>(200000, heap.count * 2);
}
