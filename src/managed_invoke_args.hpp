#pragma once
namespace astrae::il2cpp::invoke_args {
// il2cpp_runtime_invoke expects value-type arguments as addresses to their storage,
// but reference-type arguments as the managed object pointer itself.
inline void *value(const void *storage) noexcept { return const_cast<void *>(storage); }
inline void *reference(void *object) noexcept { return object; }
}
