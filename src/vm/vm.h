#pragma once

#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/error.h"
#include "vm/object.h"

struct CallFrame {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
};

// An error raised by error(value): `catch e` receives the value itself rather than its message.
struct SedThrow : SedError {
    Value value;
    SedThrow(const std::string& msg, Value v) : SedError(msg), value(v) {}
};

class VM {
public:
    VM();
    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    std::ostream* out;

    // Globals are addressed by index; the compiler allocates an index per distinct name.
    int globalSlot(const std::string& name);
    void setGlobal(const std::string& name, Value v);
    Value getGlobal(const std::string& name);
    void defineNative(const std::string& name, int minArgs, int maxArgs, NativeFn fn, const std::string& doc,
                      const std::string& category);
    const std::vector<ObjNative*>& natives() const { return natives_; }

    Value execute(ObjFunction* script);
    // Calls a function value from native code (re-entering the interpreter loop if needed).
    Value call(const Value& callee, const std::vector<Value>& args);

    std::string stackTrace() const;
    void resetAfterError();
    void collectGarbage();

    std::vector<Value> globals;
    std::vector<std::string> globalNames;
    GlobalEnv userEnv_;

    // Compiles the file at an absolute path into a script function (set by the interpreter).
    std::function<ObjFunction*(const std::string& path)> importLoader;

private:
    struct Handler {
        int frameCount;    // frames live when the try began; the handler runs in the top one
        size_t sp;         // stack height to restore
        uint8_t* catchIp;
        int nativeDepth;
    };
    static const size_t STACK_MAX = 1 << 20;
    static const int FRAMES_MAX = 20000;

    std::unique_ptr<Value[]> stack_;
    Value* sp_;
    std::unique_ptr<CallFrame[]> frames_;
    int frameCount_ = 0;
    ObjUpvalue* openUpvalues_ = nullptr;
    int nativeDepth_ = 0;
    std::unordered_map<std::string, int> globalIndex_;
    std::vector<ObjNative*> natives_;
    std::vector<Handler> handlers_;
    std::unordered_set<std::string> imported_;
    std::unordered_map<std::string, Value> moduleValues_;
    std::vector<std::unique_ptr<GlobalEnv>> moduleEnvs_;
    bool userEnvReady_ = false;

    Value readSlot(GlobalEnv* env, uint16_t idx) const;
    void writeSlot(GlobalEnv* env, uint16_t idx, const Value& v);

    void push(const Value& v) { *sp_++ = v; }
    Value pop() { return *--sp_; }
    Value& peek(int distance) { return sp_[-1 - distance]; }

    Value run(int stopAt);
    Value runLoop(int stopAt);
    bool catchError(const std::exception& e, int stopAt);
    void importModule(const std::string& path, const ObjFunction* from);
    void callValue(const Value& callee, int argc);
    ObjUpvalue* captureUpvalue(Value* local);
    void closeUpvalues(Value* last);
    void closeSlot(Value* slot);

    void markValue(const Value& v, std::vector<Obj*>& gray);
    void markObject(Obj* o, std::vector<Obj*>& gray);
    void traceObject(Obj* o, std::vector<Obj*>& gray);
};
