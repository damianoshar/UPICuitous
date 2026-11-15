#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Arc.h"
#include "UpicConstants.h"

namespace upic
{
    //---------------- Page sequencer ----------------
    // Next page in linear order, wrapping when asked; -1 = stop after this page.
    inline int seqNextPage(int current, int numPages, bool wrapAtEnd)
    {
        if (numPages <= 0) return -1;
        const int next = current + 1;
        if (next >= numPages) return wrapAtEnd ? 0 : -1;
        return next;
    }

    //---------------- Procedural arc generation (nUPIC-style) -------------------
    // Pure generators (Stochastic/Convergent Arborescence, Echelon Construction
    // after Bourbaki, Ordinal Spine with branching — nUPIC's procedural arc
    // family). They return bare arcs (id 0, voice fields untouched); the caller
    // stamps page ids and voice settings.
    struct GenParams { double t0 = 0.0, t1 = 10.0; double f0 = 90.0, f1 = 3000.0; };

    namespace gen
    {
        inline bool usable(const GenParams& p)
        { return p.t1 - p.t0 >= 0.2 && p.f1 > p.f0 && p.f0 >= kMinFreq && p.f1 <= kMaxFreq; }

        inline double clampLog(double l, const GenParams& p)
        { return juce::jlimit(std::log2(p.f0), std::log2(p.f1), l); }

        // Build an arc from parallel time/log2-freq arrays; drops duplicate
        // timestamps (keeps t strictly increasing) and clamps to the range.
        inline Arc makeArc(const std::vector<double>& ts, const std::vector<double>& ls, const GenParams& p)
        {
            Arc a;
            const size_t n = juce::jmin(ts.size(), ls.size());
            for (size_t i = 0; i < n; ++i)
            {
                const double t = juce::jlimit(p.t0, p.t1, ts[i]);
                if (! a.points.empty() && t <= a.points.back().t + 1e-9) continue;
                a.points.push_back({ t, std::pow(2.0, clampLog(ls[i], p)), 1.0f });
            }
            if (a.points.size() == 1)
                a.points.push_back({ juce::jmin(p.t1, a.points[0].t + 0.05), a.points[0].freq, 1.0f });
            return a;
        }

        // helper: log-freq line sampling
        inline void sampleLine(std::vector<double>& ts, std::vector<double>& ls,
                               double ta, double la, double tb, double lb,
                               double jitter, juce::Random& rng, int n)
        {
            for (int i = 0; i <= n; ++i)
            {
                const double u = (double) i / (double) n;
                double l = la + (lb - la) * u;
                if (i > 0 && i < n && jitter > 0.0)
                    l += (rng.nextFloat() * 2.0f - 1.0f) * jitter;
                ts.push_back(ta + (tb - ta) * u);
                ls.push_back(l);
            }
        }
    }

    // 1) Stochastic arborescence: a random-walk trunk that branches into child
    //    walks — Xenakis-style proliferating tree forms. The first spawn of
    //    each chain is forced, so the result is always a real tree (>=3 arcs).
    inline std::vector<Arc> genStochasticArborescence(const GenParams& p, juce::Random& rng)
    {
        namespace g = gen;
        std::vector<Arc> out;
        if (! g::usable(p)) return out;
        const double dur = p.t1 - p.t0;
        const double lo = std::log2(p.f0), hi = std::log2(p.f1);

        struct Walk { double t0, t1, l0, slope, jitter; int depth; bool mustSpawn; };
        std::vector<Walk> queue;
        queue.push_back({ p.t0, p.t1, 0.5 * (lo + hi),
                          (rng.nextFloat() - 0.5f) * 0.5, 0.35, 0, true });
        const int steps = juce::jlimit(4, 24, (int) std::round(dur / 0.4));

        while (! queue.empty())
        {
            const Walk w = queue.front();
            queue.erase(queue.begin());
            if (w.t1 - w.t0 < 0.1) continue;

            const int n = juce::jmax(2, (int) std::round(steps * (w.t1 - w.t0) / dur));
            const double dt = (w.t1 - w.t0) / n;
            std::vector<double> ts, ls;
            double l = w.l0;
            for (int i = 0; i <= n; ++i)
            {
                ts.push_back(w.t0 + dt * i);
                ls.push_back(g::clampLog(l, p));
                l += w.slope * dt + (rng.nextFloat() * 2.0f - 1.0f) * w.jitter;
            }
            out.push_back(g::makeArc(ts, ls, p));

            // branch: up to two children (forced first spawn, deeper = rarer)
            if (w.depth < 2 && out.size() + queue.size() < 8)
            {
                bool spawned = false;
                for (int c = 0; c < 2; ++c)
                {
                    if (queue.size() + out.size() >= 8) break;
                    const bool doSpawn = (w.mustSpawn && ! spawned)
                                         || rng.nextFloat() <= 0.55f;
                    if (! doSpawn) continue;
                    spawned = true;
                    const int bi = 1 + rng.nextInt(juce::jmax(1, n - 1));
                    const double bt = w.t0 + dt * bi;
                    const double bend = bt + (w.t1 - bt) * (0.4f + rng.nextFloat() * 0.45f);
                    if (bend - bt < 0.15) continue;
                    queue.push_back({ bt, bend, ls[(size_t) juce::jmin(bi, n)],
                                      -w.slope * 0.4f + (rng.nextFloat() - 0.5f) * 0.8f,
                                      w.jitter * 0.8f, w.depth + 1, w.depth + 1 < 2 });
                }
            }
        }
        return out;
    }

    // 2) Convergent arborescence: staggered branches that all converge onto one
    //    target pitch at the end (mass narrowing to a line).
    inline std::vector<Arc> genConvergentArborescence(const GenParams& p, juce::Random& rng)
    {
        namespace g = gen;
        std::vector<Arc> out;
        if (! g::usable(p)) return out;
        const double dur = p.t1 - p.t0;
        const double lo = std::log2(p.f0), hi = std::log2(p.f1);
        const double target = 0.5 * (lo + hi);

        const int nBranch = 4 + rng.nextInt(3); // 4..6
        for (int b = 0; b < nBranch; ++b)
        {
            const double u0 = (nBranch > 1) ? (double) b / (double) (nBranch - 1) : 0.5;
            const double start = juce::jlimit(lo, hi,
                    target + (u0 - 0.5) * 2.0 * 0.45 * (hi - lo) + (rng.nextFloat() - 0.5f) * 0.3);
            const double t0 = p.t0 + dur * 0.12 * u0;         // staggered starts
            const double jit = 0.18 * (1.0 - u0 * 0.3);
            std::vector<double> ts, ls;
            const int n = juce::jlimit(4, 24, (int) std::round(dur / 0.4));
            for (int i = 0; i <= n; ++i)
            {
                const double u = (double) i / n;
                const double t = juce::jmin(p.t1, t0 + (p.t1 - t0) * u);
                double l = start + (target - start) * u;        // exact convergence at u=1
                if (i < n) l += (rng.nextFloat() - 0.5f) * 2.0 * jit * (1.0 - u);
                ts.push_back(t);
                ls.push_back(i == n ? target : g::clampLog(l, p));
            }
            out.push_back(g::makeArc(ts, ls, p));
        }
        return out;
    }

    // 3) Echelon construction (Bourbaki): parallel staircase ranks — discrete
    //    steps marching in lockstep, staggered in pitch like military ranks.
    inline std::vector<Arc> genEchelon(const GenParams& p, juce::Random& rng)
    {
        namespace g = gen;
        std::vector<Arc> out;
        if (! g::usable(p)) return out;
        const double dur = p.t1 - p.t0;
        const double lo = std::log2(p.f0), hi = std::log2(p.f1);

        const int ranks  = 4 + rng.nextInt(3);        // 4..6 parallel arcs
        const int nSteps = 5 + rng.nextInt(6);        // 5..10 stair steps
        const double dir = rng.nextBool() ? 1.0 : -1.0;
        const double st  = (1.0 + rng.nextInt(2)) / 12.0;   // 1..2 semitones per step
        const double gap = (1.5 + rng.nextFloat()) / 12.0;  // 1.5..2.5 st between ranks
        const double base = 0.5 * (lo + hi) - dir * 0.5 * st * nSteps;

        const double hold = 0.65; // hold each step, then a quick rise to the next
        for (int r = 0; r < ranks; ++r)
        {
            std::vector<double> ts, ls;
            for (int k = 0; k <= nSteps; ++k)
            {
                const double tk  = p.t0 + dur * ((double) k / nSteps);
                const double tk1 = p.t0 + dur * ((double) (k + 1) / nSteps);
                const double l   = base + r * gap + dir * st * k;
                ts.push_back(tk);  ls.push_back(g::clampLog(l, p));
                if (k < nSteps)
                {
                    const double th = tk + (tk1 - tk) * hold;
                    ts.push_back(th); ls.push_back(g::clampLog(l, p));
                }
            }
            out.push_back(g::makeArc(ts, ls, p));
        }
        return out;
    }

    // 4) Ordinal spine with branching: one central spine with branches leaving
    //    it at ordinal positions (1/4, 1/2, 3/4 …), offset by fixed intervals.
    inline std::vector<Arc> genOrdinalSpine(const GenParams& p, juce::Random& rng)
    {
        namespace g = gen;
        std::vector<Arc> out;
        if (! g::usable(p)) return out;
        const double dur = p.t1 - p.t0;
        const double lo = std::log2(p.f0), hi = std::log2(p.f1);

        // spine: smooth drift through the range with light stochastic shading
        const int n = juce::jlimit(6, 32, (int) std::round(dur / 0.35));
        const double lA = juce::jlimit(lo, hi, 0.5 * (lo + hi) + (rng.nextFloat() - 0.5f) * 0.6 * (hi - lo));
        const double lB = juce::jlimit(lo, hi, 0.5 * (lo + hi) + (rng.nextFloat() - 0.5f) * 0.6 * (hi - lo));
        std::vector<double> spineT, spineL;
        for (int i = 0; i <= n; ++i)
        {
            const double u = (double) i / n;
            const double s = u * u * (3.0 - 2.0 * u); // smoothstep shape
            spineT.push_back(p.t0 + dur * u);
            spineL.push_back(g::clampLog(lA + (lB - lA) * s + (rng.nextFloat() - 0.5f) * 0.05, p));
        }
        out.push_back(g::makeArc(spineT, spineL, p));

        // branches at ordinal positions along the spine
        const int nBranch = 3 + rng.nextInt(2); // 3..4
        const double step = 0.33;               // fixed ordinal interval (octaves)
        for (int b = 0; b < nBranch; ++b)
        {
            const double ub = (b + 1) / (double) (nBranch + 1) + (rng.nextFloat() - 0.5f) * 0.06;
            const int si = juce::jlimit(1, n - 1, (int) std::round(ub * n));
            const double tb = spineT[(size_t) si], lb = spineL[(size_t) si];
            const double tEnd = juce::jmin(p.t1, tb + dur * (0.15 + rng.nextFloat() * 0.2));
            if (tEnd - tb < 0.1) continue;
            const double lEnd = lb + (b % 2 == 0 ? 1.0 : -1.0) * step * (1.0 + b * 0.5)
                                + (rng.nextFloat() - 0.5f) * 0.2;
            std::vector<double> ts, ls;
            g::sampleLine(ts, ls, tb, lb, tEnd, g::clampLog(lEnd, p), 0.0, rng,
                          juce::jmax(2, (int) std::round((tEnd - tb) / 0.3)));
            out.push_back(g::makeArc(ts, ls, p));
        }
        return out;
    }
}
