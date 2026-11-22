#pragma once
#include <cstddef>

namespace astrae::debug {
enum class Level { Debug, Info, Warning, Error };
// Opens the app-specific external-files log. Safe to call more than once.
bool initialize() noexcept;
void shutdown() noexcept;
void write(Level level,const char *file,int line,const char *format,...) noexcept;
void write_raw(Level level,const char *message) noexcept;
bool file_ready() noexcept;
const char *file_path() noexcept;
}
#define ASTRAE_LOGD(...) ::astrae::debug::write(::astrae::debug::Level::Debug,__FILE__,__LINE__,__VA_ARGS__)
#define ASTRAE_LOGI(...) ::astrae::debug::write(::astrae::debug::Level::Info,__FILE__,__LINE__,__VA_ARGS__)
#define ASTRAE_LOGW(...) ::astrae::debug::write(::astrae::debug::Level::Warning,__FILE__,__LINE__,__VA_ARGS__)
#define ASTRAE_LOGE(...) ::astrae::debug::write(::astrae::debug::Level::Error,__FILE__,__LINE__,__VA_ARGS__)
