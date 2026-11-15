#pragma once

//==============================================================================
// The allocation counter (v1.33.4, M21) - the selftest's instrument.
//
// The audio thread must not allocate: a malloc there can block (and a blocked
// audio thread is a dropout), and freeing a buffer there can stall the whole
// thread. The counter counts the allocations that happen while it is armed,
// so the selftest can prove that a render does not allocate.
//
// The counter is thread-local: the app has other threads (the message thread,
// the audio device thread), and their allocations would drown out the one the
// selftest is looking for.
//
// The replacement operators that do the counting are global (operator new
// cannot be a member of a namespace): they are defined once, in SelfTest.cpp.
// The counter lives here so the checks that use it can be in their own file.
namespace upic
{
    // The calling thread's allocations since the counter was cleared.
    inline thread_local int allocCount = 0;

    // Is the counter armed? (Only the selftest arms it.)
    inline thread_local bool allocCounting = false;
}
