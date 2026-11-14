#pragma once

//==============================================================================
// The headless verification of UPICuitous (UPICuitous.exe --selftest).
//
// The checks live in SelfTest.cpp: they cover the DSP engine, the envelope
// and pitch maths, the JSON round-trip (including the files written by the
// Python version), the file I/O, the undo/redo, the crash recovery and the
// offscreen UI snapshots.
//
// They are kept out of Main.cpp on purpose: the application entry point is
// small enough to read at a glance, and the checks can grow without making
// it harder to find the app itself.
namespace upic
{
    // Runs every check and returns the number of failures (0 = all passed).
    // The log of the run is written to <temp>/upic_selftest.txt.
    int runSelfTest();
}
