# UPICuitous button and drawing-tool SVGs

This folder contains 52 editable, 24×24 vector SVG images. The general UI
symbols are selected from the open-source [Lucide](https://lucide.dev/) icon
library (ISC license; some symbols originated in Feather and additionally
carry MIT conditions). The mini-diagram icons prefixed `wave-` and `env-`,
and `arc.svg`, `fade.svg`, `line-tool.svg`, `sieve.svg`, `zone.svg` were created
for this project. See `LICENSE-LUCIDE.txt` for full upstream terms.

Use `python Tools/create_button_icons.py` at repository root to regenerate
`Source/UpicButtonIconData.h`, the SVG files and the contact-sheet preview.

The C++ UI loads **embedded** vector images from the generated header, not
files at runtime. This works from stand-alone CMake, Projucer, VST3 and Android
builds without changing their binary data rules or needing asset install paths.

`Source/UpicButtonIcons.h` contains the button→image associations. The
`MainComponent::UpicLookAndFeel` paints them inside existing buttons:
callbacks, tooltips, keyboard interaction, accessibility names and data model
remain unchanged. Narrow wavetable/envelope controls display the diagram and
expose their original button text as a tooltip. Tool combobox icons represent
Draw, Line, Sine, Select/Move, Erase and Sieve zone.
