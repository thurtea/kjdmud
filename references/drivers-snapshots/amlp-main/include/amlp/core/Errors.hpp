#pragma once
#include <stdexcept>
#include <string>

namespace amlp {

class LpcRuntimeError : public std::runtime_error {
public:
    explicit LpcRuntimeError(const std::string& msg) : std::runtime_error(msg) {}
};

class NotImplementedError : public std::logic_error {
public:
    explicit NotImplementedError(const std::string& where)
        : std::logic_error("not implemented: " + where) {}
};

// Deliberately NOT a subclass of LpcRuntimeError: real LPC's own
// catch(expr) cannot trap this. Confirmed against the FluffOS reference
// driver's do_catch() (interpret.c): "if (max_eval_error) { pop_context
// (&econ); error(\"Can't catch eval cost too big error.\\n\"); }" -- a
// runaway loop wrapped in catch() must still be stoppable, so this one
// error type is made to bypass VM::run()'s "catch (const
// LpcRuntimeError&)" handling entirely and propagate all the way out,
// the same as it did before catch() existed.
class EvalCostError : public std::runtime_error {
public:
    explicit EvalCostError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace amlp
