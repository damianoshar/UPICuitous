#pragma once

#include <cstddef>
#include <set>
#include <vector>

#include "PieceLimits.h"

namespace upic
{
    //================ the ids of a loaded piece (L13) ==================
    //
    // The arcs, the sieve zones, the wavetables and the envelopes are keyed
    // by their id: the selection, the drag map, the arc list and the
    // material references all use it. A file could carry the same id twice
    // (two arcs with id 7) or an id that was never meant to be one (0, a
    // negative number, a huge number). Then the selection and the delete
    // hit the wrong arc, and the "next id" - taken from the file - could
    // collide with an arc that is already there.
    //
    // The ids are checked when a piece is loaded: the FIRST item of an id
    // keeps it (so the arcs that point at the material keep pointing at the
    // same thing), a duplicate or an impossible id gets a free one, and the
    // counter of the next id is derived from the items that are there.
    template <typename T>
    inline int uniquifyIds (std::vector<T>& items, int minId = 0)
    {
        std::set<int> used;
        for (auto& it : items)
            if (it.id >= minId && it.id <= limits::kMaxItemId)
                used.insert (it.id);

        std::set<int> seen;
        int nextFree = minId;
        int changed = 0;
        for (auto& it : items)
        {
            const bool valid = (it.id >= minId && it.id <= limits::kMaxItemId);
            if (valid && seen.count (it.id) == 0)
            {
                seen.insert (it.id);
                continue;      // the first one keeps the id
            }
            // a duplicate, or an id that was never meant to be one: it gets
            // a free id (the first one that nothing else uses)
            while (used.count (nextFree) > 0) ++nextFree;
            it.id = nextFree;
            used.insert (nextFree);
            seen.insert (nextFree);
            ++nextFree;
            ++changed;
        }
        return changed;
    }

    // Does the list carry an item with this id?
    template <typename T>
    inline bool hasId (const std::vector<T>& items, int id)
    {
        for (auto& it : items) if (it.id == id) return true;
        return false;
    }

    // The next free id in a bank. New bank objects use the same rule as
    // uniquifyIds(): the first id in the bank's range that is not already
    // occupied wins. This matters for an empty bank (its first id is still
    // a valid id) and for a bank whose ids have a hole (a hole is reusable
    // instead of making the counter jump past the limit). -1 means that the
    // bank has no free id left.
    template <typename T>
    inline int nextBankId (const std::vector<T>& items, int minId = 0)
    {
        minId = juce::jmax (0, minId);
        std::set<int> used;
        for (const auto& it : items)
            if (it.id >= minId && it.id <= limits::kMaxItemId)
                used.insert (it.id);

        for (int id = minId; id <= limits::kMaxItemId; ++id)
            if (used.count (id) == 0)
                return id;
        return -1;
    }
}
