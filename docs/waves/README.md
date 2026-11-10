# Wave Design Notes

This directory contains per-wave design notes for UPICuitous. Each wave corresponds to a development cycle (typically 1-2 weeks) with a focused set of features.

## Wave Index

| Wave | Version | Focus | Status |
|------|---------|-------|--------|
| 1-14 | 1.x.x | Core UPIC clone: score, oscillators, samples, live input | Shipped |
| 15 | 1.18 | Lock-free model snapshot (UPIC-32) | Shipped |
| 16 | 1.19 | Sample reverse read (UPIC-25) | Shipped |
| 17 | 1.20 | Live reverse delay (UPIC-27) | Shipped |
| 18 | 1.21 | Arc list panel (UPIC-19) | Shipped |
| 19 | 1.22 | Scrub audition (UPIC-21) | Shipped |
| 20 | 1.23 | MIDI input foundation (UPIC-24) | Shipped |
| 21 | 1.24 | Multi-selection / toolbar edits (UPIC-26) | Shipped |
| 22 | 1.25 | Signed playhead, MIDI scrub, live reverse, PM voice | Shipped |
| 23 | 1.26 | Wavetable fork, Stop rewind, Clear warning | Shipped |
| 24 | 1.27 | Granular no-stretch, OSC scrub | Shipped |
| 25 | 0.28.0 | Release packaging, security review | Shipped |

## Wave Detail Files

Each wave has a corresponding markdown file with:
- **Complexity score** (S/M/L)
- **Risk list**
- **Design decisions**
- **Implementation notes**
- **Test inventory**
- **Open questions**

Files are named: `NN-wave-name.md` (e.g., `15-lockfree-snapshot.md`)

## Future Waves (Planned)

| Wave | Kanso Card | Focus | Size |
|------|------------|-------|------|
| 26 | UPIC-28 | MIDI scrub transport | M |
| 27 | UPIC-29 | Host transport follow | M |
| 28 | UPIC-26 | 2-op PM proof | M |
| 29 | UPIC-30 | Full FM engine | L |
| 30 | UPIC-32 | Musical time (beats) | L |

See `../FUTURE_IDEAS.md` for the full roadmap.