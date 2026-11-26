/*
    UPICuitous - the rectangle the layout engines share.

    The layout engines are JUCE-free, so they cannot call juce::Rectangle's
    removeFromTop() and friends. This header holds the plain rectangle the
    engines pass around and repeats the JUCE semantics of the rectangle
    operations they need - including the clamping - so every engine computes
    the same pixels.
*/

#pragma once

namespace upic::layout
{
    /** A half-open pixel rectangle: x .. x+w, y .. y+h. */
    struct Rect
    {
        int x = 0, y = 0, w = 0, h = 0;

        int getRight()  const noexcept { return x + w; }
        int getBottom() const noexcept { return y + h; }
    };

    namespace rect
    {
        inline int maxOf (int a, int b) noexcept { return a > b ? a : b; }
        inline int minOf (int a, int b) noexcept { return a < b ? a : b; }
        inline int clamp (int value, int lo, int hi) noexcept
        {
            return value < lo ? lo : (value > hi ? hi : value);
        }

        /** juce::Rectangle::reduced */
        inline Rect reduced (Rect r, int deltaX, int deltaY) noexcept
        {
            return { r.x + deltaX, r.y + deltaY,
                     maxOf (0, r.w - deltaX * 2),
                     maxOf (0, r.h - deltaY * 2) };
        }

        /** juce::Rectangle::removeFromTop - takes the strip off the top. */
        inline Rect removeFromTop (Rect& r, int amount) noexcept
        {
            const int h = minOf (amount, r.h);
            const Rect out { r.x, r.y, r.w, h };
            r.y += h;
            r.h -= h;
            return out;
        }

        /** juce::Rectangle::removeFromBottom - takes the strip off the bottom. */
        inline Rect removeFromBottom (Rect& r, int amount) noexcept
        {
            const int newHeight = maxOf (0, r.h - amount);
            const Rect out { r.x, r.y + newHeight, r.w, r.h - newHeight };
            r.h = newHeight;
            return out;
        }

        /** juce::Rectangle::removeFromLeft - takes the strip off the left. */
        inline Rect removeFromLeft (Rect& r, int amount) noexcept
        {
            const int w = minOf (amount, r.w);
            const Rect out { r.x, r.y, w, r.h };
            r.x += w;
            r.w -= w;
            return out;
        }

        /** juce::Rectangle::removeFromRight - takes the strip off the right. */
        inline Rect removeFromRight (Rect& r, int amount) noexcept
        {
            const int newWidth = maxOf (0, r.w - amount);
            const Rect out { r.x + newWidth, r.y, r.w - newWidth, r.h };
            r.w = newWidth;
            return out;
        }
    }
}
