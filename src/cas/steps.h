#pragma once

#include <string>
#include <vector>

#include "cas/expr.h"

// Step-by-step working. While recording, diff, integrate, solve and limit build a tree of the
// rules they applied, which renderSteps turns into an indented derivation.
namespace cas {

enum class StepKind { Root, Diff, Integrate, Solve, Limit, Rewrite, Note };

struct StepNode {
    StepKind kind = StepKind::Root;
    std::string text, rule, result;
    std::vector<StepNode> kids;
};

void stepsBegin();
StepNode stepsEnd();
bool stepsRecording();

// One node in the derivation. Nodes of a kind are only recorded directly under the root or under
// a node of the same kind, so helper computations (the derivatives inside an integration) stay out
// of the trace. A scope that is never completed with done() is removed again.
class StepScope {
public:
    explicit StepScope(StepKind kind);
    ~StepScope();
    StepScope(const StepScope&) = delete;
    StepScope& operator=(const StepScope&) = delete;

    bool active() const { return node_ != nullptr; }
    void head(const std::string& text);
    void rule(const std::string& text);
    void done(const E& result);
    void doneText(const std::string& result);
    // Drops the sub-steps recorded after mark(), for attempts that were abandoned.
    size_t mark() const { return node_ ? node_->kids.size() : 0; }
    void rollback(size_t m) {
        if (node_ && node_->kids.size() > m) node_->kids.resize(m);
    }

private:
    StepNode* node_ = nullptr;
    bool committed_ = false;
};

// Adds an explanatory line to the innermost node when it is of the given kind.
void stepNote(StepKind kind, const std::string& text);
bool stepsWant(StepKind kind);  // true when a note of this kind would be recorded
// Mark and roll back the innermost node's sub-steps (for abandoned attempts outside a StepScope).
size_t stepsMark();
void stepsRollback(size_t m);

std::string stepExpr(const E& e);         // expression text for a step
std::string stepExprParen(const E& e);    // the same, parenthesised when it is a sum
std::string renderSteps(const StepNode& root);

}  // namespace cas
