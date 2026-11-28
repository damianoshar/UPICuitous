#pragma once

//==============================================================================
// R5.4 (the review's R046, section 24): the editing tools.
//
// The score canvas's mouse handlers used to hold a condition for every
// drawing mode: the freehand stroke, the straight line, the sine stroke, the
// selection, the marquee, the move, the erase, the zone. The review asks for
// an explicit tool controller instead:
//
//   "Rather than allowing mouse handlers to accumulate conditions for every
//    drawing mode, create an explicit tool controller." (section 24)
//
//   class EditingTool
//   {
//   public:
//       virtual ~EditingTool() = default;
//       virtual void begin (const PointerInput&) = 0;
//       virtual void update (const PointerInput&) = 0;
//       virtual void end (const PointerInput&) = 0;
//       virtual void cancel() = 0;
//   };
//
// This is that interface and the pointer's input it is given. The pointer
// input carries what the review asks for - "coordinates, pressure,
// modifiers, pointer identity and relevant gesture information" - and the
// tools never see a JUCE mouse event.
//
// The concrete tools are in Source/ScoreEditingTools.h; the canvas's mouse
// handlers ask the active tool and the tool acts on the canvas.

#include <JuceHeader.h>

namespace upic
{
    namespace score_tools
    {
        //================ the pointer's input ==============================
        // What the pointer says when it moves: where it is, how hard it
        // presses, which keys are down, which pointer it is and where the
        // gesture began.
        struct PointerInput
        {
            // Where the pointer is, and where the gesture began.
            juce::Point<float> position;
            juce::Point<float> downPosition;

            // How hard the pointer presses (1 = no pressure information).
            float pressure = 1.0f;

            // The keys that are down.
            juce::ModifierKeys mods;

            // Which pointer this is (the mouse, a finger), and whether it is
            // a touch.
            int pointerId = 0;
            bool isTouch = false;
        };

        //================ the tool ==========================================
        // A tool is the controller of one drawing mode: what a press means,
        // what a drag means and what a release means in that mode.
        class EditingTool
        {
        public:
            virtual ~EditingTool() = default;

            // The gesture starts, moves, ends - or is cancelled (a pinch, a
            // layer change: the tool gives the gesture up).
            virtual void begin  (const PointerInput&) = 0;
            virtual void update (const PointerInput&) = 0;
            virtual void end    (const PointerInput&) = 0;
            virtual void cancel () = 0;
        };
    }
}
