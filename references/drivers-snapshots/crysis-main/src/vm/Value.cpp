#include "amlp/vm/Value.hpp"

namespace amlp {

bool isTruthy(const Value& v) {
    if (std::holds_alternative<std::monostate>(v.data)) return false;
    // DGD's nil (ROADMAP.md row 1.10's minimal real piece): real DGD's
    // own VAL_TRUE(v) macro (data.h) -- "(v)->number != 0 || (v)->type >
    // T_FLOAT || ..." -- is false for a nil value (type T_NIL == 0,
    // number == 0, neither disjunct holds), i.e. nil is falsy.
    if (std::holds_alternative<Nil>(v.data)) return false;
    if (auto* i = std::get_if<int64_t>(&v.data)) return *i != 0;
    if (auto* d = std::get_if<double>(&v.data)) return *d != 0.0;
    if (auto* s = std::get_if<std::string>(&v.data)) return !s->empty();
    // LDMud 'name symbol (ROADMAP.md row 1.7/1.8): real LDMud has no
    // "falsy symbol" concept at all -- a symbol is never int 0, so
    // there is no real precedent to match here, only a reasonable
    // default. Nonempty by construction (Lexer::lexQuote() requires at
    // least one identifier character), so always truthy.
    if (std::holds_alternative<Symbol>(v.data)) return true;
    if (auto* obj = std::get_if<std::shared_ptr<LpcObject>>(&v.data)) return static_cast<bool>(*obj);
    if (auto* arr = std::get_if<std::shared_ptr<Array>>(&v.data)) return static_cast<bool>(*arr);
    if (auto* map = std::get_if<std::shared_ptr<Mapping>>(&v.data)) return static_cast<bool>(*map);
    if (auto* fn = std::get_if<std::shared_ptr<Closure>>(&v.data)) return static_cast<bool>(*fn);
    // A buffer svalue is always truthy in real FluffOS (T_BUFFER's tag
    // is above T_NUMBER, so VAL_TRUE-style checks pass), independent of
    // its byte length -- a zero-length buffer is still truthy. A Buffer
    // Value always holds a non-null shared_ptr, so this is just "true".
    if (auto* buf = std::get_if<std::shared_ptr<Buffer>>(&v.data)) return static_cast<bool>(*buf);
    return false;
}

bool valuesEqual(const Value& a, const Value& b) {
    if (a.data.index() != b.data.index()) return false;
    // DGD's nil (ROADMAP.md row 1.10's minimal real piece): both sides
    // already confirmed to hold Nil by the index check above, and Nil
    // carries no data to compare, so any two nils are equal -- matching
    // real DGD's own VAL_NIL(v) macro ("(v)->type == nil.type &&
    // (v)->number == 0", always true for a value already known to be
    // nil-typed). This also means nil is never equal to plain int 0
    // under this driver's std::monostate/int64_t handling below (the
    // index check above already separates them), matching real DGD's
    // own strict-typechecking behavior where nil.type (T_NIL) and
    // int 0's type (T_INT) are different tags -- see data.cpp's own
    // "nil.type = (stricttc) ? T_NIL : T_INT;" comment in Value.hpp.
    if (std::holds_alternative<Nil>(a.data)) return true;
    if (auto* ai = std::get_if<int64_t>(&a.data)) return *ai == std::get<int64_t>(b.data);
    if (auto* ad = std::get_if<double>(&a.data)) return *ad == std::get<double>(b.data);
    if (auto* as = std::get_if<std::string>(&a.data)) return *as == std::get<std::string>(b.data);
    // LDMud 'name symbol (ROADMAP.md row 1.7/1.8): two symbols are the
    // same real LDMud value only when their names match (real LDMud
    // interns symbols as shared strings, table-comparable by pointer;
    // this driver's own Symbol is a plain std::string, so name equality
    // is the direct equivalent).
    if (auto* asym = std::get_if<Symbol>(&a.data)) return asym->name == std::get<Symbol>(b.data).name;
    if (auto* ao = std::get_if<std::shared_ptr<LpcObject>>(&a.data)) return *ao == std::get<std::shared_ptr<LpcObject>>(b.data);
    if (auto* af = std::get_if<std::shared_ptr<Closure>>(&a.data)) return *af == std::get<std::shared_ptr<Closure>>(b.data);
    // Real "==" between two buffers is pointer identity (eoperators.c
    // f_eq's T_BUFFER case: "(sp-1)->u.buf == sp->u.buf"), the same rule
    // object and closure comparison above already use. Two distinct
    // allocate_buffer() results with identical bytes are NOT equal; a
    // second variable aliasing the same buffer IS.
    if (auto* ab = std::get_if<std::shared_ptr<Buffer>>(&a.data)) return *ab == std::get<std::shared_ptr<Buffer>>(b.data);
    if (std::holds_alternative<std::monostate>(a.data)) return true;
    return false;
}

} // namespace amlp
