#include "vm/indexing.h"

#include "common/error.h"
#include "common/utf8.h"
#include "numeric/ops.h"
#include "sets/set.h"

namespace {

int64_t normIndex(const Value& i, size_t len) {
    if (!isIntegerValue(i)) fail("index must be an integer, got " + typeName(i));
    int64_t k = toInt64(i, "an integer index");
    int64_t n = static_cast<int64_t>(len);
    if (k < 0) k = n + k + 1;
    if (k < 1 || k > n)
        fail("index " + toString(i) + " is out of range (length " + std::to_string(len) + ")");
    return k - 1;
}

bool isIndexList(const Value& v) { return isRange(v) || isList(v); }

std::vector<int64_t> selection(const Value& idx, size_t len) {
    std::vector<int64_t> out;
    if (isIndexList(idx)) {
        for (const auto& v : iterableToVector(idx)) out.push_back(normIndex(v, len));
    } else {
        out.push_back(normIndex(idx, len));
    }
    return out;
}

Value indexSequence(const std::vector<Value>& items, const Value& idx) {
    if (!isIndexList(idx)) return items[static_cast<size_t>(normIndex(idx, items.size()))];
    std::vector<Value> r;
    for (int64_t k : selection(idx, items.size())) r.push_back(items[static_cast<size_t>(k)]);
    return newList(std::move(r));
}

}  // namespace

Value indexValue(const Value& obj, const Value* idx, int n) {
    if (isList(obj) || isSet(obj)) {
        if (n != 1) fail(typeName(obj) + "s take a single index");
        return indexSequence(isList(obj) ? as<ObjList>(obj)->items : as<ObjSet>(obj)->items, idx[0]);
    }
    if (isMatrix(obj)) {
        const ObjMatrix* m = as<ObjMatrix>(obj);
        if (n == 1) {
            std::vector<int64_t> rs = selection(idx[0], m->rows);
            if (!isIndexList(idx[0])) {
                size_t r = static_cast<size_t>(rs[0]);
                return newList(std::vector<Value>(m->data.begin() + static_cast<long>(r * m->cols),
                                                  m->data.begin() + static_cast<long>((r + 1) * m->cols)));
            }
            std::vector<Value> d;
            for (int64_t r : rs)
                for (size_t j = 0; j < m->cols; j++) d.push_back(m->at(static_cast<size_t>(r), j));
            return newMatrix(rs.size(), m->cols, std::move(d));
        }
        if (n == 2) {
            std::vector<int64_t> rs = selection(idx[0], m->rows), cs = selection(idx[1], m->cols);
            bool mr = isIndexList(idx[0]), mc = isIndexList(idx[1]);
            if (!mr && !mc) return m->at(static_cast<size_t>(rs[0]), static_cast<size_t>(cs[0]));
            std::vector<Value> d;
            for (int64_t r : rs)
                for (int64_t c : cs) d.push_back(m->at(static_cast<size_t>(r), static_cast<size_t>(c)));
            if (!mr || !mc) return newList(std::move(d));
            return newMatrix(rs.size(), cs.size(), std::move(d));
        }
        fail("matrices take one or two indices");
    }
    if (isString(obj)) {
        if (n != 1) fail("strings take a single index");
        const std::string& s = as<ObjString>(obj)->s;
        std::vector<std::string> chars;
        for (size_t i = 0; i < s.size();) {
            size_t start = i;
            utf8::decode(s, i);
            chars.push_back(s.substr(start, i - start));
        }
        std::string r;
        for (int64_t k : selection(idx[0], chars.size())) r += chars[static_cast<size_t>(k)];
        return newString(r);
    }
    if (isRecord(obj)) {
        if (n != 1) fail("records take a single key");
        return recordGet(obj, idx[0]);
    }
    if (isRange(obj)) {
        if (n != 1) fail("ranges take a single index");
        const ObjRange* r = as<ObjRange>(obj);
        int64_t count = rangeCount(r);
        if (!isIndexList(idx[0])) return rangeAt(r, normIndex(idx[0], static_cast<size_t>(count)));
        std::vector<Value> out;
        for (int64_t k : selection(idx[0], static_cast<size_t>(count))) out.push_back(rangeAt(r, k));
        return newList(std::move(out));
    }
    fail("cannot index a value of type " + typeName(obj));
}

void setIndexValue(const Value& obj, const Value* idx, int n, const Value& v) {
    if (isRecord(obj)) {
        if (n != 1) fail("records take a single key");
        recordSet(obj, idx[0], v);
        return;
    }
    if (isList(obj)) {
        if (n != 1 || isIndexList(idx[0])) fail("list assignment takes a single integer index");
        auto& items = as<ObjList>(obj)->items;
        items[static_cast<size_t>(normIndex(idx[0], items.size()))] = v;
        return;
    }
    if (isMatrix(obj)) {
        ObjMatrix* m = as<ObjMatrix>(obj);
        if (n == 2 && !isIndexList(idx[0]) && !isIndexList(idx[1])) {
            if (isList(v) || isMatrix(v)) fail("matrix entries must be scalars");
            m->at(static_cast<size_t>(normIndex(idx[0], m->rows)), static_cast<size_t>(normIndex(idx[1], m->cols))) = v;
            return;
        }
        if (n == 1 && !isIndexList(idx[0]) && isList(v) && as<ObjList>(v)->items.size() == m->cols) {
            size_t r = static_cast<size_t>(normIndex(idx[0], m->rows));
            for (size_t j = 0; j < m->cols; j++) m->at(r, j) = as<ObjList>(v)->items[j];
            return;
        }
        fail("matrix assignment needs A[i, j] = value or A[i] = row");
    }
    fail("cannot assign into a value of type " + typeName(obj));
}
