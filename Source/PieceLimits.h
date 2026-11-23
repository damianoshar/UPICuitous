#pragma once

//==============================================================================
// The limits a piece (or a plugin state blob) must respect.
//
// A piece file is untrusted input: it can come from a download, a shared
// folder or a host that hands the plugin garbage. Without a ceiling, a small
// file can declare a huge sample buffer, a thousand arcs or a million
// envelope points and make the UI freeze or the audio thread work for
// minutes on every block.
//
// Every parser clamps against these numbers, so a malformed piece is
// truncated rather than rejected β€” the app still opens it, it just cannot
// be used as a denial-of-service vector.
//
// v1.30 (UPIC): added with the security hardening (S-03).
//==============================================================================
#include <JuceHeader.h>
#include <cstdint>

namespace upic
{
    namespace limits
    {
        // --- the sample budget (the file cap below is derived from it) ---
        // The decoded PCM of every slot together, as it sits in memory
        // (32-bit float).
        constexpr size_t kMaxTotalSampleBytes = 50u * 1024u * 1024u;

        // --- the file / state blob itself ---
        // A piece is JSON with the samples base64-ed inside it. The cap is
        // derived from the sample budget above, so that everything the app
        // can hold in memory can also be written to a piece and read back:
        // a sample is 4 bytes in memory (32-bit float) and 2 + 2/3 bytes in
        // the file (16-bit, base64-ed), so the file is two thirds of the
        // memory.
        //
        // The two limits used to disagree: the 10 MB file cap bound long
        // before the 50 MB sample budget, so the app could hold - and save
        // - a piece it then refused to reopen ("Piece too large"). A 30 s
        // stereo capture at 96 kHz is 15.4 MB of base64, which the old cap
        // rejected.
        constexpr size_t kMaxPieceBytes = (kMaxTotalSampleBytes / 3u) * 2u;

        // --- WP7: the user-level asset library ---
        // A library asset is one JSON file. The cap is generous for a
        // wavetable, an envelope or a recorded sequence, but it keeps a
        // crafted file from being read in wholesale.
        constexpr size_t kMaxUserAssetBytes = 4u * 1024u * 1024u;

        // --- the ids ---
        // The arcs, the sieve zones, the wavetables and the envelopes are
        // keyed by their id (the selection, the drag map, the arc list and
        // the material references all use it). An id outside this range is
        // not an id: a huge one overflows the "next id" counter and a
        // negative one is not an id at all.
        constexpr int kMaxItemId = 1000000;

        // --- the arrays in a piece ---
        constexpr int kMaxPages = 100;
        // The real UPIC's page carried 2024 arcs (MΓΆche) or 4000
        // (RobindorΓ©); the clone follows the smaller number. This is a
        // memory and parse guard, not a realtime one - the engine's
        // overload guard (U6 option B) bounds what the audio thread is
        // asked to do, and the cursor render path (option C) made the
        // worst case fit the block budget.
        constexpr int kMaxArcsPerPage = 2024;
        constexpr int kMaxPointsPerArc = 1000;
        constexpr int kMaxWaveforms = 200;
        constexpr int kMaxEnvelopes = 50;
        constexpr int kMaxPointsPerEnvelope = 1000;
        // WP2: the amplitude conversion tables. The historical UPIC offered a
        // handful; sixteen is generous and keeps the bank one screen tall.
        constexpr int kMaxAmplitudeTables = 16;
        // WP3: the page's arc groups (the four performance groups the
        // historical page had, with room to organise).
        constexpr int kMaxGroupsPerPage = 64;
        // WP4: the performance sequences. The historical UPIC kept two;
        // 20000 points is a 10-minute gesture at the 30 Hz recorder (a
        // 12-minute page at the 6 ms historical resolution).
        constexpr int kMaxSequences = 2;
        constexpr int kMaxSequencePoints = 20000;
        constexpr int kMaxSequenceSeconds = 720;
        // WP6: the mixer channels (model only in 0.34; the engine/UI follow).
        constexpr int kMaxChannels = 16;

        constexpr int kMaxSieveZones = 200;
        constexpr int kMaxSieveRemainders = 5000;
        // The degrees of one octave in a tuning (M4). A scale with more
        // notes than this is not a scale a person can use, and the cap
        // keeps a crafted piece from carrying a huge list.
        constexpr int kMaxTuningDegrees = 256;
        // The size of a Scala tuning file (M4). A scale is a list of
        // degrees - a few kilobytes at most - so a megabyte is generous,
        // and the cap keeps a hostile file from being read in wholesale.
        constexpr size_t kMaxTuningBytes = 1024u * 1024u;

        // --- WP1: the drawn frequency tables ---
        // The New UPIC's conversion table held up to 16384 entries. A drawn
        // table needs far fewer, but the cap is the historical one so an
        // imported table never has to be truncated by surprise.
        constexpr int kMaxTablePoints = 16384;
        // The discrete table's staircase: 1..99 equal divisions per octave
        // (the same range as an arc's discrete frequency space).
        constexpr int kMaxStepDivisions = 99;

        // --- the per-sample limits ---
        // 60 seconds of material, counted at the material's OWN rate: a
        // 44.1 kHz file is 60 s (2.65 M frames), not 130 s. The cap used to
        // be counted at 96 kHz whatever the file was, so a 44.1 kHz file
        // could hold more than twice its stated length.
        constexpr double kMaxSampleSeconds = 60.0;
        // The highest rate the app keeps. The capture records at the device
        // rate and an imported file keeps its own rate, so the app has to
        // accept the rates a modern device or file can have - but never by
        // retuning the material: a rate that is changed while the frames
        // are kept plays at the wrong speed (a 192 kHz file read as 96 kHz
        // sounds at half speed).
        constexpr double kMaxSampleRate = 192000.0;
        // The frames one slot may hold: the decoded stereo float PCM has to
        // stay inside the total sample budget above (the piece loader counts
        // every slot together, so one slot cannot use more than the whole).
        // At 96 kHz the duration cap (60 s) binds first; at 192 kHz this one
        // does (about 34 s).
        constexpr int kMaxFramesPerSample = (int) (kMaxTotalSampleBytes / (2u * sizeof(float)));
        // Base64 is 4/3 of the binary it carries; allow a little slack for
        // the padding, so an oversized string is rejected before decoding.
        constexpr size_t kBase64Overhead = 2; // 4/3 rounded up, with slack

        // --- the audio parameters ---
        // The master gain slider runs 0..1.5; a loaded piece outside that
        // range would either clip or blow the mix up.
        constexpr float kMinMasterGain = 0.0f;
        constexpr float kMaxMasterGain = 1.5f;
        constexpr float kMinIntensity = 0.0f;
        constexpr float kMaxIntensity = 1.0f;

        //==============================================================================
        // Helpers: a finite check and a clamp that also rejects NaN.
        //
        // juce::jlimit() returns NaN unchanged, so a NaN read from a piece
        // would survive the clamp and reach the audio thread, where it can
        // become an out-of-bounds index. These helpers never return NaN.
        //==============================================================================
        template <typename T>
        inline bool isFinite (T v) noexcept
        {
            if constexpr (std::is_floating_point_v<T>)
                return std::isfinite (v);
            else
                return true;
        }

        template <typename T>
        inline T clampFinite (T v, T lo, T hi) noexcept
        {
            if (! isFinite (v)) return lo;
            return v < lo ? lo : (v > hi ? hi : v);
        }

        inline float clampGain (float v) noexcept   { return clampFinite (v, kMinMasterGain, kMaxMasterGain); }
        inline float clampIntensity (float v) noexcept { return clampFinite (v, kMinIntensity, kMaxIntensity); }
    }
}
