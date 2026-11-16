#!/usr/bin/env python3
"""Regenerate embedded JUCE SVG icons, editable SVG sources and an icon contact sheet.

The Lucide-derived paths below are from lucide-icons/lucide, licensed under ISC
(with supplementary Feather MIT terms for certain symbols). The miniature
waveform/envelope/tool symbols are original to UPICuitous's icon integration.
This script has no network dependencies.
"""
from pathlib import Path
from xml.etree import ElementTree as ET
from html import escape
import io

ROOT = Path(__file__).resolve().parents[1]
ICONS = ROOT / 'Resources' / 'Icons'
ICONS.mkdir(parents=True, exist_ok=True)

# Lucide icon contents, retrieved from the upstream lucide-icons/lucide icons/ tree.
# Internal SVG geometry remains vector at any GUI scale.
lucide = {
 'play':'<path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"/>',
 'stop':'<rect width="18" height="18" x="3" y="3" rx="2"/>',
 'repeat':'<path d="m17 2 4 4-4 4"/><path d="M3 11v-1a4 4 0 0 1 4-4h14"/><path d="m7 22-4-4 4-4"/><path d="M21 13v1a4 4 0 0 1-4 4H3"/>',
 'sequence':'<path d="M21 5H3"/><path d="M10 12H3"/><path d="M10 19H3"/><path d="M15 12.003a1 1 0 0 1 1.517-.859l4.997 2.997a1 1 0 0 1 0 1.718l-4.997 2.997a1 1 0 0 1-1.517-.86z"/>',
 'hand':'<path d="M18 11V6a2 2 0 0 0-2-2a2 2 0 0 0-2 2"/><path d="M14 10V4a2 2 0 0 0-2-2a2 2 0 0 0-2 2v2"/><path d="M10 10.5V6a2 2 0 0 0-2-2a2 2 0 0 0-2 2v8"/><path d="M18 8a2 2 0 1 1 4 0v6a8 8 0 0 1-8 8h-2c-2.8 0-4.5-.86-5.99-2.34l-3.6-3.6a2 2 0 0 1 2.83-2.82L7 15"/>',
 'scrub':'<path d="M3 7V5a2 2 0 0 1 2-2h2"/><path d="M17 3h2a2 2 0 0 1 2 2v2"/><path d="M21 17v2a2 2 0 0 1-2 2h-2"/><path d="M7 21H5a2 2 0 0 1-2-2v-2"/><path d="M7 12h10"/>',
 'magnet':'<path d="m12 15 4 4"/><path d="M2.352 10.648a1.205 1.205 0 0 0 0 1.704l2.296 2.296a1.205 1.205 0 0 0 1.704 0l6.029-6.029a1 1 0 1 1 3 3l-6.029 6.029a1.205 1.205 0 0 0 0 1.704l2.296 2.296a1.205 1.205 0 0 0 1.704 0l6.365-6.367A1 1 0 0 0 8.716 4.282z"/><path d="m5 8 4 4"/>',
 'file':'<path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z"/><path d="M14 2v5a1 1 0 0 0 1 1h5"/>',
 'settings':'<path d="M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915"/><circle cx="12" cy="12" r="3"/>',
 'trash':'<path d="M10 11v6"/><path d="M14 11v6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M3 6h18"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>',
 'plus':'<path d="M5 12h14"/><path d="M12 5v14"/>',
 'copy':'<rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/>',
 'minus':'<path d="M5 12h14"/>',
 'save':'<path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7"/><path d="M7 3v4a1 1 0 0 0 1 1h7"/>',
 'folder-open':'<path d="m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2"/>',
 'layers':'<path d="M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83z"/><path d="M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12"/><path d="M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17"/>',
 'mic':'<path d="M12 19v3"/><path d="M19 10v2a7 7 0 0 1-14 0v-2"/><rect x="9" y="2" width="6" height="13" rx="3"/>',
 'audio-lines':'<path d="M2 10v3"/><path d="M6 6v11"/><path d="M10 3v18"/><path d="M14 8v7"/><path d="M18 5v13"/><path d="M22 10v3"/>',
 'radio':'<path d="M16.247 7.761a6 6 0 0 1 0 8.478"/><path d="M19.075 4.933a10 10 0 0 1 0 14.134"/><path d="M4.925 19.067a10 10 0 0 1 0-14.134"/><path d="M7.753 16.239a6 6 0 0 1 0-8.478"/><circle cx="12" cy="12" r="2"/>',
 'panel-bottom':'<rect width="18" height="18" x="3" y="3" rx="2"/><path d="M3 15h18"/>',
 'eye':'<path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"/><circle cx="12" cy="12" r="3"/>',
 'list':'<path d="M3 5h.01"/><path d="M3 12h.01"/><path d="M3 19h.01"/><path d="M8 5h13"/><path d="M8 12h13"/><path d="M8 19h13"/>',
 'check':'<path d="M20 6 9 17l-5-5"/>',
 'x':'<path d="M18 6 6 18"/><path d="m6 6 12 12"/>',
 'upload':'<path d="M12 3v12"/><path d="m17 8-5-5-5 5"/><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>',
 'download':'<path d="M12 15V3"/><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m7 10 5 5 5-5"/>',
 'volume-x':'<path d="M11 4.702a.7.7 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.7.7 0 0 0 11 19.298z"/><path d="m16.5 14.5 5-5"/><path d="m16.5 9.5 5 5"/>',
 'reverse':'<path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/>',
 'pencil':'<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z"/><path d="m15 5 4 4"/>',
 'eraser':'<path d="M21 21H8a2 2 0 0 1-1.42-.587l-3.994-3.999a2 2 0 0 1 0-2.828l10-10a2 2 0 0 1 2.829 0l5.999 6a2 2 0 0 1 0 2.828L12.834 21"/><path d="m5.082 11.09 8.828 8.828"/>',
 'move':'<path d="M8 3 4 7l4 4"/><path d="M4 7h16"/><path d="m16 21 4-4-4-4"/><path d="M20 17H4"/>',
 'file-music':'<path d="M11.65 22H18a2 2 0 0 0 2-2V8a2.4 2.4 0 0 0-.706-1.706l-3.588-3.588A2.4 2.4 0 0 0 14 2H6a2 2 0 0 0-2 2v10.35"/><path d="M14 2v5a1 1 0 0 0 1 1h5"/><path d="M8 20v-7l3 1.474"/><circle cx="6" cy="20" r="2"/>',
 'headphones':'<path d="M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a9 9 0 0 1 18 0v7a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3"/>',
 'undo':'<path d="M9 14 4 9l5-5"/><path d="M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11"/>',
 'redo':'<path d="m15 14 5-5-5-5"/><path d="M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13"/>',
}

# Original 24px mini-diagrams: unlike generic icons, each accurately depicts
# the result of its UPIC waveform/envelope preset.
custom = {
 'wave-sine':'<path d="M2 12 C4.5 2 7 2 9 12 S13.5 22 16 12 S20 2 22 12"/>',
 'wave-saw':'<path d="M2 19 8 5v14l7-14v14l7-14"/>',
 'wave-square':'<path d="M2 18V6h6v12h7V6h7v12"/>',
 'wave-triangle':'<path d="M2 18 7 5 14 19 20 5 22 10"/>',
 'wave-noise':'<polyline points="2,13 4,4 6,18 8,8 10,14 12,3 14,20 16,9 18,16 20,5 22,13"/>',
 'wave-smooth':'<polyline points="2,15 5,7 8,18 11,5 14,18 17,7 21,12" opacity="0.45"/><path d="M2 15 C7 5 10 18 14 12 S19 7 22 12"/>',
 'wave-normalize':'<path d="M3 12h18" opacity="0.35"/><path d="M4 19 4 5m16 14V5"/><path d="m6 17 3-10 3 10 3-12 3 12"/>',
 'env-flat':'<path d="M2 17h20"/><path d="M2 21V4" opacity="0.3"/>',
 'env-percussive':'<polyline points="2,20 4,4 7,9 10,13 14,17 20,20 22,20"/>',
 'env-swell':'<path d="M2 20C7 20 8 16 11 9S17 4 19 4l3 16"/>',
 'env-gate':'<polyline points="2,20 4,4 19,4 20,20 22,20"/>',
 'env-strings':'<polyline points="2,20 6,9 9,7 14,7 19,10 22,20"/>',
 'zone':'<rect x="3" y="5" width="18" height="14" rx="1" stroke-dasharray="3 3"/><path d="M3 12h18" opacity="0.4"/>',
 'line-tool':'<path d="M3 20 21 4"/><circle cx="3" cy="20" r="1.6"/><circle cx="21" cy="4" r="1.6"/>',
 'fade':'<polyline points="2,5 8,5 12,9 16,16 22,20"/>',
 'sieve':'<path d="M3 7h18M3 12h18M3 17h18" opacity="0.65"/><path d="M7 3v18M13 3v18M19 3v18" opacity="0.65"/><circle cx="13" cy="12" r="2.5" fill="#e9edf7" stroke="none"/>',
 'arc':'<path d="M2 19 C4 5 12 20 22 5"/><circle cx="2" cy="19" r="1"/><circle cx="22" cy="5" r="1"/>',
}

icons = {**lucide, **custom}
for name, geometry in sorted(icons.items()):
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" '
           'viewBox="0 0 24 24" fill="none" stroke="#e9edf7" '
           'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
           + geometry + '</svg>\n')
    ET.fromstring(svg)
    (ICONS / (name + '.svg')).write_text(svg, encoding='utf-8')

# Fully embedded SVGs keep CMake / Projucer / VST3 / Android build configurations
# unchanged, with no run-time filesystem or resource paths to maintain.
header = ['// AUTO-GENERATED by Tools/create_button_icons.py. Do not edit by hand.',
          '#pragma once', '#include <JuceHeader.h>', '#include <memory>', '#include <vector>',
          'namespace upic { namespace buttonIcons {',
          'struct Definition { const char* name; const char* svg; };',
          'inline const Definition definitions[] = {']
for name in sorted(icons):
    svg = (ICONS / (name + '.svg')).read_text(encoding='utf-8').strip()
    header.append('    {"' + name + '", R"UPICICON(' + svg + ')UPICICON"},')
header += [
 '};',
 'inline const juce::Drawable* find (const juce::String& name)',
 '{',
 '    struct Cached { juce::String name; std::unique_ptr<juce::Drawable> drawable; };',
 '    static const std::vector<Cached> cache = []',
 '    {',
 '        std::vector<Cached> icons;',
 '        icons.reserve (sizeof (definitions) / sizeof (definitions[0]));',
 '        for (const auto& d : definitions)',
 '            if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (d.svg)))',
 '                icons.push_back ({ d.name, juce::Drawable::createFromSVG (*xml) });',
 '        return icons;',
 '    }();',
 '    for (const auto& entry : cache)',
 '        if (entry.name == name) return entry.drawable.get();',
 '    return nullptr;',
 '}',
 '}} // namespace upic::buttonIcons', ''
]
(ROOT / 'Source' / 'UpicButtonIconData.h').write_text('\n'.join(header), encoding='utf-8')

# Contact sheet for reviewing the visual vocabulary without running the app.
try:
    from PIL import Image, ImageDraw, ImageFont
    import cairosvg
    names = sorted(icons)
    w = 900; cols = 7; cw = w // cols; ch = 88; rows = (len(names)+cols-1)//cols
    canvas = Image.new('RGB', (w, rows*ch + 56), '#171b24')
    d = ImageDraw.Draw(canvas)
    d.text((20, 14), 'UPICuitous  /  button and tool icons', fill='#ecedf4')
    for i, name in enumerate(names):
        x = (i % cols)*cw; y = 56+(i//cols)*ch
        d.rounded_rectangle((x+6,y+4,x+cw-7,y+ch-6),radius=10,fill='#252d38')
        raster = cairosvg.svg2png(bytestring=(ICONS/(name+'.svg')).read_bytes(),output_width=36, output_height=36)
        im = Image.open(io.BytesIO(raster)).convert('RGBA')
        canvas.paste(im, (x+(cw-36)//2, y+10), im)
        bb = d.textbbox((0,0),name)
        d.text((x+(cw-(bb[2]-bb[0]))/2, y+56), name, fill='#b5bfd2')
    canvas.save(ROOT/'docs'/'screenshots'/'upicuitous-button-icons.png')
except ImportError:
    pass
print(f'Created {len(icons)} SVG images, JUCE embed header and preview.')
