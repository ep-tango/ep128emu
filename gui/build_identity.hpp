// Shared, source-identical platform identity for the 2.1 unified release builds.
#ifndef EP128EMU_BUILD_IDENTITY_HPP
#define EP128EMU_BUILD_IDENTITY_HPP
#define EP128EMU_CUSTOM_VERSION "2.1"
#if defined(_WIN64) || defined(__MINGW64__)
# define EP128EMU_BUILD_PLATFORM "Windows x64"
#elif defined(_WIN32) || defined(WIN32) || defined(__MINGW32__)
# define EP128EMU_BUILD_PLATFORM "Windows x86 (Windows 7 compatible)"
#elif defined(__APPLE__) && defined(__aarch64__)
# define EP128EMU_BUILD_PLATFORM "macOS ARM64"
#else
# define EP128EMU_BUILD_PLATFORM "Other"
#endif
#define EP128EMU_CUSTOM_ABOUT_LINE "Custom build: " EP128EMU_BUILD_PLATFORM " - (c) 2026 Attila G.\n"
#endif
