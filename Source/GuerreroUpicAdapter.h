#pragma once

#if GUERRERO_INTEGRATION

#include <JuceHeader.h>

#include "GuerreroImport.h"
#include "guerrero/upic/GuerreroUpicPlan.h"

namespace upic
{
    namespace guerrero_bridge
    {
        // Convert an engine-side conversion plan into this host's neutral import
        // type. The result is plain data: no host ids, no material selection, no
        // page state. MainComponent::insertGuerreroArcs() owns all of that.
        //
        // The plan is already validated by planArcFromCurve() (finite values,
        // host frequency range, point cap, sorted score times), so this is a
        // representation change only.
        upic::GuerreroImportArc toImportArc (const guerrero::upicadapter::PlannedArc& plan);

        // Rebuild a planned arc from a saved recipe.
        //
        // Returns false when the recipe is not usable (a legacy record has no
        // recipe at all). This is the operation that makes the stored recipe
        // meaningful: it must reproduce the original points on the same build.
        //
        // Determinism caveat: Random.h still uses the C++ standard library
        // distributions, so this is exact on one compiler/version and only
        // nominally reproducible across platforms.
        bool regenerateFromRecipe (const upic::GuerreroRecipe& recipe,
                                   guerrero::upicadapter::PlannedArc& out,
                                   bool* lossyOut = nullptr,
                                   uint32_t* omittedOut = nullptr);
    }
}

#endif // GUERRERO_INTEGRATION
