/* The original AArch64 ELF exports __errno_location as a 4-byte tail branch
 * to libc's __errno PLT entry. On Android/bionic, reproduce that trampoline
 * without shadowing libc's errno TLS implementation. Host builds intentionally
 * resolve __errno_location from the host libc dependency. */
#if defined(__ANDROID__)
extern "C" int *__errno(void);
extern "C" __attribute__((visibility("default"))) int *__errno_location(void){return __errno();}
#endif
