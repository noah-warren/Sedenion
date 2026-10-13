#include "parser/parser.h"

#include <cctype>
#include <cstdlib>
#include <set>
#include <string>

#include "common/error.h"
#include "common/utf8.h"
#include "lexer/lexer.h"
#include "numeric/ops.h"
#include "units/units.h"
#include "vm/object.h"

namespace {

// Binding power, lowest to highest.
enum Prec {
    P_NONE = 0,
    P_LAMBDA,
    P_TO,
    P_OR,
    P_AND,
    P_NOT,
    P_CMP,
    P_RANGE,
    P_ADD,
    P_MUL,
    P_IMPLICIT,
    P_UNARY,
    P_POW,
    P_POSTFIX
};

bool isCompareOp(Tok t) {
    switch (t) {
        case Tok::EqEq:
        case Tok::NotEq:
        case Tok::Lt:
        case Tok::Le:
        case Tok::Gt:
        case Tok::Ge:
        case Tok::In:
        case Tok::NotIn:
        case Tok::Subset:
        case Tok::SubsetEq:
        case Tok::Superset:
        case Tok::SupersetEq: return true;
        default: return false;
    }
}

bool isAssignOp(Tok t) {
    return t == Tok::Assign || t == Tok::PlusAssign || t == Tok::MinusAssign || t == Tok::StarAssign ||
           t == Tok::SlashAssign;
}

Value parseNumber(const std::string& s) {
    if (s.find_first_of(".eE") != std::string::npos) return Value::real(std::strtod(s.c_str(), nullptr));
    if (s.size() <= 18) return Value::integer(std::stoll(s));
    return fromBigInt(BigInt::fromString(s));
}

NodeP parseExpression(const std::string& source);

class Parser {
public:
    explicit Parser(const std::vector<Token>& t) : t_(t) {}

    std::vector<NodeP> program() {
        std::vector<NodeP> out;
        skipTerminators();
        while (!check(Tok::Eof)) {
            out.push_back(statement());
            endStatement();
            skipTerminators();
        }
        return out;
    }

    NodeP oneExpression() {
        skipTerminators();
        NodeP e = expression();
        skipTerminators();
        if (!check(Tok::Eof)) error("expected a single expression");
        return e;
    }

private:
    const std::vector<Token>& t_;
    size_t p_ = 0;

    const Token& peek(size_t k = 0) const {
        size_t i = p_ + k;
        if (i >= t_.size()) i = t_.size() - 1;
        return t_[i];
    }
    bool check(Tok k) const { return peek().type == k; }
    const Token& advance() {
        const Token& tk = t_[p_];
        if (p_ + 1 < t_.size()) p_++;
        return tk;
    }
    bool match(Tok k) {
        if (!check(k)) return false;
        advance();
        return true;
    }

    [[noreturn]] void error(const std::string& msg) {
        const Token& tk = peek();
        bool eof = tk.type == Tok::Eof;
        std::string where = eof ? " (reached end of input)"
                                : (tk.type == Tok::Newline ? " at end of line" : " near '" + tk.text + "'");
        throw SyntaxError(msg + where, tk.line, tk.col, eof);
    }

    const Token& expect(Tok k, const char* what) {
        if (!check(k)) error(std::string("expected ") + what);
        return advance();
    }

    NodeP node(NK k, const Token& tk) { return std::make_unique<Node>(k, tk.line, tk.col); }

    void skipTerminators() {
        while (check(Tok::Newline) || check(Tok::Semicolon)) advance();
    }

    bool atBlockEnd() const {
        Tok k = peek().type;
        return k == Tok::KwEnd || k == Tok::KwElse || k == Tok::KwElif || k == Tok::KwCatch;
    }

    void endStatement() {
        if (check(Tok::Newline) || check(Tok::Semicolon)) {
            advance();
            return;
        }
        if (check(Tok::Eof) || atBlockEnd()) return;
        // A block statement can be followed directly by the next statement: `if c then ... end return x`.
        if (p_ > 0 && t_[p_ - 1].type == Tok::KwEnd) return;
        error("expected end of statement");
    }

    Block block() {
        Block b;
        skipTerminators();
        while (!atBlockEnd()) {
            if (check(Tok::Eof)) error("expected 'end'");
            b.push_back(statement());
            endStatement();
            skipTerminators();
        }
        return b;
    }

    // ---------- statements ----------

    NodeP statement() {
        const Token& tk = peek();
        switch (tk.type) {
            case Tok::KwIf: return ifStatement();
            case Tok::KwWhile: return whileStatement();
            case Tok::KwFor: return forStatement();
            case Tok::KwFn:
                if (peek(1).type == Tok::Ident) return fnStatement();
                break;
            case Tok::KwReturn: {
                advance();
                NodeP n = node(NK::Return, tk);
                if (!check(Tok::Newline) && !check(Tok::Semicolon) && !check(Tok::Eof) && !atBlockEnd())
                    n->kids.push_back(expression());
                return n;
            }
            case Tok::KwTry: return tryStatement();
            case Tok::KwImport: {
                advance();
                NodeP n = node(NK::Import, tk);
                n->name = expect(Tok::String, "a file name in quotes after 'import'").text;
                if (match(Tok::KwAs)) {
                    n->flag = true;
                    n->names.push_back(expect(Tok::Ident, "a name after 'as'").text);
                }
                return n;
            }
            case Tok::KwExport: {
                advance();
                NodeP n = node(NK::Export, tk);
                do {
                    n->names.push_back(expect(Tok::Ident, "a name to export").text);
                } while (match(Tok::Comma));
                return n;
            }
            case Tok::KwRule: return ruleStatement();
            case Tok::KwAssume: return assumeStatement();
            case Tok::KwBreak: advance(); return node(NK::Break, tk);
            case Tok::KwContinue: advance(); return node(NK::Continue, tk);
            case Tok::KwSym: {
                advance();
                NodeP n = node(NK::SymDecl, tk);
                do {
                    n->names.push_back(expect(Tok::Ident, "a symbol name").text);
                } while (match(Tok::Comma));
                return n;
            }
            default: break;
        }
        NodeP e = expression();
        if (e->kind == NK::Ident && check(Tok::Comma)) return parallelAssign(std::move(e));
        if (isAssignOp(peek().type)) {
            const Token& opTok = advance();
            NodeP value = expression();
            if (e->kind == NK::Call && opTok.type == Tok::Assign) return functionFromCall(std::move(e), std::move(value));
            if (e->kind != NK::Ident && e->kind != NK::Index)
                throw SyntaxError("cannot assign to this expression", e->line, e->col);
            NodeP n = node(NK::Assign, opTok);
            n->op = opTok.type;
            n->kids.push_back(std::move(e));
            n->kids.push_back(std::move(value));
            return n;
        }
        NodeP s = std::make_unique<Node>(NK::ExprStmt, e->line, e->col);
        s->kids.push_back(std::move(e));
        return s;
    }

    // a, b = b, a % b: every value is evaluated before any name is assigned.
    NodeP parallelAssign(NodeP first) {
        NodeP n = std::make_unique<Node>(NK::Assign, first->line, first->col);
        n->op = Tok::Assign;
        n->names.push_back(first->name);
        while (match(Tok::Comma)) n->names.push_back(expect(Tok::Ident, "a variable name").text);
        expect(Tok::Assign, "'=' after the names in a parallel assignment");
        do {
            n->kids.push_back(expression());
        } while (match(Tok::Comma));
        if (n->kids.size() != n->names.size())
            throw SyntaxError("parallel assignment needs as many values as names", n->line, n->col);
        return n;
    }

    NodeP functionFromCall(NodeP call, NodeP body) {
        Node* callee = call->kids[0].get();
        if (callee->kind != NK::Ident) throw SyntaxError("invalid function definition", call->line, call->col);
        NodeP f = std::make_unique<Node>(NK::FuncDef, call->line, call->col);
        f->name = callee->name;
        if (call->flag) {
            f->names = std::move(call->names);
            f->defaults = std::move(call->defaults);
            f->rest = call->rest;
        } else {
            for (size_t i = 1; i < call->kids.size(); i++) {
                Node* a = call->kids[i].get();
                if (a->kind != NK::Ident)
                    throw SyntaxError("function parameters must be plain names", a->line, a->col);
                f->names.push_back(a->name);
                f->defaults.push_back(nullptr);
            }
        }
        f->flag = true;
        f->kids.push_back(std::move(body));
        return f;
    }

    NodeP ifStatement() {
        const Token& tk = advance();
        NodeP n = node(NK::If, tk);
        n->kids.push_back(expression());
        expect(Tok::KwThen, "'then'");
        n->blocks.push_back(block());
        for (;;) {
            if (match(Tok::KwElif)) {
                n->kids.push_back(expression());
                expect(Tok::KwThen, "'then'");
                n->blocks.push_back(block());
                continue;
            }
            if (match(Tok::KwElse)) {
                if (match(Tok::KwIf)) {
                    n->kids.push_back(expression());
                    expect(Tok::KwThen, "'then'");
                    n->blocks.push_back(block());
                    continue;
                }
                n->blocks.push_back(block());
            }
            expect(Tok::KwEnd, "'end'");
            break;
        }
        return n;
    }

    NodeP tryStatement() {
        const Token& tk = advance();
        NodeP n = node(NK::Try, tk);
        n->blocks.push_back(block());
        expect(Tok::KwCatch, "'catch'");
        // `catch e` names the error; in a one-line `catch print(...)` the identifier starts the handler.
        if (check(Tok::Ident)) {
            Tok next = peek(1).type;
            bool name = next == Tok::Newline || next == Tok::Semicolon || next == Tok::KwEnd || next == Tok::Ident ||
                        next == Tok::KwIf || next == Tok::KwFor || next == Tok::KwWhile || next == Tok::KwReturn ||
                        next == Tok::KwBreak || next == Tok::KwContinue || next == Tok::KwTry || next == Tok::Eof;
            if (name) n->name = advance().text;
        }
        n->blocks.push_back(block());
        expect(Tok::KwEnd, "'end'");
        return n;
    }

    NodeP ruleStatement() {
        const Token& tk = advance();
        NodeP n = node(NK::Rule, tk);
        n->name = expect(Tok::Ident, "a name for the rule").text;
        skipTerminators();
        if (check(Tok::KwEnd)) error("a rule needs at least one 'pattern => replacement'");
        while (!check(Tok::KwEnd)) {
            if (check(Tok::Eof)) error("expected 'end'");
            n->kids.push_back(expression());
            expect(Tok::FatArrow, "'=>'");
            n->kids.push_back(expression());
            skipTerminators();
        }
        expect(Tok::KwEnd, "'end'");
        return n;
    }

    NodeP assumeStatement() {
        const Token& tk = advance();
        NodeP n = node(NK::Assume, tk);
        n->kids.push_back(parse(P_CMP + 1));
        if (!isCompareOp(peek().type) || peek().type == Tok::EqEq)
            error("expected a relation such as '>', '>=', '<' or '!=' after 'assume'");
        n->op = advance().type;
        n->kids.push_back(parse(P_CMP + 1));
        return n;
    }

    NodeP whileStatement() {
        const Token& tk = advance();
        NodeP n = node(NK::While, tk);
        n->kids.push_back(expression());
        match(Tok::KwDo);
        n->blocks.push_back(block());
        expect(Tok::KwEnd, "'end'");
        return n;
    }

    NodeP forStatement() {
        const Token& tk = advance();
        NodeP n = node(NK::For, tk);
        n->name = expect(Tok::Ident, "a loop variable").text;
        if (!match(Tok::In) && !match(Tok::Assign)) error("expected 'in' or '∈'");
        n->kids.push_back(expression());
        match(Tok::KwDo);
        n->blocks.push_back(block());
        expect(Tok::KwEnd, "'end'");
        return n;
    }

    void params(Node* f) {
        expect(Tok::LParen, "'('");
        bool seenDefault = false;
        if (!check(Tok::RParen)) {
            do {
                if (check(Tok::Ellipsis)) {
                    const Token& dots = advance();
                    f->names.push_back(expect(Tok::Ident, "a name after '...'").text);
                    if (check(Tok::Assign)) error("the rest parameter cannot have a default");
                    f->defaults.push_back(nullptr);
                    f->rest = true;
                    if (check(Tok::Comma))
                        throw SyntaxError("the rest parameter must be last", dots.line, dots.col);
                    break;
                }
                const Token& nm = expect(Tok::Ident, "a parameter name");
                NodeP def;
                if (match(Tok::Assign)) {
                    def = expression();
                    seenDefault = true;
                } else if (seenDefault) {
                    throw SyntaxError("parameters with defaults must follow the required parameters", nm.line, nm.col);
                }
                f->names.push_back(nm.text);
                f->defaults.push_back(std::move(def));
            } while (match(Tok::Comma));
        }
        expect(Tok::RParen, "')'");
    }

    NodeP fnBody(NodeP f) {
        params(f.get());
        if (match(Tok::Assign)) {
            f->flag = true;
            f->kids.push_back(expression());
        } else {
            f->blocks.push_back(block());
            expect(Tok::KwEnd, "'end'");
        }
        return f;
    }

    NodeP fnStatement() {
        const Token& tk = advance();
        NodeP f = node(NK::FuncDef, tk);
        f->name = advance().text;
        return fnBody(std::move(f));
    }

    // ---------- expressions ----------

    NodeP expression() { return parse(P_LAMBDA); }

    int infixPrec(const Token& tk) const {
        switch (tk.type) {
            case Tok::Arrow: return P_LAMBDA;
            case Tok::KwTo: return P_TO;
            case Tok::KwOr: return P_OR;
            case Tok::KwAnd: return P_AND;
            case Tok::DotDot: return P_RANGE;
            case Tok::Plus:
            case Tok::Minus:
            case Tok::Union:
            case Tok::Backslash: return P_ADD;
            case Tok::Star:
            case Tok::Slash:
            case Tok::Percent:
            case Tok::Dot:
            case Tok::Cross:
            case Tok::Intersect: return P_MUL;
            case Tok::Caret: return P_POW;
            case Tok::LParen:
            case Tok::LBracket:
            case Tok::Field:
            case Tok::Bang:
            case Tok::Quote: return P_POSTFIX;
            case Tok::Ident: return tk.text == "step" ? P_NONE : P_IMPLICIT;
            case Tok::Sqrt:
            case Tok::Cbrt:
            case Tok::Sum:
            case Tok::Prod: return P_IMPLICIT;
            default: return isCompareOp(tk.type) ? P_CMP : P_NONE;
        }
    }

    NodeP parse(int minPrec) {
        NodeP left = prefix();
        for (;;) {
            int pr = infixPrec(peek());
            if (pr == P_NONE || pr < minPrec) break;
            left = infix(std::move(left), pr);
        }
        return left;
    }

    NodeP prefix() {
        const Token& tk = peek();
        switch (tk.type) {
            case Tok::Number: {
                advance();
                NodeP n = node(NK::Number, tk);
                n->lit = parseNumber(tk.text);
                return n;
            }
            case Tok::Imag: {
                advance();
                NodeP n = node(NK::Number, tk);
                n->lit = fromComplex({0.0, toDouble(parseNumber(tk.text))});
                return n;
            }
            case Tok::String: advance(); return stringNode(tk);
            case Tok::KwTrue: advance(); return node(NK::True, tk);
            case Tok::KwFalse: advance(); return node(NK::False, tk);
            case Tok::KwNil: advance(); return node(NK::Nil, tk);
            case Tok::Ident: {
                advance();
                NodeP n = node(NK::Ident, tk);
                n->name = tk.text;
                return n;
            }
            case Tok::LParen: return group();
            case Tok::LBracket: return bracket();
            case Tok::LBrace: return brace();
            case Tok::Pipe: {
                advance();
                NodeP n = node(NK::Abs, tk);
                n->kids.push_back(expression());
                expect(Tok::Pipe, "closing '|'");
                return n;
            }
            case Tok::Minus:
            case Tok::Plus:
            case Tok::Sqrt:
            case Tok::Cbrt: {
                advance();
                NodeP n = node(NK::Unary, tk);
                n->op = tk.type;
                n->kids.push_back(parse(P_UNARY));
                return n;
            }
            case Tok::KwNot: {
                advance();
                NodeP n = node(NK::Unary, tk);
                n->op = Tok::KwNot;
                n->kids.push_back(parse(P_NOT));
                return n;
            }
            case Tok::KwIf: return ifExpression();
            case Tok::Sum:
            case Tok::Prod: return sumProd();
            case Tok::KwFn: {
                advance();
                return fnBody(node(NK::FuncDef, tk));
            }
            default: error("expected an expression");
        }
    }

    NodeP infix(NodeP left, int pr) {
        const Token& tk = peek();
        switch (tk.type) {
            case Tok::Arrow: advance(); return lambdaFrom(std::move(left), tk);
            case Tok::KwTo: {
                advance();
                NodeP n = node(NK::To, tk);
                n->kids.push_back(std::move(left));
                n->lit = unitBracket();
                return n;
            }
            case Tok::KwOr:
            case Tok::KwAnd: {
                advance();
                NodeP n = node(NK::Logical, tk);
                n->op = tk.type;
                n->kids.push_back(std::move(left));
                n->kids.push_back(parse(pr + 1));
                return n;
            }
            case Tok::DotDot: {
                advance();
                NodeP n = node(NK::Range, tk);
                n->kids.push_back(std::move(left));
                n->kids.push_back(parse(P_RANGE + 1));
                if (check(Tok::Ident) && peek().text == "step") {
                    advance();
                    n->kids.push_back(parse(P_RANGE + 1));
                }
                return n;
            }
            case Tok::Caret: {
                advance();
                NodeP n = node(NK::Binary, tk);
                n->op = Tok::Caret;
                n->kids.push_back(std::move(left));
                n->kids.push_back(parse(P_UNARY));
                return n;
            }
            case Tok::Field: {
                const Token& dot = advance();
                if (!check(Tok::Ident)) error("expected a field name after '.'");
                const Token& nm = advance();
                NodeP n = node(NK::Index, dot);
                NodeP key = node(NK::String, nm);
                key->lit = newString(nm.text);
                n->kids.push_back(std::move(left));
                n->kids.push_back(std::move(key));
                n->flag = true;  // obj.name, so a following call passes the receiver
                return n;
            }
            case Tok::LParen: return call(std::move(left));
            case Tok::LBracket:
                if (left->kind == NK::Number) return unitLiteral(std::move(left));
                return index(std::move(left));
            case Tok::Bang:
            case Tok::Quote: {
                advance();
                NodeP n = node(NK::Postfix, tk);
                n->op = tk.type;
                n->kids.push_back(std::move(left));
                return n;
            }
            case Tok::Ident:
            case Tok::Sqrt:
            case Tok::Cbrt:
            case Tok::Sum:
            case Tok::Prod: {
                NodeP n = node(NK::Binary, tk);
                n->op = Tok::Star;
                n->kids.push_back(std::move(left));
                n->kids.push_back(parse(P_IMPLICIT + 1));
                return n;
            }
            default: break;
        }
        if (isCompareOp(tk.type)) return comparison(std::move(left));
        advance();
        NodeP n = node(NK::Binary, tk);
        n->op = tk.type;
        n->kids.push_back(std::move(left));
        n->kids.push_back(parse(pr + 1));
        return n;
    }

    NodeP comparison(NodeP left) {
        NodeP n = node(NK::Compare, peek());
        n->kids.push_back(std::move(left));
        while (isCompareOp(peek().type)) {
            n->ops.push_back(advance().type);
            n->kids.push_back(parse(P_CMP + 1));
        }
        return n;
    }

    NodeP lambdaFrom(NodeP left, const Token& tk) {
        NodeP n = node(NK::Lambda, tk);
        if (left->kind == NK::Ident) {
            n->names.push_back(left->name);
        } else if (left->kind == NK::Tuple) {
            for (auto& k : left->kids) {
                if (k->kind != NK::Ident) throw SyntaxError("lambda parameters must be plain names", k->line, k->col);
                n->names.push_back(k->name);
            }
        } else {
            throw SyntaxError("invalid lambda parameters", left->line, left->col);
        }
        n->kids.push_back(parse(P_LAMBDA));
        return n;
    }

    static bool isMembershipGenerator(const Node* a) {
        return a->kind == NK::Compare && a->ops.size() == 1 && a->ops[0] == Tok::In && a->kids[0]->kind == NK::Ident;
    }

    NodeP call(NodeP callee) {
        const Token& tk = advance();
        NodeP n = node(NK::Call, tk);
        n->kids.push_back(std::move(callee));

        // Arguments are kept in source order. A following '=' turns a parameter-shaped
        // list into the short function form `f(a, b = 1, ...rest) = ...`.
        struct Piece {
            NodeP node;
            std::string name;
            bool binding = false;
            bool rest = false;
            int line = 0;
            int col = 0;
        };
        std::vector<Piece> pieces;
        if (!check(Tok::RParen)) {
            do {
                if (check(Tok::Ellipsis)) {
                    const Token& dots = advance();
                    const Token& nm = expect(Tok::Ident, "a name after '...'");
                    if (check(Tok::Assign)) error("the rest parameter cannot have a default");
                    Piece p;
                    p.name = nm.text;
                    p.rest = true;
                    p.line = dots.line;
                    p.col = dots.col;
                    pieces.push_back(std::move(p));
                    if (check(Tok::Comma)) error("the rest parameter must be last");
                    break;
                }
                if (check(Tok::Ident) && peek(1).type == Tok::Assign) {
                    const Token& nm = advance();
                    advance();
                    Piece p;
                    p.name = nm.text;
                    p.binding = true;
                    p.line = nm.line;
                    p.col = nm.col;
                    p.node = expression();
                    pieces.push_back(std::move(p));
                } else {
                    Piece p;
                    p.node = expression();
                    p.line = p.node->line;
                    p.col = p.node->col;
                    if (p.node->kind == NK::Ident) p.name = p.node->name;
                    pieces.push_back(std::move(p));
                }
            } while (match(Tok::Comma));
        }
        expect(Tok::RParen, "')'");

        const Node* c = n->kids[0].get();
        bool prototype = c->kind == NK::Ident && check(Tok::Assign);
        if (prototype) {
            bool seenDefault = false;
            for (const auto& p : pieces) {
                if (p.rest) continue;
                if (p.binding) {
                    seenDefault = true;
                    continue;
                }
                if (!p.node || p.node->kind != NK::Ident) {
                    prototype = false;
                    break;
                }
                if (seenDefault)
                    throw SyntaxError("parameters with defaults must follow the required parameters", p.line, p.col);
            }
        }
        if (prototype) {
            for (auto& p : pieces) {
                n->names.push_back(p.name);
                if (p.binding) n->defaults.push_back(std::move(p.node));
                else n->defaults.push_back(nullptr);
                if (p.rest) n->rest = true;
            }
            n->flag = true;
            return n;
        }

        std::vector<NodeP> bindings;
        for (auto& p : pieces) {
            if (p.rest) throw SyntaxError("'...' is only valid in a parameter list", p.line, p.col);
            if (p.binding) {
                NodeP g = std::make_unique<Node>(NK::Generator, p.line, p.col);
                g->name = p.name;
                g->kids.push_back(std::move(p.node));
                bindings.push_back(std::move(g));
            } else {
                n->kids.push_back(std::move(p.node));
            }
        }

        bool sumName = c->kind == NK::Ident && (c->name == "sum" || c->name == "prod");
        if (sumName) {
            bool allGenerators = n->kids.size() >= 3;
            for (size_t i = 2; i < n->kids.size(); i++)
                if (!isMembershipGenerator(n->kids[i].get())) allGenerators = false;
            if ((!bindings.empty() && n->kids.size() == 2) || (bindings.empty() && allGenerators)) {
                NodeP s = node(NK::SumProd, tk);
                s->op = c->name == "sum" ? Tok::Sum : Tok::Prod;
                s->kids.push_back(std::move(n->kids[1]));
                for (auto& b : bindings) s->kids.push_back(std::move(b));
                for (size_t i = 2; i < n->kids.size(); i++) {
                    Node* m = n->kids[i].get();
                    NodeP g = std::make_unique<Node>(NK::Generator, m->line, m->col);
                    g->name = m->kids[0]->name;
                    g->kids.push_back(std::move(m->kids[1]));
                    s->kids.push_back(std::move(g));
                }
                return s;
            }
        }
        if (!bindings.empty())
            throw SyntaxError("unexpected '=' in arguments (only sum and prod take bindings like k = 1..n)",
                              bindings[0]->line, bindings[0]->col);
        // steps(expr) receives () -> expr, so the expression runs with step recording switched on.
        if (c->kind == NK::Ident && (c->name == "steps" || c->name == "working") && n->kids.size() == 2 &&
            n->kids[1]->kind != NK::Lambda) {
            NodeP lam = std::make_unique<Node>(NK::Lambda, n->kids[1]->line, n->kids[1]->col);
            lam->kids.push_back(std::move(n->kids[1]));
            n->kids[1] = std::move(lam);
        }
        return n;
    }

    NodeP index(NodeP obj) {
        const Token& tk = advance();
        NodeP n = node(NK::Index, tk);
        n->kids.push_back(std::move(obj));
        do {
            n->kids.push_back(expression());
        } while (match(Tok::Comma));
        expect(Tok::RBracket, "']'");
        return n;
    }

    Value unitBracket() {
        const Token& open = expect(Tok::LBracket, "'[' followed by a unit");
        std::string text;
        int depth = 0;
        while (!(check(Tok::RBracket) && depth == 0)) {
            if (check(Tok::Eof) || check(Tok::Newline)) error("unterminated unit");
            const Token& tk = advance();
            if (tk.type == Tok::LParen) depth++;
            if (tk.type == Tok::RParen) depth--;
            if (!text.empty()) text += ' ';
            text += tk.text;
        }
        advance();
        try {
            return parseUnitLiteral(text);
        } catch (const SedError& e) {
            throw SyntaxError(e.what(), open.line, open.col);
        }
    }

    NodeP unitLiteral(NodeP number) {
        const Token& tk = peek();
        NodeP u = node(NK::UnitConst, tk);
        u->lit = unitBracket();
        NodeP n = node(NK::Binary, tk);
        n->op = Tok::Star;
        n->kids.push_back(std::move(number));
        n->kids.push_back(std::move(u));
        return n;
    }

    NodeP group() {
        const Token& tk = advance();
        if (match(Tok::RParen)) {
            if (!check(Tok::Arrow)) error("expected '->' after ()");
            return node(NK::Tuple, tk);
        }
        NodeP e = expression();
        if (check(Tok::Comma)) {
            NodeP n = node(NK::Tuple, tk);
            n->kids.push_back(std::move(e));
            while (match(Tok::Comma)) n->kids.push_back(expression());
            expect(Tok::RParen, "')'");
            return n;
        }
        expect(Tok::RParen, "')'");
        return e;
    }

    NodeP bracket() {
        const Token& tk = advance();
        if (match(Tok::RBracket)) return node(NK::ListLit, tk);
        NodeP first = expression();
        if (match(Tok::Pipe)) return comprehension(tk, std::move(first), false, Tok::RBracket);
        std::vector<std::vector<NodeP>> rows(1);
        rows[0].push_back(std::move(first));
        for (;;) {
            if (match(Tok::Comma)) {
                if (check(Tok::RBracket)) break;
                rows.back().push_back(expression());
                continue;
            }
            if (match(Tok::Semicolon)) {
                if (check(Tok::RBracket)) break;
                rows.emplace_back();
                rows.back().push_back(expression());
                continue;
            }
            break;
        }
        expect(Tok::RBracket, "']'");
        if (rows.size() == 1) {
            NodeP n = node(NK::ListLit, tk);
            n->kids = std::move(rows[0]);
            return n;
        }
        NodeP n = node(NK::MatrixLit, tk);
        n->rows = static_cast<int>(rows.size());
        n->cols = static_cast<int>(rows[0].size());
        for (auto& r : rows) {
            if (static_cast<int>(r.size()) != n->cols)
                throw SyntaxError("all matrix rows must have the same length", tk.line, tk.col);
            for (auto& e : r) n->kids.push_back(std::move(e));
        }
        return n;
    }

    NodeP brace() {
        const Token& tk = advance();
        if (match(Tok::RBrace)) return node(NK::SetLit, tk);
        if (match(Tok::Colon)) {
            expect(Tok::RBrace, "'}' (an empty record is '{:}')");
            return node(NK::RecordLit, tk);
        }
        NodeP first = expression();
        if (match(Tok::Pipe)) return comprehension(tk, std::move(first), true, Tok::RBrace);
        if (match(Tok::Colon)) {
            NodeP n = node(NK::RecordLit, tk);
            n->kids.push_back(std::move(first));
            n->kids.push_back(expression());
            while (match(Tok::Comma)) {
                if (check(Tok::RBrace)) break;
                n->kids.push_back(expression());
                expect(Tok::Colon, "':' after a record key");
                n->kids.push_back(expression());
            }
            expect(Tok::RBrace, "'}'");
            return n;
        }
        NodeP n = node(NK::SetLit, tk);
        n->kids.push_back(std::move(first));
        while (match(Tok::Comma)) {
            if (check(Tok::RBrace)) break;
            n->kids.push_back(expression());
        }
        expect(Tok::RBrace, "'}'");
        return n;
    }

    static bool identStart(uint32_t cp) {
        return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_' || cp >= 0x80;
    }

    NodeP stringNode(const Token& tk) {
        const std::string& s = tk.text;
        bool hole = false;
        for (char c : s)
            if (c == '$') hole = true;
        auto literal = [](std::string t) {
            std::string out;
            out.reserve(t.size());
            for (char c : t) out += c == '\x01' ? '$' : c;
            return out;
        };
        if (!hole) {
            NodeP n = node(NK::String, tk);
            n->lit = newString(literal(s));
            return n;
        }
        NodeP n = node(NK::Interp, tk);
        std::string lit;
        auto flush = [&]() {
            NodeP part = std::make_unique<Node>(NK::String, tk.line, tk.col);
            part->lit = newString(literal(lit));
            n->kids.push_back(std::move(part));
            lit.clear();
        };
        for (size_t i = 0; i < s.size();) {
            if (s[i] == '\x01') {
                lit += '$';
                i++;
                continue;
            }
            if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '$') {
                lit += '$';
                i += 2;
                continue;
            }
            if (s[i] != '$') {
                lit += s[i++];
                continue;
            }
            flush();
            if (i + 1 < s.size() && s[i + 1] == '{') {
                size_t j = i + 2;
                int depth = 1;
                for (; j < s.size() && depth; j++) {
                    if (s[j] == '{') depth++;
                    else if (s[j] == '}') depth--;
                }
                if (depth) error("unterminated interpolation '${'");
                std::string inner = s.substr(i + 2, (j - 1) - (i + 2));
                if (inner.empty()) error("empty interpolation '${}'");
                n->kids.push_back(parseExpression(inner));
                i = j;
                continue;
            }
            size_t k = i + 1, len = 0;
            uint32_t cp = utf8::decode(s, k);
            if (!identStart(cp)) error("expected a name or '${...}' after '$'");
            (void)len;
            size_t start = i + 1;
            i = k;
            while (i < s.size()) {
                size_t next = i;
                uint32_t c = utf8::decode(s, next);
                bool cont = identStart(c) || (c >= '0' && c <= '9');
                if (!cont) break;
                i = next;
            }
            NodeP id = std::make_unique<Node>(NK::Ident, tk.line, tk.col);
            id->name = s.substr(start, i - start);
            n->kids.push_back(std::move(id));
        }
        flush();
        return n;
    }

    NodeP comprehension(const Token& tk, NodeP elem, bool isSet, Tok close) {
        NodeP n = node(NK::Comprehension, tk);
        n->flag = isSet;
        n->kids.push_back(std::move(elem));
        std::set<std::string> bound;
        do {
            if (check(Tok::Ident) && (peek(1).type == Tok::In || peek(1).type == Tok::Assign) &&
                !bound.count(peek().text)) {
                const Token& nm = advance();
                advance();
                NodeP g = node(NK::Generator, nm);
                g->name = nm.text;
                g->kids.push_back(expression());
                bound.insert(nm.text);
                n->kids.push_back(std::move(g));
            } else {
                n->kids.push_back(expression());
            }
        } while (match(Tok::Comma));
        expect(close, close == Tok::RBracket ? "']'" : "'}'");
        if (bound.empty()) throw SyntaxError("a comprehension needs a generator such as x ∈ S", tk.line, tk.col);
        return n;
    }

    NodeP ifExpression() {
        const Token& tk = advance();
        NodeP n = node(NK::IfExpr, tk);
        n->kids.push_back(expression());
        expect(Tok::KwThen, "'then'");
        n->kids.push_back(expression());
        while (match(Tok::KwElif)) {
            n->kids.push_back(expression());
            expect(Tok::KwThen, "'then'");
            n->kids.push_back(expression());
        }
        expect(Tok::KwElse, "'else' (an if expression needs an else branch)");
        n->kids.push_back(expression());
        return n;
    }

    NodeP sumProd() {
        const Token& tk = advance();
        expect(Tok::LParen, "'(' after ∑ or ∏");
        bool bindings = check(Tok::Ident) && (peek(1).type == Tok::In || peek(1).type == Tok::Assign);
        if (!bindings) {
            NodeP c = node(NK::Call, tk);
            NodeP callee = node(NK::Ident, tk);
            callee->name = tk.type == Tok::Sum ? "sum" : "prod";
            c->kids.push_back(std::move(callee));
            if (!check(Tok::RParen)) {
                do {
                    c->kids.push_back(expression());
                } while (match(Tok::Comma));
            }
            expect(Tok::RParen, "')'");
            return c;
        }
        std::vector<NodeP> gens;
        do {
            const Token& nm = expect(Tok::Ident, "an index variable");
            if (!match(Tok::In) && !match(Tok::Assign)) error("expected '=' or '∈'");
            NodeP g = node(NK::Generator, nm);
            g->name = nm.text;
            g->kids.push_back(expression());
            gens.push_back(std::move(g));
        } while (match(Tok::Comma));
        expect(Tok::RParen, "')'");
        NodeP n = node(NK::SumProd, tk);
        n->op = tk.type;
        n->kids.push_back(parse(P_MUL));
        for (auto& g : gens) n->kids.push_back(std::move(g));
        return n;
    }
};

NodeP parseExpression(const std::string& source) { return Parser(lex(source)).oneExpression(); }

}  // namespace

std::vector<NodeP> parseProgram(const std::vector<Token>& tokens) { return Parser(tokens).program(); }
