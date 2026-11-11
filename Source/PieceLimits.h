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
// truncated rather than rejected — the app still opens it, it just cannot
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
        // --- the file / state blob itself ---
        // A piece is JSON with the samples base64-edoded inside it. 10 MB is
        // far above any real piece (the biggest test piece is ~150 KB) and
        // far below anything that could exhaust memory.
        constexpr size_t kMaxPieceBytes = 10u * 1024u * 1024u;

        // --- the arrays in a piece ---
        constexpr int kMaxPages = 100;
        constexpr int kMaxArcsPerPage = 500;
        constexpr int kMaxPointsPerArc = 1000;
        constexpr int kMaxWaveforms = 200;
        constexpr int kMaxEnvelopes = 50;
        constexpr int kMaxPointsPerEnvelope = 1000;
        constexpr int kMaxSieveZones = 200;
        constexpr int kMaxSieveRemainders = 5000;

        // --- the sample material ---
        // 60 seconds at the highest sample rate the app accepts. The same
        // 60-second cap already applies to files loaded through the file
        // chooser; this makes the piece format obey it too.
        constexpr double kMaxSampleSeconds = 60.0;
        constexpr double kMaxSampleRate = 96000.0;
        constexpr int kMaxFramesPerSample = (int) (kMaxSampleSeconds * kMaxSampleRate);
        // The decoded PCM of every slot together (16-bit stereo).
        constexpr size_t kMaxTotalSampleBytes = 50u * 1024u * 1024u;
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
