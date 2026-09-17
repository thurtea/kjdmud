#pragma once
#include <string>
#include "kjdmud/vm/Value.hpp"

namespace kjdmud {

// FluffOS save_svalue / restore_svalue text used by save_variable,
// restore_variable, and MUD-mode socket framing.
void writeRealSaveValue(std::string& out, const Value& v);
Value parseRestoreVariableTopLevel(const std::string& s);
// Also used by restore_object's FluffOS .o line reader.
Value parseRealSaveValue(const std::string& s, size_t& pos);

}  // namespace kjdmud
