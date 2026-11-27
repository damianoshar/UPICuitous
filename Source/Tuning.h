#pragma once

//==============================================================================
// The tuning (M4).
//
// A tuning says which pitch a step of the scale is: the degrees of one
// octave, in cents above the base. Without one the app is in 12-tone equal
// temperament with A440 - the mapping it has always used, so a piece
// without a tuning draws and sounds exactly as it did.
//
// The Scala (.scl) format is the file format for a tuning:
//
//   ! a comment
//   The description of the scale
//     12
//   !
//    100.0
//    200.0
//     ...
//    1100.0
//    2/1
//
// The rules of the format (huygens-fokker.org/scala/scl_format.html):
//
//  - the first line that is not a comment is the description, the second is
//    the number of notes - the number of pitch lines that follow;
//  - a pitch line is a ratio ("9/8") or a value in cents ("100.0"); a value
//    with a period is cents, everything else is a ratio, so a plain "2" is
//    2/1. Anything after the value on the line is ignored ("100.0 C#");
//  - the first note (1/1, 0 cents) is implicit - it is not in the files -
//    but some files list it, and both are accepted;
//  - the last note is the octave (2/1, 1200 cents).
//
// The scale's first note (1/1) is the base: it sits where the base note
// sits in 12-tone equal temperament (middle C by default), and the scale
// repeats every octave. The steps between two degrees are interpolated
// linearly - the usual "linear" mapping, and the one every tuning-aware
// tool uses.
//
// With a scale loaded, the "step axis" - the axis the sieve's m/R works on -
// becomes the scale's own degrees: the sieve's numbers then mean "every
// n-th degree of this scale" instead of "every n-th semitone". A tuning
// with twelve degrees therefore behaves like 12-TET, and a seven-note scale
// gets its octave at m = 7.
//
// The arcs themselves hold frequencies, not pitches, so the engine is not
// touched by a tuning: the drawing snaps to the scale, and what was drawn
// is what is played.
//==============================================================================
#include <JuceHeader.h>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "PieceLimits.h"
#include "UpicConstants.h"

namespace upic
{
    struct Tuning
    {
        juce::String name;            // the file's description
        std::vector<double> cents;    // the degrees of one octave: the base (0 cents) first, the octave itself is implied
        int  baseNote = 60;           // the MIDI note the base degree sits on (middle C)
        bool custom = false;          // false = 12-tone equal temperament

        bool isDefault() const noexcept { return ! custom; }

        // the degrees of one octave (12 in 12-TET)
        int degrees() const noexcept { return custom ? (int) cents.size() : 12; }

        // The pitch of the base degree: the base note in 12-tone equal
        // temperament (middle C by default).
        double baseFreq() const noexcept
        {
            return 440.0 * std::pow (2.0, ((double) baseNote - 69.0) / 12.0);
        }

        // The cents of a degree. The scale repeats every octave, and the
        // steps between two degrees are interpolated.
        double degreeToCents (double degree) const noexcept
        {
            if (! custom || cents.empty()) return degree * 100.0;

            const int n = (int) cents.size();
            const double oct = std::floor (degree / (double) n);
            const double local = degree - oct * (double) n;      // [0, n)
            const int i = (int) std::floor (local);
            const double frac = local - (double) i;
            const double c0 = cents[(size_t) juce::jlimit (0, n - 1, i)];
            const double c1 = (i + 1 < n) ? cents[(size_t) (i + 1)] : 1200.0;
            return oct * 1200.0 + c0 + (c1 - c0) * frac;
        }

        // The degree of a pitch in cents - the inverse of degreeToCents.
        double centsToDegree (double c) const noexcept
        {
            if (! custom || cents.empty()) return c / 100.0;

            const int n = (int) cents.size();
            const double oct = std::floor (c / 1200.0);
            const double local = c - oct * 1200.0;                // [0, 1200)
            int i = 0;
            while (i < n - 1 && cents[(size_t) (i + 1)] <= local) ++i;
            const double c0 = cents[(size_t) i];
            const double c1 = (i + 1 < n) ? cents[(size_t) (i + 1)] : 1200.0;
            const double frac = (c1 > c0) ? (local - c0) / (c1 - c0) : 0.0;
            return oct * (double) n + (double) i + frac;
        }

        // The pitch of a step of the axis: a semitone (counted as a MIDI
        // number) in 12-TET, a degree of the scale in a tuning.
        double stepToFreq (double step) const noexcept
        {
            if (! custom || cents.empty()) return 440.0 * std::pow (2.0, (step - 69.0) / 12.0);
            return baseFreq() * std::exp2 (degreeToCents (step) / 1200.0);
        }

        // The step of the axis a pitch falls on.
        double freqToStep (double hz) const noexcept
        {
            hz = juce::jlimit (kMinFreq, kMaxFreq, hz);
            if (! custom || cents.empty()) return 69.0 + 12.0 * std::log2 (hz / 440.0);
            return centsToDegree (1200.0 * std::log2 (hz / baseFreq()));
        }

        // The same tuning seen through the MIDI numbers. A MIDI number is a
        // twelfth of an octave, so it is not a scale degree: the scale's
        // degrees are spread over the MIDI numbers.
        double midiToFreq (double midi) const noexcept
        {
            if (! custom || cents.empty()) return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
            return stepToFreq ((midi - (double) baseNote) * (double) degrees() / 12.0);
        }

        double freqToMidi (double hz) const noexcept
        {
            if (! custom || cents.empty())
            {
                hz = juce::jlimit (kMinFreq, kMaxFreq, hz);
                return 69.0 + 12.0 * std::log2 (hz / 440.0);
            }
            return (double) baseNote + freqToStep (hz) * 12.0 / (double) degrees();
        }

        // The nearest step of the axis: what the chromatic snap lands on.
        double snapToStep (double hz) const noexcept
        {
            if (! custom || cents.empty())
            {
                double mn = std::round (freqToMidi (hz));
                mn = juce::jlimit (12.0, 120.0, mn);
                return midiToFreq (mn);
            }
            return stepToFreq (std::round (freqToStep (hz)));
        }

        // Is a step of the axis inside the world? In 12-TET the MIDI
        // numbers 12..120 are the audible ones; a tuning's degrees are
        // bounded by the frequencies instead.
        bool stepInRange (double step) const noexcept
        {
            if (! custom || cents.empty()) return step >= 12.0 && step <= 120.0;
            const double f = stepToFreq (step);
            return f >= kMinFreq && f <= kMaxFreq;
        }

        // A step of the axis: its number and the pitch it sounds at.
        struct AxisStep
        {
            double step = 0.0;
            double freq = 0.0;
        };

        // The steps of the axis inside a frequency range - the guide lines
        // the canvas draws. In 12-tone equal temperament the steps are the
        // MIDI numbers 12..120 (the audible ones), in a tuning they are the
        // scale's degrees.
        std::vector<AxisStep> axisStepsInRange (double lo, double hi) const noexcept
        {
            std::vector<AxisStep> out;

            if (! custom || cents.empty())
            {
                for (int midi = 12; midi <= 120; ++midi)
                {
                    const double f = midiToFreq ((double) midi);
                    if (f >= lo && f <= hi) out.push_back ({ (double) midi, f });
                }
                return out;
            }

            const double d0 = std::floor (freqToStep (lo));
            const double d1 = std::ceil  (freqToStep (hi));
            const int count = (int) juce::jlimit (0, 4096, (int) (d1 - d0) + 1);
            for (int i = 0; i < count; ++i)
            {
                const double step = d0 + (double) i;
                const double f = stepToFreq (step);
                if (f >= lo && f <= hi) out.push_back ({ step, f });
            }
            return out;
        }

        // The frequencies of the axis's steps inside a frequency range.
        std::vector<double> stepFreqsInRange (double lo, double hi) const noexcept
        {
            std::vector<double> out;
            for (const auto& s : axisStepsInRange (lo, hi)) out.push_back (s.freq);
            return out;
        }

        // The name the user sees: the file's description, or the default.
        juce::String describe() const
        {
            if (! custom) return "12-tone equal temperament";
            const juce::String n = name.trim();
            return n.isNotEmpty() ? n : juce::String ("a tuning of ") + juce::String (degrees()) + " notes";
        }

        // R2.10: the file format of a tuning is a different thing from what
        // a tuning is. It lives at the edge (Source/TuningCodec.h), where
        // the JUCE value types are kept.
    };

    // The app's default: 12-tone equal temperament with A440.
    inline const Tuning& defaultTuning()
    {
        static const Tuning t;
        return t;
    }

    // A number, and nothing but the number (JUCE's getDoubleValue() reads
    // the number and ignores the rest, so the text is checked first).
    inline bool parseWholeNumber (const juce::String& text, long long& out) noexcept
    {
        const juce::String t = text.trim();
        if (t.isEmpty()) return false;

        const char* start = t.toRawUTF8();
        char* end = nullptr;
        errno = 0;
        const long long v = std::strtoll (start, &end, 10);
        if (end == start || *end != 0 || errno == ERANGE) return false;
        out = v;
        return true;
    }

    inline bool parseWholeNumber (const juce::String& text, int& out) noexcept
    {
        long long v = 0;
        if (! parseWholeNumber (text, v)) return false;
        if (v < (long long) std::numeric_limits<int>::min()
            || v > (long long) std::numeric_limits<int>::max()) return false;
        out = (int) v;
        return true;
    }

    inline bool parseDecimalNumber (const juce::String& text, double& out) noexcept
    {
        const juce::String t = text.trim();
        if (t.isEmpty()) return false;

        const char* start = t.toRawUTF8();
        char* end = nullptr;
        const double v = std::strtod (start, &end);
        if (end == start || *end != 0) return false;
        if (! std::isfinite (v)) return false;
        out = v;
        return true;
    }

    // One pitch value of a Scala file: a ratio ("9/8", "2") or a value in
    // cents ("100.0", "408."). A value that starts with a period is an
    // extension of the format: it is relative to the pitch before it.
    inline bool parseScalaValue (const juce::String& word, double previous, double& centsOut) noexcept
    {
        const juce::String w = word.trim();
        if (w.isEmpty()) return false;

        // a ratio: 9/8 is 203.91 cents
        if (w.containsChar ('/'))
        {
            const int slash = w.indexOfChar ('/');
            if (w.indexOfChar ('/', slash + 1) >= 0) return false;   // only one slash
            long long a = 0, b = 0;
            if (! parseWholeNumber (w.substring (0, slash), a)) return false;
            if (! parseWholeNumber (w.substring (slash + 1), b)) return false;
            if (a <= 0 || b <= 0) return false;   // a negative ratio is meaningless
            centsOut = 1200.0 * std::log2 ((double) a / (double) b);
            return true;
        }

        // a value in cents. The format says: a value with a period is a
        // cents value, anything else is a ratio - so a plain "2" is 2/1.
        if (w.containsChar ('.'))
        {
            double v = 0.0;
            if (! parseDecimalNumber (w, v)) return false;
            centsOut = w.startsWithChar ('.') ? previous + v : v;
            return true;
        }

        // a plain integer is the ratio n/1
        long long n = 0;
        if (! parseWholeNumber (w, n)) return false;
        if (n <= 0) return false;
        centsOut = 1200.0 * std::log2 ((double) n);
        return true;
    }

    // One pitch line of a Scala file: the value and, after it, anything
    // else ("100.0 C#" - the rest is ignored). The value is the first word
    // that reads as one (some files put the note name first).
    inline bool parseScalaPitch (const juce::String& line, double previous, double& centsOut) noexcept
    {
        std::istringstream stream (line.toStdString());
        std::string word;
        while (stream >> word)
            if (parseScalaValue (juce::String (word), previous, centsOut)) return true;
        return false;
    }

    // Parses a Scala (.scl) file. Returns false and puts the reason in
    // `error` when the file is not a tuning.
    inline bool parseScalaTuning (const juce::String& text, Tuning& out, juce::String& error)
    {
        out = Tuning{};
        error.clear();

        // the lines that are not comments and not empty
        std::vector<juce::String> content;
        {
            std::istringstream stream (text.toStdString());
            std::string raw;
            while (std::getline (stream, raw))
            {
                juce::String line (raw);
                const int bang = line.indexOfChar ('!');
                if (bang >= 0) line = line.substring (0, bang);
                line = line.trim();
                if (line.isNotEmpty()) content.push_back (line);
            }
        }

        if (content.size() < 2)
        {
            error = "the file is too short to be a Scala tuning";
            return false;
        }

        const juce::String description = content[0];

        int count = 0;
        if (! parseWholeNumber (content[1], count))
        {
            error = "'" + content[1] + "' is not the number of notes";
            return false;
        }
        if (count < 0 || count > limits::kMaxTuningDegrees + 1)
        {
            error = "the scale has " + juce::String (count) + " notes (0 - "
                    + juce::String (limits::kMaxTuningDegrees) + ")";
            return false;
        }

        const std::vector<juce::String> pitchLines (content.begin() + 2, content.end());
        const int lines = (int) pitchLines.size();
        // The format says the number of notes is the number of pitch lines.
        // The files in the wild also count the implicit 1/1, or leave the
        // octave out of the count, so a count that is one off is accepted
        // too - anything else is a broken file.
        if (lines != count && lines != count + 1 && lines != count - 1)
        {
            error = "the file says " + juce::String (count) + " notes but carries "
                    + juce::String (lines) + " pitches";
            return false;
        }

        std::vector<double> pitches;
        pitches.reserve (pitchLines.size());
        double previous = 0.0;
        for (size_t i = 0; i < pitchLines.size(); ++i)
        {
            double c = 0.0;
            if (! parseScalaPitch (pitchLines[i], previous, c))
            {
                error = "'" + pitchLines[i] + "' is not a pitch";
                return false;
            }
            pitches.push_back (c);
            previous = c;
        }

        if (pitches.empty())
        {
            // a scale of no notes is legal (the 1/1 is implicit), but it is
            // not a scale a person can use
            error = "the scale has no notes";
            return false;
        }

        // the pitches must rise
        for (size_t i = 1; i < pitches.size(); ++i)
            if (pitches[i] <= pitches[i-1])
            {
                error = "the pitches must rise";
                return false;
            }

        // the base (1/1) is implicit, but some files list it
        if (std::abs (pitches.front()) > 0.01)
        {
            if (pitches.front() < 0.0)
            {
                error = "the first pitch is below the base";
                return false;
            }
        }
        else
        {
            pitches.erase (pitches.begin());
        }

        // the last pitch is the octave
        if (pitches.empty() || std::abs (pitches.back() - 1200.0) > 0.01)
        {
            error = "the last pitch must be the octave (1200 cents)";
            return false;
        }
        pitches.pop_back();   // the octave repeats the base

        // the scale's degrees: the base (0 cents) and the notes above it
        std::vector<double> degrees;
        degrees.reserve (pitches.size() + 1);
        degrees.push_back (0.0);
        for (double c : pitches)
        {
            if (c <= degrees.back())
            {
                error = "the pitches must rise";
                return false;
            }
            if (c >= 1200.0)
            {
                error = "a pitch is above the octave";
                return false;
            }
            degrees.push_back (c);
        }

        if ((int) degrees.size() > limits::kMaxTuningDegrees)
        {
            error = "the scale has too many notes";
            return false;
        }

        out.name = description;
        out.cents = std::move (degrees);
        out.baseNote = 60;
        out.custom = true;
        return true;
    }
}
