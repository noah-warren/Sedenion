#include "lexer/lexer.h"

#include <unordered_map>

#include "common/error.h"
#include "common/utf8.h"

namespace {

struct UnicodeOp {
    Tok tok;
    const char* ident;  // non-null: the symbol lexes as this identifier
};

const std::unordered_map<uint32_t, UnicodeOp>& unicodeOps() {
    static const std::unordered_map<uint32_t, UnicodeOp> m = {
        {0x221E, {Tok::Ident, "∞"}},       {0x2205, {Tok::Ident, "∅"}},       {0x222B, {Tok::Ident, "integrate"}},
        {0x00B0, {Tok::Ident, "°"}},       {0x221A, {Tok::Sqrt, nullptr}},    {0x221B, {Tok::Cbrt, nullptr}},
        {0x2211, {Tok::Sum, nullptr}},     {0x220F, {Tok::Prod, nullptr}},    {0x2208, {Tok::In, nullptr}},
        {0x2209, {Tok::NotIn, nullptr}},   {0x222A, {Tok::Union, nullptr}},   {0x2229, {Tok::Intersect, nullptr}},
        {0x2286, {Tok::SubsetEq, nullptr}}, {0x2282, {Tok::Subset, nullptr}},  {0x2287, {Tok::SupersetEq, nullptr}},
        {0x2283, {Tok::Superset, nullptr}}, {0x2216, {Tok::Backslash, nullptr}}, {0x2260, {Tok::NotEq, nullptr}},
        {0x2264, {Tok::Le, nullptr}},      {0x2265, {Tok::Ge, nullptr}},      {0x00D7, {Tok::Cross, nullptr}},
        {0x00B7, {Tok::Dot, nullptr}},     {0x22C5, {Tok::Dot, nullptr}},     {0x2219, {Tok::Dot, nullptr}},
        {0x00F7, {Tok::Slash, nullptr}},   {0x2212, {Tok::Minus, nullptr}},   {0x00AC, {Tok::KwNot, nullptr}},
        {0x2227, {Tok::KwAnd, nullptr}},   {0x2228, {Tok::KwOr, nullptr}},    {0x2192, {Tok::Arrow, nullptr}},
        {0x21A6, {Tok::Arrow, nullptr}},   {0x2261, {Tok::EqEq, nullptr}},
    };
    return m;
}

int superscriptDigit(uint32_t cp) {
    switch (cp) {
        case 0x2070: return 0;
        case 0x00B9: return 1;
        case 0x00B2: return 2;
        case 0x00B3: return 3;
        default:
            if (cp >= 0x2074 && cp <= 0x2079) return static_cast<int>(cp - 0x2074) + 4;
            return -1;
    }
}

bool isSuperscript(uint32_t cp) { return superscriptDigit(cp) >= 0 || cp == 0x207B; }

bool isIdentStart(uint32_t cp) {
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_') return true;
    if (cp < 0x80) return false;
    if (cp == 0x00A0 || cp == 0xFEFF || cp == 0x2028 || cp == 0x2029) return false;
    if (isSuperscript(cp)) return false;
    return unicodeOps().find(cp) == unicodeOps().end();
}

bool isIdentChar(uint32_t cp) { return isIdentStart(cp) || (cp >= '0' && cp <= '9'); }

const std::unordered_map<std::string, Tok>& keywords() {
    static const std::unordered_map<std::string, Tok> m = {
        {"if", Tok::KwIf},       {"then", Tok::KwThen},         {"elif", Tok::KwElif},   {"else", Tok::KwElse},
        {"end", Tok::KwEnd},     {"while", Tok::KwWhile},       {"do", Tok::KwDo},       {"for", Tok::KwFor},
        {"fn", Tok::KwFn},       {"return", Tok::KwReturn},     {"break", Tok::KwBreak}, {"continue", Tok::KwContinue},
        {"and", Tok::KwAnd},     {"or", Tok::KwOr},             {"not", Tok::KwNot},     {"true", Tok::KwTrue},
        {"false", Tok::KwFalse}, {"nil", Tok::KwNil},           {"sym", Tok::KwSym},     {"to", Tok::KwTo},
        {"in", Tok::In},         {"try", Tok::KwTry},           {"catch", Tok::KwCatch}, {"import", Tok::KwImport},
        {"as", Tok::KwAs},       {"export", Tok::KwExport},     {"rule", Tok::KwRule},   {"assume", Tok::KwAssume},
    };
    return m;
}

// Tokens after which a line break does not end the statement.
bool continuesLine(Tok t) {
    switch (t) {
        case Tok::Plus:
        case Tok::Minus:
        case Tok::Star:
        case Tok::Slash:
        case Tok::Percent:
        case Tok::Caret:
        case Tok::Assign:
        case Tok::PlusAssign:
        case Tok::MinusAssign:
        case Tok::StarAssign:
        case Tok::SlashAssign:
        case Tok::EqEq:
        case Tok::NotEq:
        case Tok::Lt:
        case Tok::Le:
        case Tok::Gt:
        case Tok::Ge:
        case Tok::Arrow:
        case Tok::DotDot:
        case Tok::Ellipsis:
        case Tok::Backslash:
        case Tok::Dot:
        case Tok::Field:
        case Tok::Colon:
        case Tok::FatArrow:
        case Tok::Cross:
        case Tok::Union:
        case Tok::Intersect:
        case Tok::In:
        case Tok::NotIn:
        case Tok::Subset:
        case Tok::SubsetEq:
        case Tok::Superset:
        case Tok::SupersetEq:
        case Tok::Comma:
        case Tok::KwAnd:
        case Tok::KwOr:
        case Tok::KwNot:
        case Tok::KwTo:
        case Tok::Sqrt:
        case Tok::Cbrt: return true;
        default: return false;
    }
}

class Lexer {
public:
    explicit Lexer(const std::string& s) : src_(s) {}

    std::vector<Token> run() {
        if (src_.compare(0, 3, "\xEF\xBB\xBF") == 0) pos_ = 3;
        while (pos_ < src_.size()) step();
        if (!out_.empty() && out_.back().type != Tok::Newline) push(Tok::Newline, "\\n", line_, col_);
        push(Tok::Eof, "", line_, col_);
        return std::move(out_);
    }

private:
    const std::string& src_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    int depth_ = 0;
    bool space_ = false;
    std::vector<Token> out_;

    [[noreturn]] void error(const std::string& msg, bool incomplete = false) {
        throw SyntaxError(msg, line_, col_, incomplete);
    }

    char peekChar(size_t k = 0) const { return pos_ + k < src_.size() ? src_[pos_ + k] : '\0'; }

    uint32_t peekCp(size_t& len) const {
        size_t i = pos_;
        uint32_t cp = utf8::decode(src_, i);
        len = i - pos_;
        return cp;
    }

    void advanceBytes(size_t n) {
        pos_ += n;
        col_++;
    }

    void push(Tok t, std::string text, int line, int col) {
        Token tk;
        tk.type = t;
        tk.text = std::move(text);
        tk.line = line;
        tk.col = col;
        tk.spaceBefore = space_;
        out_.push_back(std::move(tk));
        space_ = false;
    }

    void step() {
        char c = peekChar();
        if (c == ' ' || c == '\t' || c == '\r') {
            pos_++;
            col_++;
            space_ = true;
            return;
        }
        if (c == '\\' && (peekChar(1) == '\n' || (peekChar(1) == '\r' && peekChar(2) == '\n'))) {
            pos_ += peekChar(1) == '\n' ? 2 : 3;
            line_++;
            col_ = 1;
            space_ = true;
            return;
        }
        if (c == '\n') {
            pos_++;
            bool keep = depth_ == 0 && !out_.empty() && out_.back().type != Tok::Newline &&
                        out_.back().type != Tok::Semicolon && !continuesLine(out_.back().type);
            if (keep) push(Tok::Newline, "\\n", line_, col_);
            line_++;
            col_ = 1;
            space_ = true;
            return;
        }
        if (c == '#') {
            while (pos_ < src_.size() && src_[pos_] != '\n') pos_++;
            return;
        }
        int line = line_, col = col_;
        if (c >= '0' && c <= '9') return number(line, col);
        if (c == '"') return string(line, col);

        size_t len;
        uint32_t cp = peekCp(len);
        if (isIdentStart(cp)) return identifier(line, col);
        if (isSuperscript(cp)) return superscript(line, col);
        auto it = unicodeOps().find(cp);
        if (it != unicodeOps().end()) {
            std::string text = src_.substr(pos_, len);
            advanceBytes(len);
            if (it->second.ident) return push(Tok::Ident, it->second.ident, line, col);
            return push(it->second.tok, text, line, col);
        }
        op(line, col);
    }

    void number(int line, int col) {
        std::string text;
        auto digits = [&]() {
            while (true) {
                char d = peekChar();
                if (d >= '0' && d <= '9') {
                    text += d;
                    pos_++;
                    col_++;
                } else if (d == '_' && peekChar(1) >= '0' && peekChar(1) <= '9') {
                    pos_++;
                    col_++;
                } else {
                    break;
                }
            }
        };
        digits();
        if (peekChar() == '.' && peekChar(1) >= '0' && peekChar(1) <= '9') {
            text += '.';
            pos_++;
            col_++;
            digits();
        }
        char e = peekChar();
        if (e == 'e' || e == 'E') {
            char n1 = peekChar(1), n2 = peekChar(2);
            bool isExp = (n1 >= '0' && n1 <= '9') || ((n1 == '+' || n1 == '-') && n2 >= '0' && n2 <= '9');
            if (isExp) {
                text += 'e';
                pos_++;
                col_++;
                if (n1 == '+' || n1 == '-') {
                    text += n1;
                    pos_++;
                    col_++;
                }
                digits();
            }
        }
        if (peekChar() == 'i') {
            size_t save = pos_;
            pos_++;
            size_t len;
            bool more = pos_ < src_.size() && isIdentChar(peekCp(len));
            if (!more) {
                col_++;
                return push(Tok::Imag, text, line, col);
            }
            pos_ = save;
        }
        push(Tok::Number, text, line, col);
    }

    void string(int line, int col) {
        pos_++;
        col_++;
        std::string s;
        while (true) {
            if (pos_ >= src_.size()) error("unterminated string", true);
            char c = src_[pos_];
            if (c == '"') {
                pos_++;
                col_++;
                break;
            }
            if (c == '\n') {
                line_++;
                col_ = 1;
                s += c;
                pos_++;
                continue;
            }
            if (c == '\\') {
                char n = peekChar(1);
                pos_ += 2;
                col_ += 2;
                switch (n) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '$': s += '\x01'; break;  // a literal dollar, so it is not an interpolation
                    case '0': s += '\0'; break;
                    default: error(std::string("unknown escape sequence \\") + n);
                }
                continue;
            }
            size_t len;
            peekCp(len);
            s += src_.substr(pos_, len);
            advanceBytes(len);
        }
        push(Tok::String, s, line, col);
    }

    void identifier(int line, int col) {
        size_t start = pos_;
        while (pos_ < src_.size()) {
            size_t len;
            uint32_t cp = peekCp(len);
            if (!isIdentChar(cp)) break;
            advanceBytes(len);
        }
        std::string text = src_.substr(start, pos_ - start);
        auto it = keywords().find(text);
        Tok type = it != keywords().end() ? it->second : Tok::Ident;
        // `A union B` is the operator; `union(A, B)` stays a call to the builtin.
        if (text == "union" || text == "intersect") {
            size_t k = pos_;
            while (k < src_.size() && (src_[k] == ' ' || src_[k] == '\t')) k++;
            if (k >= src_.size() || src_[k] != '(') type = text == "union" ? Tok::Union : Tok::Intersect;
        }
        if (type == Tok::In && !out_.empty() && out_.back().type == Tok::KwNot) {
            out_.back().type = Tok::NotIn;
            out_.back().text = "not in";
            return;
        }
        push(type, text, line, col);
    }

    void superscript(int line, int col) {
        bool neg = false;
        std::string digits;
        while (pos_ < src_.size()) {
            size_t len;
            uint32_t cp = peekCp(len);
            if (cp == 0x207B && digits.empty() && !neg) {
                neg = true;
            } else if (superscriptDigit(cp) >= 0) {
                digits += static_cast<char>('0' + superscriptDigit(cp));
            } else {
                break;
            }
            advanceBytes(len);
        }
        if (digits.empty()) error("expected a superscript digit");
        bool sp = space_;
        push(Tok::Caret, "^", line, col);
        out_.back().spaceBefore = sp;
        if (neg) push(Tok::Minus, "-", line, col);
        push(Tok::Number, digits, line, col);
    }

    void op(int line, int col) {
        char c = peekChar(), n = peekChar(1);
        auto two = [&](Tok t, const char* text) {
            pos_ += 2;
            col_ += 2;
            push(t, text, line, col);
        };
        auto one = [&](Tok t) {
            std::string text(1, c);
            pos_++;
            col_++;
            push(t, text, line, col);
        };
        switch (c) {
            case '=':
                if (n == '=') return two(Tok::EqEq, "==");
                if (n == '>') return two(Tok::FatArrow, "=>");
                return one(Tok::Assign);
            case '!':
                if (n == '=') return two(Tok::NotEq, "!=");
                return one(Tok::Bang);
            case '<':
                if (n == '=') return two(Tok::Le, "<=");
                return one(Tok::Lt);
            case '>':
                if (n == '=') return two(Tok::Ge, ">=");
                return one(Tok::Gt);
            case '-':
                if (n == '>') return two(Tok::Arrow, "->");
                if (n == '=') return two(Tok::MinusAssign, "-=");
                return one(Tok::Minus);
            case '+':
                if (n == '=') return two(Tok::PlusAssign, "+=");
                return one(Tok::Plus);
            case '*':
                if (n == '*') return two(Tok::Caret, "**");
                if (n == '=') return two(Tok::StarAssign, "*=");
                return one(Tok::Star);
            case '/':
                if (n == '=') return two(Tok::SlashAssign, "/=");
                return one(Tok::Slash);
            case '&':
                if (n == '&') return two(Tok::KwAnd, "&&");
                break;
            case '.':
                if (n == '.') {
                    if (peekChar(2) == '.') {
                        pos_ += 3;
                        col_ += 3;
                        push(Tok::Ellipsis, "...", line, col);
                        return;
                    }
                    return two(Tok::DotDot, "..");
                }
                return one(Tok::Field);
            case ':': return one(Tok::Colon);
            case '%': return one(Tok::Percent);
            case '^': return one(Tok::Caret);
            case ',': return one(Tok::Comma);
            case ';': return one(Tok::Semicolon);
            case '|': return one(Tok::Pipe);
            case '\\': return one(Tok::Backslash);
            case '(':
                depth_++;
                return one(Tok::LParen);
            case '[':
                depth_++;
                return one(Tok::LBracket);
            case '{':
                depth_++;
                return one(Tok::LBrace);
            case ')':
                if (depth_ > 0) depth_--;
                return one(Tok::RParen);
            case ']':
                if (depth_ > 0) depth_--;
                return one(Tok::RBracket);
            case '}':
                if (depth_ > 0) depth_--;
                return one(Tok::RBrace);
            case '\'': {
                bool adjacent = !space_ && !out_.empty();
                if (adjacent) {
                    Tok p = out_.back().type;
                    adjacent = p == Tok::Ident || p == Tok::RParen || p == Tok::RBracket || p == Tok::Quote ||
                               p == Tok::Number;
                }
                if (!adjacent) error("unexpected ' (strings use double quotes; ' is the transpose operator)");
                return one(Tok::Quote);
            }
            default: break;
        }
        size_t len;
        uint32_t cp = peekCp(len);
        std::string ch;
        utf8::append(ch, cp);
        error("unexpected character '" + ch + "'");
    }
};

}  // namespace

std::vector<Token> lex(const std::string& src) { return Lexer(src).run(); }
