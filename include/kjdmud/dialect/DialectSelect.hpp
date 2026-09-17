#pragma once
#include <memory>

#include "kjdmud/dialect/BootApi.hpp"

namespace kjdmud {

class Config;

// Constructs the BootApi matching Config::dialect() (LpcDialect.hpp's
// dialectFromString() does the actual string<->enum mapping). This is
// main.cpp's one real construction site for a BootApi. Deliberately a
// small free function rather than the DialectFactory class sketched in
// src/dialect/instruct.md: a real DialectFactory's whole point is being
// handed to several independent consumers (ApplyTable, ObjectManager,
// Lexer/Parser, VM, per that file's own "how dialect flows through the
// driver" diagram), none of which exist yet. Only this one call site
// (src/main.cpp's queryMasterUid() caller) does. Building a class with
// exactly one caller and no other landed responsibility would be
// premature architecture; promote this to a real DialectFactory when a
// second consumer actually needs the same construction logic.
//
// DGD is deliberately unsupported here. DgdBootApi does not exist yet
// (out of scope this pass). Throws NotImplementedError if
// Config::dialect() resolves to LpcDialect::DGD. An unrecognized
// dialect string throws std::invalid_argument, the same as
// dialectFromString() itself.
std::unique_ptr<BootApi> makeBootApiForConfig(const Config& config);

} // namespace kjdmud
