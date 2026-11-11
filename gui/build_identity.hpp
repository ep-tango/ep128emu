// Shared, source-identical platform identity for the current unified release builds.
#ifndef EP128EMU_BUILD_IDENTITY_HPP
#define EP128EMU_BUILD_IDENTITY_HPP
#define EP128EMU_CUSTOM_VERSION "2.1.1"
#if defined(_WIN64) || defined(__MINGW64__)
# define EP128EMU_BUILD_PLATFORM "Windows x64"
#elif defined(_WIN32) || defined(WIN32) || defined(__MINGW32__)
# define EP128EMU_BUILD_PLATFORM "Windows x86 (Windows 7 compatible)"
#elif defined(__APPLE__) && defined(EP128EMU_MACOS_UNIVERSAL_BUILD)
# define EP128EMU_BUILD_PLATFORM "macOS Universal (ARM64 + Intel)"
#elif defined(__APPLE__) && defined(__aarch64__)
# define EP128EMU_BUILD_PLATFORM "macOS ARM64"
#elif defined(__APPLE__) && defined(__x86_64__)
# define EP128EMU_BUILD_PLATFORM "macOS Intel"
#else
# define EP128EMU_BUILD_PLATFORM "Other"
#endif
#define EP128EMU_CUSTOM_ABOUT_LINE "Custom build: " EP128EMU_BUILD_PLATFORM " - (c) 2026 Attila G.\n"
#endif
