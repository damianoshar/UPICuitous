#pragma once

//==============================================================================
// UPIC data model - mirrors Xenakis' UPIC concepts:
//  Score Page (time=X, pitch=Y log) -> Arcs (pitch curves)
//  Wavetable (single-cycle waveform) per arc
//  Envelope (amplitude 0..1 over arc lifetime) per arc
//
// v1.33.4 (Tier 1 refactor): the model is split by concept into the headers
// below; this file is the umbrella the rest of the app includes. No type and
// no behaviour changed - the split is a move, not a rewrite.
//==============================================================================

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "UpicConstants.h"
#include "UpicMath.h"
#include "UpicTheme.h"
#include "Tuning.h"
#include "Waveform.h"
#include "Envelope.h"
#include "Arc.h"
#include "FreqSpace.h"
#include "Sieve.h"
#include "Generators.h"
#include "Sample.h"
#include "PickedFile.h"
#include "Ids.h"
#include "Page.h"
#include "Piece.h"