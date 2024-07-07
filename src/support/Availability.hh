//
// Availability.hh
//
// 
//

#pragma once

// Older versions of Apple's libc++ don't have the floating point versions of std::to_chars.
// I used to check at runtime with:
//      if (__builtin_available(macOS 13.3, iOS 16.3, tvOS 16.3, watchOS 9.3, *)) { ... }
// Sadly, this doesn't work, because to_chars' availability attributes use the "strict" flag.
// The Clang docs say:
// > The flag strict disallows using API when deploying back to a platform version prior to
// > when the declaration was introduced. An attempt to use such API before its introduction
// > causes a hard error.
#ifdef __APPLE__
#   include <Availability.h>
#   include <TargetConditionals.h>
#   if (TARGET_OS_OSX && __MAC_OS_X_VERSION_MIN_REQUIRED >= 130300)
#       define FLOAT_TO_CHARS_AVAILABLE 1
#   elif (TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 160300)
#       define FLOAT_TO_CHARS_AVAILABLE 1
#   else
#       define FLOAT_TO_CHARS_AVAILABLE 0
#   endif
#else
#   define FLOAT_TO_CHARS_AVAILABLE 1
#endif
