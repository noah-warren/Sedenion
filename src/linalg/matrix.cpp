#include "linalg/matrix.h"

#include <algorithm>
#include <cmath>

#include "cas/bridge.h"
#include "cas/expr.h"
#include "common/error.h"
#include "numeric/ops.h"
#include "stdlib/stdlib.h"
#include "vm/vm.h"

namespace {

// Elimination works on a dense row-major grid of values. Entries decide the arithmetic:
// all exact -> exact rationals, any float -> partial pivoting, any symbolic -> simplification.
enum class Mode { Exact, Float, Symbolic };

struct Grid {
    size_t rows, cols;
    std::vector<Value> d;
    Value& at(size_t i, size_t j) { return d[i * cols + j]; }
};

Mode modeOf(const std::vector<Value>& d) {
    Mode m = Mode::Exact;
    for (const auto& v : d) {
        if (isSym(v)) return Mode::Symbolic;
        if (!isNumber(v)) fail("matrix entries must be numbers or expressions, got " + typeName(v));
        if (!isExactNumber(v)) m = Mode::Float;
    }
    return m;
}

double magnitude(const Value& v) { return std::abs(toComplex(v)); }

Value simp(const Value& v, Mode m) { return m == Mode::Symbolic ? simplifyValue(v) : v; }

const ObjMatrix* matArg(const Value& v, const char* fname) {
    if (!isMatrix(v)) fail(std::string(fname) + " expects a matrix, got " + typeName(v));
    return as<ObjMatrix>(v);
}

Grid gridOf(const Value& v, const char* fname) {
    Value m = matrixFromValue(v);
    const ObjMatrix* mm = matArg(m, fname);
    return Grid{mm->rows, mm->cols, mm->data};
}

Value toValue(const Grid& g) { return newMatrix(g.rows, g.cols, g.d); }

// Reduces the grid to (reduced) row echelon form, pivoting in the first `pivotCols` columns.
// Returns the pivot columns; `det` receives the determinant of the leading square block.
std::vector<size_t> eliminate(Grid& g, size_t pivotCols, bool reduced, Value* det) {
    Mode m = modeOf(g.d);
    double eps = 0;
    if (m == Mode::Float) {
        double maxAbs = 0;
        for (const auto& v : g.d) maxAbs = std::max(maxAbs, magnitude(v));
        eps = 1e-12 * std::max(1.0, maxAbs);
    }
    auto isZero = [&](const Value& v) {
        if (m == Mode::Float) return magnitude(v) <= eps;
        return isZeroValue(v);
    };
    Value dv = Value::integer(1);
    std::vector<size_t> pivots;
    size_t r = 0;
    for (size_t c = 0; c < pivotCols && r < g.rows; c++) {
        size_t p = g.rows;
        if (m == Mode::Float) {
            double best = eps;
            for (size_t i = r; i < g.rows; i++) {
                double v = magnitude(g.at(i, c));
                if (v > best) {
                    best = v;
                    p = i;
                }
            }
        } else {
            for (size_t i = r; i < g.rows; i++)
                if (!isZero(g.at(i, c))) {
                    p = i;
                    break;
                }
        }
        if (p == g.rows) continue;
        if (p != r) {
            for (size_t j = 0; j < g.cols; j++) std::swap(g.at(p, j), g.at(r, j));
            dv = negateValue(dv);
        }
        Value piv = g.at(r, c);
        dv = simp(vmul(dv, piv), m);
        for (size_t j = c; j < g.cols; j++) g.at(r, j) = simp(vdiv(g.at(r, j), piv), m);
        g.at(r, c) = Value::integer(1);
        for (size_t i = reduced ? 0 : r + 1; i < g.rows; i++) {
            if (i == r) continue;
            Value f = g.at(i, c);
            if (isZero(f)) {
                g.at(i, c) = Value::integer(0);
                continue;
            }
            for (size_t j = c; j < g.cols; j++) g.at(i, j) = simp(vsub(g.at(i, j), vmul(f, g.at(r, j))), m);
            g.at(i, c) = Value::integer(0);
        }
        if (m == Mode::Float) {
            for (auto& v : g.d)
                if (isNumber(v) && magnitude(v) <= eps) v = Value::integer(0);
        }
        pivots.push_back(c);
        r++;
    }
    if (det) *det = pivots.size() == std::min(g.rows, pivotCols) ? dv : Value::integer(0);
    return pivots;
}

Value laplaceDet(const std::vector<Value>& d, size_t n) {
    if (n == 1) return d[0];
    if (n == 2) return vsub(vmul(d[0], d[3]), vmul(d[1], d[2]));
    Value total = Value::integer(0);
    for (size_t j = 0; j < n; j++) {
        if (isZeroValue(d[j])) continue;
        std::vector<Value> minor;
        for (size_t i = 1; i < n; i++)
            for (size_t k = 0; k < n; k++)
                if (k != j) minor.push_back(d[i * n + k]);
        Value term = vmul(d[j], laplaceDet(minor, n - 1));
        total = (j % 2 == 0) ? vadd(total, term) : vsub(total, term);
    }
    return total;
}

Value identityMatrix(size_t n) {
    std::vector<Value> d(n * n, Value::integer(0));
    for (size_t i = 0; i < n; i++) d[i * n + i] = Value::integer(1);
    return newMatrix(n, n, std::move(d));
}

Value matmul(const ObjMatrix* a, const ObjMatrix* b) {
    if (a->cols != b->rows)
        fail("matrix dimensions do not match for multiplication: " + std::to_string(a->rows) + "x" +
             std::to_string(a->cols) + " * " + std::to_string(b->rows) + "x" + std::to_string(b->cols));
    std::vector<Value> d(a->rows * b->cols);
    for (size_t i = 0; i < a->rows; i++) {
        for (size_t j = 0; j < b->cols; j++) {
            Value s = Value::integer(0);
            for (size_t k = 0; k < a->cols; k++) s = vadd(s, vmul(a->at(i, k), b->at(k, j)));
            d[i * b->cols + j] = s;
        }
    }
    return newMatrix(a->rows, b->cols, std::move(d));
}

Value columnOf(const Value& list) {
    const auto& items = as<ObjList>(list)->items;
    return newMatrix(items.size(), 1, items);
}

Value rowOf(const Value& list) {
    const auto& items = as<ObjList>(list)->items;
    return newMatrix(1, items.size(), items);
}

Value flatten(const Value& m) { return newList(as<ObjMatrix>(m)->data); }

Value matrixPower(const Value& a, const Value& e) {
    const ObjMatrix* m = as<ObjMatrix>(a);
    if (m->rows != m->cols) fail("only square matrices can be raised to a power");
    if (!isIntegerValue(e)) fail("matrix exponent must be an integer");
    int64_t k = toInt64(e, "an integer exponent");
    Value base = k < 0 ? inverse(a) : a;
    if (k < 0) k = -k;
    Value result = identityMatrix(m->rows);
    while (k) {
        if (k & 1) result = matmul(as<ObjMatrix>(result), as<ObjMatrix>(base));
        k >>= 1;
        if (k) base = matmul(as<ObjMatrix>(base), as<ObjMatrix>(base));
    }
    return result;
}

Value elementwise(BinOp op, const ObjMatrix* a, const ObjMatrix* b) {
    if (a->rows != b->rows || a->cols != b->cols)
        fail(std::string("matrix dimensions differ for ") + binOpSymbol(op) + ": " + std::to_string(a->rows) + "x" +
             std::to_string(a->cols) + " and " + std::to_string(b->rows) + "x" + std::to_string(b->cols));
    std::vector<Value> d(a->data.size());
    for (size_t i = 0; i < d.size(); i++) d[i] = binaryOp(op, a->data[i], b->data[i]);
    return newMatrix(a->rows, a->cols, std::move(d));
}

}  // namespace

Value matrixFromValue(const Value& v) {
    if (isMatrix(v)) return v;
    if (isList(v)) {
        const auto& rows = as<ObjList>(v)->items;
        if (rows.empty()) return newMatrix(0, 0, {});
        if (!isList(rows[0])) return columnOf(v);
        size_t cols = as<ObjList>(rows[0])->items.size();
        std::vector<Value> d;
        for (const auto& r : rows) {
            if (!isList(r) || as<ObjList>(r)->items.size() != cols) fail("matrix rows must be lists of equal length");
            const auto& items = as<ObjList>(r)->items;
            d.insert(d.end(), items.begin(), items.end());
        }
        return newMatrix(rows.size(), cols, std::move(d));
    }
    fail("expected a matrix, got " + typeName(v));
}

Value matrixBinary(BinOp op, const Value& a, const Value& b) {
    bool ma = isMatrix(a), mb = isMatrix(b);
    if (ma && mb) {
        const ObjMatrix *x = as<ObjMatrix>(a), *y = as<ObjMatrix>(b);
        switch (op) {
            case BinOp::Mul: return matmul(x, y);
            case BinOp::Div: return matmul(x, as<ObjMatrix>(inverse(b)));
            case BinOp::LDiv: return solveLinear(a, b);
            case BinOp::Add:
            case BinOp::Sub:
            case BinOp::Mod: return elementwise(op, x, y);
            case BinOp::Dot: return dotProduct(flatten(a), flatten(b));
            default: fail(std::string("operator ") + binOpSymbol(op) + " is not defined for two matrices");
        }
    }
    if (ma && isList(b)) {
        switch (op) {
            case BinOp::Mul: return flatten(matmul(as<ObjMatrix>(a), as<ObjMatrix>(columnOf(b))));
            case BinOp::LDiv: return solveLinear(a, b);
            default: return matrixBinary(op, a, as<ObjMatrix>(a)->cols == 1 ? columnOf(b) : rowOf(b));
        }
    }
    if (isList(a) && mb) {
        if (op == BinOp::Mul) return flatten(matmul(as<ObjMatrix>(rowOf(a)), as<ObjMatrix>(b)));
        return matrixBinary(op, as<ObjMatrix>(b)->cols == 1 ? columnOf(a) : rowOf(a), b);
    }
    if (isList(a) || isList(b) || isSet(a) || isSet(b))
        fail(std::string("unsupported operand types for ") + binOpSymbol(op) + ": " + typeName(a) + " and " + typeName(b));
    // Matrix with a scalar.
    if (ma) {
        if (op == BinOp::Pow) return matrixPower(a, b);
        if (op == BinOp::LDiv) return mapElements(a, [&](const Value& e) { return binaryOp(BinOp::Div, e, b); });
        BinOp eop = (op == BinOp::Dot || op == BinOp::Cross) ? BinOp::Mul : op;
        return mapElements(a, [&](const Value& e) { return binaryOp(eop, e, b); });
    }
    if (op == BinOp::Div) return matrixBinary(BinOp::Mul, a, inverse(b));
    if (op == BinOp::Pow) fail("cannot raise a scalar to a matrix power");
    if (op == BinOp::LDiv) return matrixBinary(BinOp::Div, b, a);
    BinOp eop = (op == BinOp::Dot || op == BinOp::Cross) ? BinOp::Mul : op;
    return mapElements(b, [&](const Value& e) { return binaryOp(eop, a, e); });
}

Value transposeValue(const Value& v) {
    if (isMatrix(v)) {
        const ObjMatrix* m = as<ObjMatrix>(v);
        std::vector<Value> d(m->data.size());
        for (size_t i = 0; i < m->rows; i++)
            for (size_t j = 0; j < m->cols; j++) d[j * m->rows + i] = m->at(i, j);
        return newMatrix(m->cols, m->rows, std::move(d));
    }
    if (isList(v)) return columnOf(v);
    if (isNumber(v) || isSym(v)) return v;
    fail("cannot transpose a value of type " + typeName(v));
}

Value determinant(const Value& v) {
    Grid g = gridOf(v, "det");
    if (g.rows != g.cols) fail("det needs a square matrix");
    if (g.rows == 0) return Value::integer(1);
    Mode m = modeOf(g.d);
    if (m == Mode::Symbolic && g.rows <= 6) return simplifyValue(laplaceDet(g.d, g.rows));
    Value det;
    eliminate(g, g.cols, false, &det);
    return det;
}

Value inverse(const Value& v) {
    Grid g = gridOf(v, "inv");
    if (g.rows != g.cols) fail("only square matrices can be inverted");
    size_t n = g.rows;
    Grid aug{n, 2 * n, std::vector<Value>(n * 2 * n, Value::integer(0))};
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) aug.at(i, j) = g.at(i, j);
        aug.at(i, n + i) = Value::integer(1);
    }
    std::vector<size_t> piv = eliminate(aug, n, true, nullptr);
    if (piv.size() < n) fail("matrix is singular and has no inverse");
    std::vector<Value> d(n * n);
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) d[i * n + j] = aug.at(i, n + j);
    return newMatrix(n, n, std::move(d));
}

Value solveLinear(const Value& A, const Value& b) {
    Grid g = gridOf(A, "solve");
    bool vectorRhs = isList(b);
    Grid rhs = vectorRhs ? Grid{as<ObjList>(b)->items.size(), 1, as<ObjList>(b)->items} : gridOf(b, "solve");
    if (rhs.rows != g.rows)
        fail("solve: right-hand side has " + std::to_string(rhs.rows) + " rows, matrix has " + std::to_string(g.rows));
    size_t n = g.cols, k = rhs.cols;
    Grid aug{g.rows, n + k, std::vector<Value>(g.rows * (n + k))};
    for (size_t i = 0; i < g.rows; i++) {
        for (size_t j = 0; j < n; j++) aug.at(i, j) = g.at(i, j);
        for (size_t j = 0; j < k; j++) aug.at(i, n + j) = rhs.at(i, j);
    }
    std::vector<size_t> piv = eliminate(aug, n, true, nullptr);
    for (size_t i = piv.size(); i < g.rows; i++)
        for (size_t j = 0; j < k; j++)
            if (!isZeroValue(aug.at(i, n + j)) &&
                !(isNumber(aug.at(i, n + j)) && magnitude(aug.at(i, n + j)) < 1e-10))
                fail("the linear system has no solution");
    if (piv.size() < n) fail("the linear system has infinitely many solutions (see rref)");
    std::vector<Value> x(n * k);
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < k; j++) x[i * k + j] = aug.at(i, n + j);
    if (vectorRhs) return newList(std::move(x));
    return newMatrix(n, k, std::move(x));
}

Value dotProduct(const Value& a, const Value& b) {
    if (!isList(a) || !isList(b)) fail("dot expects two vectors");
    const auto &x = as<ObjList>(a)->items, &y = as<ObjList>(b)->items;
    if (x.size() != y.size()) fail("dot: vectors have different lengths");
    Value s = Value::integer(0);
    for (size_t i = 0; i < x.size(); i++) s = vadd(s, vmul(x[i], y[i]));
    return s;
}

Value crossProduct(const Value& a, const Value& b) {
    if (!isList(a) || !isList(b)) fail("cross expects two vectors");
    const auto &x = as<ObjList>(a)->items, &y = as<ObjList>(b)->items;
    if (x.size() != 3 || y.size() != 3) fail("cross product needs two 3-vectors");
    return newList({vsub(vmul(x[1], y[2]), vmul(x[2], y[1])), vsub(vmul(x[2], y[0]), vmul(x[0], y[2])),
                    vsub(vmul(x[0], y[1]), vmul(x[1], y[0]))});
}

Value vectorNorm(const Value& v) {
    const std::vector<Value>& items = isList(v) ? as<ObjList>(v)->items : as<ObjMatrix>(v)->data;
    Value s = Value::integer(0);
    for (const auto& x : items) {
        if (isNumber(x) && numKind(x) == NumKind::Cplx) {
            double m = magnitude(x);
            s = vadd(s, Value::real(m * m));
        } else {
            s = vadd(s, vmul(x, x));
        }
    }
    return sqrtValue(s);
}

namespace {

Value n_det(VM&, int, Value* args) { return determinant(args[0]); }
Value n_inv(VM&, int, Value* args) { return inverse(matrixFromValue(args[0])); }
Value n_transpose(VM&, int, Value* args) { return transposeValue(args[0]); }

Value n_rref(VM&, int, Value* args) {
    Grid g = gridOf(args[0], "rref");
    eliminate(g, g.cols, true, nullptr);
    return toValue(g);
}

Value n_rank(VM&, int, Value* args) {
    Grid g = gridOf(args[0], "rank");
    return Value::integer(static_cast<int64_t>(eliminate(g, g.cols, false, nullptr).size()));
}

Value n_trace(VM&, int, Value* args) {
    Grid g = gridOf(args[0], "trace");
    if (g.rows != g.cols) fail("trace needs a square matrix");
    Value s = Value::integer(0);
    for (size_t i = 0; i < g.rows; i++) s = vadd(s, g.at(i, i));
    return s;
}

size_t sizeArg(const Value& v, const char* fname) {
    int64_t n = toInt64(v, "a size");
    if (n < 0 || n > 100000) fail(std::string(fname) + ": invalid size");
    return static_cast<size_t>(n);
}

Value n_identity(VM&, int, Value* args) { return identityMatrix(sizeArg(args[0], "identity")); }

Value filled(int argc, Value* args, int64_t fill, const char* fname) {
    size_t r = sizeArg(args[0], fname), c = argc > 1 ? sizeArg(args[1], fname) : r;
    return newMatrix(r, c, std::vector<Value>(r * c, Value::integer(fill)));
}

Value n_zeros(VM&, int argc, Value* args) { return filled(argc, args, 0, "zeros"); }
Value n_ones(VM&, int argc, Value* args) { return filled(argc, args, 1, "ones"); }

Value n_diag(VM&, int, Value* args) {
    if (isMatrix(args[0])) {
        const ObjMatrix* m = as<ObjMatrix>(args[0]);
        std::vector<Value> d;
        for (size_t i = 0; i < std::min(m->rows, m->cols); i++) d.push_back(m->at(i, i));
        return newList(std::move(d));
    }
    if (!isList(args[0])) fail("diag expects a list or a matrix");
    const auto& items = as<ObjList>(args[0])->items;
    size_t n = items.size();
    std::vector<Value> d(n * n, Value::integer(0));
    for (size_t i = 0; i < n; i++) d[i * n + i] = items[i];
    return newMatrix(n, n, std::move(d));
}

Value n_dot(VM&, int, Value* args) { return dotProduct(args[0], args[1]); }
Value n_cross(VM&, int, Value* args) { return crossProduct(args[0], args[1]); }

Value n_norm(VM&, int argc, Value* args) {
    const Value& v = args[0];
    if (!isList(v) && !isMatrix(v)) return absValue(v);
    if (argc == 1) return vectorNorm(v);
    const std::vector<Value>& items = isList(v) ? as<ObjList>(v)->items : as<ObjMatrix>(v)->data;
    const Value& p = args[1];
    if (p.isReal() && std::isinf(p.as.d)) {
        Value best = Value::integer(0);
        for (const auto& x : items) {
            Value a = absValue(x);
            if (isTruthy(compareOp(CmpOp::Gt, a, best))) best = a;
        }
        return best;
    }
    if (isIntegerValue(p) && toInt64(p) == 1) {
        Value s = Value::integer(0);
        for (const auto& x : items) s = vadd(s, absValue(x));
        return s;
    }
    double pp = toDouble(p), s = 0;
    for (const auto& x : items) s += std::pow(magnitude(x), pp);
    return Value::real(std::pow(s, 1 / pp));
}

Value n_matrix(VM& vm, int argc, Value* args) {
    if (argc == 1) return matrixFromValue(args[0]);
    size_t r = sizeArg(args[0], "matrix"), c = sizeArg(args[1], "matrix");
    if (argc < 3) return newMatrix(r, c, std::vector<Value>(r * c, Value::integer(0)));
    if (isCallable(args[2])) {
        std::vector<Value> items;
        for (size_t i = 1; i <= r; i++)
            for (size_t j = 1; j <= c; j++)
                items.push_back(vm.call(args[2], {Value::integer(static_cast<int64_t>(i)),
                                                  Value::integer(static_cast<int64_t>(j))}));
        return newMatrix(r, c, std::move(items));
    }
    std::vector<Value> items = isList(args[2]) ? as<ObjList>(args[2])->items : std::vector<Value>{};
    if (items.size() != r * c) fail("matrix(r, c, xs): xs must have r*c elements");
    return newMatrix(r, c, std::move(items));
}

Value n_size(VM&, int, Value* args) {
    if (isMatrix(args[0])) {
        const ObjMatrix* m = as<ObjMatrix>(args[0]);
        return newList({Value::integer(static_cast<int64_t>(m->rows)), Value::integer(static_cast<int64_t>(m->cols))});
    }
    if (isList(args[0])) return newList({Value::integer(static_cast<int64_t>(as<ObjList>(args[0])->items.size()))});
    fail("size expects a matrix or list");
}

Value n_row(VM&, int, Value* args) {
    const ObjMatrix* m = matArg(args[0], "row");
    int64_t i = toInt64(args[1], "a row index");
    if (i < 0) i += static_cast<int64_t>(m->rows) + 1;
    if (i < 1 || i > static_cast<int64_t>(m->rows)) fail("row index out of range");
    std::vector<Value> d;
    for (size_t j = 0; j < m->cols; j++) d.push_back(m->at(static_cast<size_t>(i - 1), j));
    return newList(std::move(d));
}

Value n_col(VM&, int, Value* args) {
    const ObjMatrix* m = matArg(args[0], "col");
    int64_t j = toInt64(args[1], "a column index");
    if (j < 0) j += static_cast<int64_t>(m->cols) + 1;
    if (j < 1 || j > static_cast<int64_t>(m->cols)) fail("column index out of range");
    std::vector<Value> d;
    for (size_t i = 0; i < m->rows; i++) d.push_back(m->at(i, static_cast<size_t>(j - 1)));
    return newList(std::move(d));
}

Value n_nullspace(VM&, int, Value* args) {
    Grid g = gridOf(args[0], "nullspace");
    std::vector<size_t> piv = eliminate(g, g.cols, true, nullptr);
    std::vector<bool> isPivot(g.cols, false);
    for (size_t c : piv) isPivot[c] = true;
    std::vector<Value> basis;
    for (size_t f = 0; f < g.cols; f++) {
        if (isPivot[f]) continue;
        std::vector<Value> v(g.cols, Value::integer(0));
        v[f] = Value::integer(1);
        for (size_t r = 0; r < piv.size(); r++) v[piv[r]] = negateValue(g.at(r, f));
        basis.push_back(newList(std::move(v)));
    }
    return newList(std::move(basis));
}

// Faddeev-LeVerrier: coefficients c[0..n] of det(xI - A), with c[n] = 1.
std::vector<Value> charpolyCoeffs(const Value& A) {
    Grid g = gridOf(A, "charpoly");
    if (g.rows != g.cols) fail("charpoly needs a square matrix");
    size_t n = g.rows;
    Mode mode = modeOf(g.d);
    Value a = toValue(g);
    std::vector<Value> c(n + 1, Value::integer(0));
    c[n] = Value::integer(1);
    Value M = newMatrix(n, n, std::vector<Value>(n * n, Value::integer(0)));
    Value I = identityMatrix(n);
    for (size_t k = 1; k <= n; k++) {
        M = vadd(matmul(as<ObjMatrix>(a), as<ObjMatrix>(M)), vmul(I, c[n - k + 1]));
        Value AM = matmul(as<ObjMatrix>(a), as<ObjMatrix>(M));
        Value tr = Value::integer(0);
        for (size_t i = 0; i < n; i++) tr = vadd(tr, as<ObjMatrix>(AM)->at(i, i));
        c[n - k] = simp(vdiv(negateValue(tr), Value::integer(static_cast<int64_t>(k))), mode);
    }
    return c;
}

cas::E charpolyExpr(const Value& A, const cas::E& x) {
    std::vector<Value> c = charpolyCoeffs(A);
    std::vector<cas::E> terms;
    for (size_t k = 0; k < c.size(); k++)
        terms.push_back(cas::mul(toExpr(c[k]), cas::pow(x, cas::num(static_cast<int64_t>(k)))));
    return cas::add(terms);
}

Value n_charpoly(VM&, int argc, Value* args) {
    cas::E x = argc > 1 ? toExpr(args[1]) : cas::sym("λ");
    if (!cas::isSymbol(x)) fail("charpoly: second argument must be a symbol");
    return fromExpr(charpolyExpr(args[0], x));
}

Value n_eigvals(VM&, int, Value* args) {
    cas::E x = cas::sym("λ");
    std::vector<cas::E> roots = cas::solve(cas::eq(charpolyExpr(args[0], x), cas::zero()), x);
    std::vector<Value> out;
    for (const auto& r : roots) out.push_back(fromExpr(r));
    return newList(std::move(out));
}

Value n_eigvecs(VM& vm, int, Value* args) {
    cas::E x = cas::sym("λ");
    Value A = matrixFromValue(args[0]);
    size_t n = as<ObjMatrix>(A)->rows;
    std::vector<cas::E> roots = cas::solve(cas::eq(charpolyExpr(A, x), cas::zero()), x);
    std::vector<Value> out;
    for (const auto& r : roots) {
        Value lam = fromExpr(r);
        Value shifted = vsub(A, vmul(identityMatrix(n), lam));
        Value arg[1] = {shifted};
        Value basis = n_nullspace(vm, 1, arg);
        out.push_back(newList({lam, basis}));
    }
    return newList(std::move(out));
}

Value n_linsolve(VM&, int, Value* args) { return solveLinear(args[0], args[1]); }

Value n_augment(VM&, int, Value* args) {
    Grid a = gridOf(args[0], "augment"), b = gridOf(args[1], "augment");
    if (a.rows != b.rows) fail("augment: matrices need the same number of rows");
    Grid r{a.rows, a.cols + b.cols, std::vector<Value>(a.rows * (a.cols + b.cols))};
    for (size_t i = 0; i < a.rows; i++) {
        for (size_t j = 0; j < a.cols; j++) r.at(i, j) = a.at(i, j);
        for (size_t j = 0; j < b.cols; j++) r.at(i, a.cols + j) = b.at(i, j);
    }
    return toValue(r);
}

}  // namespace

void registerLinalg(VM& vm) {
    const char* c = "linear algebra";
    vm.defineNative("det", 1, 1, n_det, "det(A) - determinant (also |A|)", c);
    vm.defineNative("inv", 1, 1, n_inv, "inv(A) - matrix inverse (also A^-1)", c);
    vm.defineNative("transpose", 1, 1, n_transpose, "transpose(A) - transpose (also A')", c);
    vm.defineNative("rref", 1, 1, n_rref, "rref(A) - reduced row echelon form", c);
    vm.defineNative("rank", 1, 1, n_rank, "rank(A)", c);
    vm.defineNative("trace", 1, 1, n_trace, "trace(A) - sum of the diagonal", c);
    vm.defineNative("identity", 1, 1, n_identity, "identity(n) - n x n identity matrix", c);
    vm.defineNative("eye", 1, 1, n_identity, "eye(n) - n x n identity matrix", c);
    vm.defineNative("zeros", 1, 2, n_zeros, "zeros(m, n) - zero matrix", c);
    vm.defineNative("ones", 1, 2, n_ones, "ones(m, n) - matrix of ones", c);
    vm.defineNative("diag", 1, 1, n_diag, "diag(v) - diagonal matrix; diag(A) - diagonal of A", c);
    vm.defineNative("dot", 2, 2, n_dot, "dot(u, v) - dot product (also u · v)", c);
    vm.defineNative("cross", 2, 2, n_cross, "cross(u, v) - cross product (also u × v)", c);
    vm.defineNative("norm", 1, 2, n_norm, "norm(v, p) - vector p-norm (default 2; inf for max)", c);
    vm.defineNative("matrix", 1, 3, n_matrix, "matrix(rows), matrix(m, n, xs) or matrix(m, n, (i, j) -> entry)", c);
    vm.defineNative("size", 1, 1, n_size, "size(A) - [rows, cols]", c);
    vm.defineNative("row", 2, 2, n_row, "row(A, i) - i-th row as a list", c);
    vm.defineNative("col", 2, 2, n_col, "col(A, j) - j-th column as a list", c);
    vm.defineNative("nullspace", 1, 1, n_nullspace, "nullspace(A) - basis of the null space", c);
    vm.defineNative("charpoly", 1, 2, n_charpoly, "charpoly(A, x) - characteristic polynomial det(xI - A)", c);
    vm.defineNative("eigvals", 1, 1, n_eigvals, "eigvals(A) - eigenvalues", c);
    vm.defineNative("eigvecs", 1, 1, n_eigvecs, "eigvecs(A) - [eigenvalue, eigenvector basis] pairs", c);
    vm.defineNative("linsolve", 2, 2, n_linsolve, "linsolve(A, b) - solve A x = b (also A \\ b)", c);
    vm.defineNative("augment", 2, 2, n_augment, "augment(A, B) - [A | B]", c);
}
