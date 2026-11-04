#pragma once

// UPIC-58: enable ASIO in the stand-alone Windows build only when the
// Steinberg ASIO SDK headers are on the include path. The VS2026 project
// force-includes this header and adds JUCE/modules/juce_audio_devices/
// native/asio as a search path, so a missing SDK simply leaves ASIO off
// instead of breaking the build. The CMake build uses JUCE_ASIO directly.
#if defined (_WIN32) && __has_include (<asio.h>)
 #ifndef JUCE_ASIO
  #define JUCE_ASIO 1
 #endif
#endif
