#pragma once

namespace upic
{
    // The sample's PCM conversion (M26): ONE symmetric, rounded conversion.
    // The encoder scales by this number and rounds to the nearest step; the
    // decoder divides by the same number. The old pair was asymmetric (the
    // encoder scaled by 32767 and truncated, the decoder divided by 32768),
    // so every save/load cycle lost 1/32768 of the level: 0.5 became
    // 0.496948 after a hundred cycles. With one scale and a rounding step
    // the round trip is a fixed point - the value stops changing after the
    // first cycle.
    constexpr float kPcmScale = 32767.0f;

    constexpr int kWaveSize = 1024;
    // v1.33: the range the 1990s UPIC reached - "the frequency range of the
    // 1990s UPIC was extended to infrasonics, starting at 0.01 Hz" and the
    // frequency table could span "as large a range as 0.01-22,050 Hz"
    // (Robindoré). These are the LIMITS of what a pitch may be; the page's
    // ambitus (below) is the interval it is drawn in.
    constexpr double kMinFreq = 0.01;
    constexpr double kMaxFreq = 22050.0;
    // The interval the clone has always drawn in: the default page's
    // ambitus. Widening the limits must not change what a page looks like.
    constexpr double kDefaultPitchLo = 20.0;
    constexpr double kDefaultPitchHi = 12000.0;
    constexpr int kMaxSampleSlots = 8;
    constexpr double kDefaultRefFreq = 261.63; // C4: base pitch for sample pitch-shift arcs

    //---------------- the page duration range (U2) ----------------
    // The real UPIC's page ran from a few milliseconds to several minutes
    // ("the range was 6 milliseconds to 12 minutes", Xenakis 1992) - the
    // drawings could sit in micro-time as well as in macro-time. The page
    // duration is the piece's clock: the same drawing can be played fast or
    // slow, and it is the one control that sets the time scale.
    constexpr double kMinPageSeconds = 0.005;  // 5 ms
    constexpr double kMaxPageSeconds = 720.0;  // 12 minutes
}
