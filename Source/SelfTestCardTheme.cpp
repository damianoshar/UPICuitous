#include <JuceHeader.h>
#include "SelfTest.h"
#include "MainComponent.h"
#include "OverlayCard.h"

//==============================================================================
// R5.12 (the review's R055): the card's look.
//
// Every shell that draws a card draws the same frame: the backdrop behind the
// card, the card's own surface, the card's one-pixel border and the six-pixel
// corner radius. The eight shells wrote those values again, once each, and
// the values were the theme's business - R5.11 moved the card's geometry and
// left the look behind.
//
// This suite is the frame's contract. It paints each shell off-screen and
// reads the pixels: the backdrop outside the card, and the card's corner,
// where the frame's fill, its border and its radius all show.
//
// The image is an ARGB one, so the backdrop keeps its alpha, and each shell is
// painted on its own (no children), so what the suite reads is the shell's own
// frame and nothing else.
//==============================================================================

namespace
{
    /** The shell's own painting, off-screen. */
    template <typename ShellType>
    juce::Image render (ShellType& shell)
    {
        juce::Image img (juce::Image::ARGB, shell.getWidth(), shell.getHeight(), true);
        juce::Graphics g (img);
        shell.paint (g);
        return img;
    }

    juce::Colour at (const juce::Image& img, int x, int y)
    {
        return img.getPixelAt (juce::jlimit (0, img.getWidth() - 1, x),
                               juce::jlimit (0, img.getHeight() - 1, y));
    }

    /** The frame the shells draw, from the values given. */
    juce::Image frameReference (const juce::Rectangle<int>& card, int w, int h,
                                juce::Colour backdrop, juce::Colour fill,
                                juce::Colour border, float radius, float borderWidth)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);
        g.fillAll (backdrop);
        g.setColour (fill);
        g.fillRoundedRectangle (card.toFloat(), radius);
        g.setColour (border);
        g.drawRoundedRectangle (card.toFloat().reduced (borderWidth * 0.5f),
                                radius, borderWidth);
        return img;
    }

    /** The first pixel where two images differ in the block at (x, y). */
    juce::String firstDifference (const juce::Image& a, const juce::Image& b,
                                  int x, int y, int size)
    {
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i)
                if (a.getPixelAt (x + i, y + j) != b.getPixelAt (x + i, y + j))
                    return juce::String (x + i) + ", " + juce::String (y + j) + ": "
                         + a.getPixelAt (x + i, y + j).toDisplayString (true) + " / "
                         + b.getPixelAt (x + i, y + j).toDisplayString (true);
        return {};
    }

    /** The hash of a block, so two shells' frames can be compared. */
    std::uint64_t blockHash (const juce::Image& img, int x, int y, int size)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i)
            {
                const auto argb = img.getPixelAt (x + i, y + j).getARGB();
                for (int b = 0; b < 4; ++b)
                {
                    h ^= (std::uint8_t) ((argb >> (b * 8)) & 0xffu);
                    h *= 1099511628211ull;
                }
            }
        return h;
    }
}

void upic::runCardThemeChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the frame the shells draw ------------------------------------------
    // The values R5.12 pins: the backdrop, the card's surface, the card's
    // border, the border's width and the corner radius.
    const juce::Colour backdrop (0xcc000000);
    const juce::Colour fill (0xff1e1e26);
    const juce::Colour border (0xff34343f);
    constexpr float radius = 6.0f;
    constexpr float borderWidth = 1.0f;

    // the card's corner, where the fill, the border and the radius show. The
    // block is twelve pixels wide, so it holds the whole six-pixel corner and
    // no title (the titles start eighteen pixels in, and the overlays' twelve).
    constexpr int corner = 12;

    std::vector<std::uint64_t> frameHashes;

    auto checkShell = [&] (const juce::String& name, auto& shell,
                           upic::overlay_card::Shell edge)
    {
        shell.setBounds (0, 0, 1280, 800);
        const auto img = render (shell);
        const auto r = upic::overlay_card::cardFor (edge, 1280, 800);
        const juce::Rectangle<int> card (r.x, r.y, r.w, r.h);

        check (at (img, 2, 2) == backdrop,
               "R5.12 " + name + " paints the backdrop behind the card",
               at (img, 2, 2).toDisplayString (true));

        const auto reference = frameReference (card, 1280, 800, backdrop, fill, border,
                                               radius, borderWidth);
        const auto difference = firstDifference (img, reference, card.getX(), card.getY(), corner);
        frameHashes.push_back (blockHash (img, card.getX(), card.getY(), corner));
        check (difference.isEmpty(),
               "R5.12 " + name + " paints the card's frame",
               difference.isEmpty() ? juce::String ("the corner matches") : difference);
    };

    {
        GroupPanel shell;
        checkShell ("the group panel", shell, upic::overlay_card::Shell::Group);
    }
    {
        ChannelPanel shell;
        checkShell ("the mixer", shell, upic::overlay_card::Shell::Channel);
    }
    {
        SequencePanel shell;
        checkShell ("the sequence panel", shell, upic::overlay_card::Shell::Sequence);
    }
    {
        AmpTableEditor shell;
        checkShell ("the amplitude tables", shell, upic::overlay_card::Shell::AmpTables);
    }
    {
        FreqTableEditor shell;
        checkShell ("the frequency table", shell, upic::overlay_card::Shell::FreqTable);
    }
    {
        PerformanceView shell;
        checkShell ("the performance view", shell, upic::overlay_card::Shell::Performance);
    }
    {
        AudioOverlay shell;
        checkShell ("the audio settings", shell, upic::overlay_card::Shell::AudioSettings);
    }
    {
        ClearOverlay shell;
        checkShell ("the clear warning", shell, upic::overlay_card::Shell::ClearPage);
    }

    // --- the eight shells paint one frame -----------------------------------
    bool oneFrame = ! frameHashes.empty();
    for (auto h : frameHashes)
        oneFrame = oneFrame && h == frameHashes.front();
    check (oneFrame, "R5.12 the eight shells paint the same frame",
           juce::String ((int) frameHashes.size()) + " shells");
}
