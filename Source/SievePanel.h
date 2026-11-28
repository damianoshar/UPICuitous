#pragma once

//==============================================================================
// R5.7 (the review's R049, section 28): the sieve panel.
//
// The sieve quantises the drawing's PITCHES: only the pitches n mod m in the
// remainder list are allowed, so the curve becomes a staircase of steps. The
// global magnets - the stick (how strongly the drawing is pulled toward
// whatever magnet is active) and the time grid (which quantises the TIMES) -
// are the sieve's neighbours: every page and every sieve zone follows them.
//
// The review asks for the frequency and the sieve controls to be decomposed:
//
//   "| `buildSieveControls()`                                   | SievePanel         |"
//   "| `buildFrequencyControls()`, `pushSpaceDefs()`            | FrequencyInspector |"
//   (section 28)
//
// This header is the sieve panel's description: which controls it holds, in
// which cards, what its remainder list looks like as text, and what every
// control says about itself.
//
// It holds no widget and no document. The app binds the roles to its own
// controls (the app owns the widgets), and the layout asks the description
// what to draw.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include "InspectorCell.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace upic::sieve_panel
{
    using Kind = upic::inspector::Kind;

    /** A control of the sieve panel. */
    enum class Control
    {
        On,           // the sieve's switch
        Modulus,      // the m - the octave is divided into m equal steps
        Remainders,   // the R - which of the m divisions are allowed
        Preset,       // the preset library
        Stickiness,   // the global stick
        TimeGrid,     // the global time grid
        count
    };

    /** One cell of one of the sieve panel's cards. */
    struct Cell
    {
        Control control = Control::On;
        Kind kind = Kind::Plain;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the sieve panel says about itself. */
    struct Description
    {
        const char* name = "";             // the panel's name
        std::vector<Cell> card;             // the sieve card's cells, in order
        std::vector<Cell> magnets;          // the global magnets card's cells
    };

    /** The sieve panel's description. */
    inline const Description& describe()
    {
        static const Description description =
        {
            "Sieve",
            {
                // the switch, the preset library, the m and the R
                { Control::On,         Kind::Plain,  0,  64 },
                { Control::Preset,     Kind::Plain,  0, 150 },
                { Control::Modulus,    Kind::Inline, 18,  52 },
                { Control::Remainders, Kind::Inline, 18,  84 }
            },
            {
                // the stick and the time grid - the GLOBAL magnets
                { Control::Stickiness, Kind::Knob,     84,  0 },
                { Control::TimeGrid,   Kind::Labelled, 86, 84 }
            }
        };
        return description;
    }

    /** Every control the sieve panel knows. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::On, Control::Modulus, Control::Remainders,
            Control::Preset, Control::Stickiness, Control::TimeGrid
        };
        return controls;
    }

    //---------------------------------------------------------------------------
    // The modulus' domain: the octave is divided into m equal steps. A
    // modulus outside the domain is refused - the last valid one is kept
    // while the field is being typed.
    constexpr int kMinModulus = 2;
    constexpr int kMaxModulus = 120;

    //---------------------------------------------------------------------------
    // The time grid's divisions. They are ONE list: the dropdown offers them,
    // the command reads them and the loader puts the page's grid back on the
    // right one. Before, the labels and the values were two lists that had
    // to agree.
    struct Division
    {
        const char* label = "";
        double value = 0.0;
    };

    inline const std::vector<Division>& timeGridDivisions()
    {
        static const std::vector<Division> divisions =
        {
            { "Off",   0.0 },
            { "0.5/s", 0.5 },
            { "1/s",   1.0 },
            { "2/s",   2.0 },
            { "4/s",   4.0 },
            { "8/s",   8.0 },
            { "16/s", 16.0 }
        };
        return divisions;
    }

    /** The division a time grid is (the last one that matches, 0 when none
        does - "Off" is the first division, so an unknown grid is off). */
    inline int timeGridDivisionIndex (double value)
    {
        const auto& divisions = timeGridDivisions();
        int index = 0;
        for (size_t i = 0; i < divisions.size(); ++i)
            if (std::abs (divisions[i].value - value) < 1e-9) index = (int) i;
        return index;
    }

    //---------------------------------------------------------------------------
    // The remainder list's text format: "0,4,7" is read and written here.
    //
    // The list is separated by a comma, a semicolon or a space. A token that
    // is not a number is skipped (not refused): "0,4,x,7" is 0, 4 and 7. A
    // remainder is taken modulo the modulus, so a negative one lands inside
    // the octave.

    /** The remainder list as text ("0,4,7"). */
    inline std::string formatRemainders (const std::vector<int>& remainders)
    {
        std::string out;
        for (size_t i = 0; i < remainders.size(); ++i)
        {
            if (i > 0) out += ",";
            out += std::to_string (remainders[i]);
        }
        return out;
    }

    namespace detail
    {
        inline bool isWhitespace (char c)
        {
            return c == ' ' || (c >= 9 && c <= 13);
        }

        inline bool isSeparator (char c)
        {
            return c == ',' || c == ';' || c == ' ' || c == '\t';
        }

        inline std::string trim (const std::string& text)
        {
            size_t first = 0;
            size_t last = text.size();
            while (first < last && isWhitespace (text[first])) ++first;
            while (last > first && isWhitespace (text[last - 1])) --last;
            return text.substr (first, last - first);
        }

        // The number a token carries: a sign and the digits that follow it,
        // the way atoi() reads it ("1-2" is 1, "-" is 0). A number too big
        // for an int is clamped - the old atoi() was undefined there.
        inline int tokenValue (const std::string& token)
        {
            size_t i = 0;
            bool negative = false;
            if (i < token.size() && token[i] == '-') { negative = true; ++i; }
            long long value = 0;
            bool any = false;
            for (; i < token.size() && token[i] >= '0' && token[i] <= '9'; ++i)
            {
                any = true;
                value = value * 10 + (token[i] - '0');
                if (value > 2147483647LL) { value = 2147483647LL; break; }
            }
            if (! any) return 0;
            if (negative) value = -value;
            if (value > 2147483647LL) return 2147483647;
            if (value < -2147483648LL) return (int) -2147483648LL;
            return (int) value;
        }
    }

    /** The remainder list a text carries (the tokens that are not numbers are
        skipped, and each remainder is taken modulo the modulus). */
    inline std::vector<int> parseRemainders (const std::string& text, int modulus)
    {
        std::vector<int> out;
        if (modulus < 1) return out;

        std::string token;
        auto take = [&out, modulus] (const std::string& raw)
        {
            const auto t = detail::trim (raw);
            if (t.empty()) return;
            for (char c : t)
                if (c != '-' && (c < '0' || c > '9')) return;   // not a number
            const int value = detail::tokenValue (t);
            out.push_back (((value % modulus) + modulus) % modulus);
        };

        for (char c : text)
        {
            if (detail::isSeparator (c)) { take (token); token.clear(); }
            else token += c;
        }
        take (token);
        return out;
    }

    //---------------------------------------------------------------------------
    // The wording: what every control says about itself.
    inline const char* tooltip (Control control)
    {
        switch (control)
        {
            case Control::On:
                return "The sieve quantises the drawing's PITCHES: only the\n"
                       "pitches n mod m in the remainder list are allowed,\n"
                       "so the curve becomes a staircase of steps.\n"
                       "The allowed pitches are drawn as guides on the page.\n"
                       "Inside a sieve zone the zone's own sieve wins.";
            case Control::Modulus:
                return "The sieve's modulus: the octave is divided into m\n"
                       "equal steps. 12 means semitones.";
            case Control::Remainders:
                return "The sieve's remainders: which of the m divisions\n"
                       "are allowed. 0 leaves only the C's (an octave sieve),\n"
                       "0,4,7 is a major triad, 0,7 is C and G.";
            case Control::Preset:
                return "Sieve presets: they set the m/R and switch the\n"
                       "sieve on.";
            case Control::Stickiness:
                return "How strongly the drawing is pulled toward the\n"
                       "magnet: the sieve (the pitches), a zone's own\n"
                       "sieve, and the time grid (the times). 1 is a\n"
                       "hard snap, 0 leaves the drawing free.\n"
                       "GLOBAL: this value applies outside the zones.\n"
                       "A zone carries its own stick (right-click it);\n"
                       "until you set one it follows this value.";
            case Control::TimeGrid:
                return "Quantises the drawing's TIMES (the sieve quantises\n"
                       "the pitches). The points are pulled toward the grid\n"
                       "by Stick, so the curve takes the grid's shape.\n"
                       "Off leaves the times free.\n"
                       "GLOBAL: a sieve zone can set its own time grid,\n"
                       "which applies inside the zone.";
            default:
                return "";
        }
    }
}
