#include "cas/steps.h"

#include <regex>

namespace cas {

namespace {

bool g_recording = false;
StepNode g_root;
std::vector<StepNode*> g_stack;
size_t g_count = 0;
bool g_truncated = false;
const size_t kMaxSteps = 1500;

// Internal symbols such as _u1 (substitutions) and _t (limits at infinity) read as u and t.
std::string tidy(const std::string& s) {
    // Whole names only, so a rule called difference_of_squares keeps its underscores.
    static const std::regex internal("\\b_([a-z])[0-9]*\\b");
    return std::regex_replace(s, internal, "$1");
}

// Expression text is produced with recording switched off, since simplify may call back into the CAS.
struct Pause {
    bool saved;
    Pause() : saved(g_recording) { g_recording = false; }
    ~Pause() { g_recording = saved; }
};

void render(const StepNode& n, const std::string& indent, std::string& out) {
    if (n.kind == StepKind::Note) {
        out += indent + n.text + "\n";
        return;
    }
    std::string rule = n.rule.empty() ? "" : "    [" + n.rule + "]";
    std::string is = n.kind == StepKind::Solve ? "⇒ " : "= ";
    if (n.kids.empty()) {
        out += indent + n.text + " " + is + n.result + rule + "\n";
        return;
    }
    out += indent + n.text + rule + "\n";
    for (const auto& k : n.kids) render(k, indent + "  ", out);
    out += indent + "  " + is + n.result + "\n";
}

}  // namespace

void stepsBegin() {
    g_root = StepNode{};
    g_stack.assign(1, &g_root);
    g_count = 0;
    g_truncated = false;
    g_recording = true;
}

StepNode stepsEnd() {
    g_recording = false;
    g_stack.clear();
    StepNode r = std::move(g_root);
    g_root = StepNode{};
    if (g_truncated) r.kids.push_back(StepNode{StepKind::Note, "... (further steps omitted)", "", "", {}});
    return r;
}

bool stepsRecording() { return g_recording; }

bool stepsWant(StepKind kind) { return g_recording && !g_stack.empty() && g_stack.back()->kind == kind; }

StepScope::StepScope(StepKind kind) {
    if (!g_recording || g_stack.empty()) return;
    StepKind top = g_stack.back()->kind;
    if (top != StepKind::Root && top != kind) return;
    if (++g_count > kMaxSteps) {
        g_truncated = true;
        return;
    }
    StepNode* parent = g_stack.back();
    parent->kids.push_back(StepNode{kind, "", "", "", {}});
    node_ = &parent->kids.back();
    g_stack.push_back(node_);
}

StepScope::~StepScope() {
    if (!node_) return;
    // Recording may have been stopped by an error that unwound through this scope.
    if (g_stack.empty() || g_stack.back() != node_) return;
    g_stack.pop_back();
    if (!committed_) g_stack.back()->kids.pop_back();
}

void StepScope::head(const std::string& text) {
    if (node_) node_->text = tidy(text);
}

void StepScope::rule(const std::string& text) {
    if (node_) node_->rule = tidy(text);
}

void StepScope::done(const E& result) {
    if (!node_) return;
    Pause pause;
    E shown = result;
    try {
        E s = simplify(result);
        if (countNodes(s) <= countNodes(result)) shown = s;
    } catch (const CasError&) {
    }
    node_->result = tidy(toString(shown));
    committed_ = true;
}

void StepScope::doneText(const std::string& result) {
    if (!node_) return;
    node_->result = tidy(result);
    committed_ = true;
}

size_t stepsMark() { return g_recording && !g_stack.empty() ? g_stack.back()->kids.size() : 0; }

void stepsRollback(size_t m) {
    if (!g_recording || g_stack.empty()) return;
    auto& kids = g_stack.back()->kids;
    if (kids.size() > m) kids.resize(m);
}

void stepNote(StepKind kind, const std::string& text) {
    if (!stepsWant(kind)) return;
    if (++g_count > kMaxSteps) {
        g_truncated = true;
        return;
    }
    g_stack.back()->kids.push_back(StepNode{StepKind::Note, tidy(text), "", "", {}});
}

std::string stepExpr(const E& e) {
    Pause pause;
    return tidy(toString(e));
}

std::string stepExprParen(const E& e) {
    std::string s = stepExpr(e);
    return e->kind == Kind::Add ? "(" + s + ")" : s;
}

std::string renderSteps(const StepNode& root) {
    std::string out;
    for (const auto& k : root.kids) render(k, "", out);
    return out;
}

}  // namespace cas
