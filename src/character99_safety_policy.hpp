#pragma once
namespace astrae::lobby {
// The device tombstone shows libil2cpp SIGSEGV immediately after FromXml ABI
// validation. Keep these managed factory calls disabled until the native cause
// is isolated on an instrumented build. No CMake cache can turn this on.
inline constexpr bool kExperimentalCharacter99FromXmlEnabled = false;
}
