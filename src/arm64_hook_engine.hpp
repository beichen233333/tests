#pragma once
#include <cstdint>
#include <string>

namespace astrae::hooks::arm64 {
// Encode an AArch64 unconditional B instruction at pc targeting destination.
// The target must be 4-byte aligned and within the signed imm26 range.
bool encode_branch(uintptr_t pc, uintptr_t destination, uint32_t &instruction, std::string *error=nullptr);
// Relocate one AArch64 instruction from old_pc to new_pc. Unsupported or
// out-of-range PC-relative instructions fail closed instead of being copied
// with a stale PC-relative immediate.
bool relocate_instruction(uint32_t instruction, uintptr_t old_pc, uintptr_t new_pc, uint32_t &relocated, std::string *error=nullptr);
bool is_pc_relative_instruction(uint32_t instruction);
}
