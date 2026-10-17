# media/icons/ultraai/generate_icons.py - writes the UltraAI dashboard icon set.
# Usage: python3 generate_icons.py <out-dir>; then render each SVG to a 128 px
# PNG (e.g. cairosvg.svg2png(url=svg, write_to=png, output_width=128)).
import math, sys, os

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

# Placeholders in glyph templates:
#   {F}  white fill        {S}  white stroke
#   {Fh} half-white fill   {Sh} half-white stroke
#   {A}  accent fill (drawn over white; vanishes in the shadow pass)
#   {As} accent stroke
def paints(shadow, accent):
    if shadow:
        sh = 'fill="#0B1020" fill-opacity="0.22"'
        st = 'fill="none" stroke="#0B1020" stroke-opacity="0.22"'
        none = 'fill="none" stroke="none"'
        return dict(F=sh, S=st, Fh=sh, Sh=st, A=none, As=none, AF=none, P=none, Hl=none,
                    FS='fill="#0B1020" fill-opacity="0.22" stroke="#0B1020" stroke-opacity="0.22"')
    return dict(F='fill="#FFFFFF"',
                S='fill="none" stroke="#FFFFFF"',
                Fh='fill="#FFFFFF" fill-opacity="0.6"',
                Sh='fill="none" stroke="#FFFFFF" stroke-opacity="0.6"',
                A=f'fill="{accent}"',
                As=f'fill="none" stroke="{accent}"',
                AF=f'fill="{accent}" stroke="{accent}"',
                P='fill="#0D1A40"', Hl='fill="#FFFFFF"',
                FS='fill="#FFFFFF" stroke="#FFFFFF"')

def star(x, y, s):
    return (f'M{x} {y-s} Q{x} {y} {x+s} {y} Q{x} {y} {x} {y+s} '
            f'Q{x} {y} {x-s} {y} Q{x} {y} {x} {y-s} Z')

def arc(cx, cy, r, deg):
    a = math.radians(deg)
    x = cx + r * math.cos(a); y1 = cy - r * math.sin(a); y2 = cy + r * math.sin(a)
    return f'M{x:.1f} {y1:.1f} A{r} {r} 0 0 1 {x:.1f} {y2:.1f}'

def gear(cx, cy, teeth, ro, ri, hole):
    pts = []
    n = teeth * 4
    for i in range(n):
        a = 2 * math.pi * i / n - math.pi / 2
        r = ro if (i % 4) in (1, 2) else ri
        pts.append(f'{cx + r*math.cos(a):.1f} {cy + r*math.sin(a):.1f}')
    d = 'M' + ' L'.join(pts) + ' Z '
    d += (f'M{cx} {cy-hole} A{hole} {hole} 0 1 0 {cx} {cy+hole} '
          f'A{hole} {hole} 0 1 0 {cx} {cy-hole} Z')
    return d

ICONS = {
  'chat': (('#5A9BFF', '#4B32D4'), '#4B47E0', '''
    <rect x="52" y="26" width="52" height="38" rx="13" {Fh}/>
    <path d="M86 62 L98 76 L74 63 Z" {Fh}/>
    <rect x="22" y="42" width="62" height="44" rx="15" {F}/>
    <path d="M36 84 L30 102 L56 85 Z" {F}/>
    <circle cx="39" cy="64" r="5.5" {A}/>
    <circle cx="53" cy="64" r="5.5" {A}/>
    <circle cx="67" cy="64" r="5.5" {A}/>'''),
  'embeddings': (('#1FD1A8', '#0A6F94'), '#0C8A98', '''
    <path d="M34 38 L62 64 M88 32 L62 64 M32 92 L62 64 M94 88 L62 64 M34 38 L88 32 M32 92 L94 88 M34 38 L32 92"
          {Sh} stroke-width="4" stroke-linecap="round"/>
    <circle cx="34" cy="38" r="8" {F}/>
    <circle cx="88" cy="32" r="8" {F}/>
    <circle cx="32" cy="92" r="8" {F}/>
    <circle cx="94" cy="88" r="8" {F}/>
    <circle cx="62" cy="64" r="13" {F}/>
    <circle cx="62" cy="64" r="6" {A}/>'''),
  'speech-to-text': (('#FF9A3D', '#EE3F7E'), '#F2606A', '''
    <rect x="28" y="24" width="26" height="44" rx="13" {F}/>
    <path d="M19 56 A22 22 0 0 0 63 56" {S} stroke-width="6" stroke-linecap="round"/>
    <path d="M41 80 V94 M30 98 H52" {S} stroke-width="6" stroke-linecap="round"/>
    <path d="M74 40 H104 M74 56 H100 M74 72 H104 M74 88 H92" {S} stroke-width="7" stroke-linecap="round"/>'''),
  'text-to-speech': (('#B05CFF', '#E23D93'), '#C84BC8', f'''
    <path d="M22 52 H36 L58 34 V94 L36 76 H22 Z" {{FS}} stroke-width="4" stroke-linejoin="round"/>
    <path d="{arc(60, 64, 16, 48)}" {{S}} stroke-width="7" stroke-linecap="round"/>
    <path d="{arc(60, 64, 29, 48)}" {{S}} stroke-width="7" stroke-linecap="round"/>
    <path d="{arc(60, 64, 42, 48)}" {{Sh}} stroke-width="7" stroke-linecap="round"/>'''),
  'translation': (('#34C979', '#0E7F5C'), '#13905F', '''
    <circle cx="50" cy="54" r="30" {S} stroke-width="5"/>
    <ellipse cx="50" cy="54" rx="13" ry="30" {S} stroke-width="4"/>
    <path d="M20 54 H80 M25 40 H75 M25 68 H75" {S} stroke-width="4"/>
    <rect x="64" y="62" width="44" height="38" rx="11" {F}/>
    <path d="M74 98 L70 110 L88 99 Z" {F}/>
    <path d="M76 92 L86 70 L96 92 M79.5 85 H92.5" {As} stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>'''),
  'image-generation': (('#FF5F7A', '#FFB347'), '#FF7A5C', f'''
    <rect x="18" y="38" width="72" height="60" rx="11" {{F}}/>
    <path d="M26 90 L46 64 L60 80 L68 71 L82 90 Z" {{A}}/>
    <circle cx="70" cy="54" r="7" {{A}}/>
    <path d="{star(100, 30, 15)}" {{F}}/>
    <path d="{star(108, 60, 7)}" {{Fh}}/>'''),
  'vision': (('#2E9BF5', '#1C2F7A'), '#22479C', '''
    <path d="M22 36 V24 H34 M94 24 H106 V36 M22 92 V104 H34 M94 104 H106 V92"
          {S} stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="M26 64 Q64 28 102 64 Q64 100 26 64 Z" {F}/>
    <circle cx="64" cy="64" r="15" {A}/>
    <circle cx="64" cy="64" r="6.5" {P}/>
    <circle cx="69" cy="59" r="3.5" {Hl}/>'''),
  'video-generation': (('#FF4E6A', '#A3123F'), '#C61F4E', '''
    <g transform="rotate(-14 24 48)">
      <rect x="24" y="34" width="80" height="13" rx="3" {F}/>
      <path d="M36 34 H46 L40 47 H30 Z M56 34 H66 L60 47 H50 Z M76 34 H86 L80 47 H70 Z M96 34 H104 V36 L99 47 H90 Z" {A}/>
    </g>
    <rect x="24" y="52" width="80" height="48" rx="8" {F}/>
    <path d="M56 63 L80 76 L56 89 Z" {AF} stroke-width="3" stroke-linejoin="round"/>'''),
  'music-generation': (('#8E5CF0', '#3F6FE8'), '#6366EA', f'''
    <path d="M52 40 L102 26 V40 L52 54 Z" {{F}}/>
    <rect x="52" y="44" width="7" height="46" {{F}}/>
    <rect x="95" y="32" width="7" height="50" {{F}}/>
    <ellipse cx="44" cy="90" rx="14" ry="10" transform="rotate(-20 44 90)" {{F}}/>
    <ellipse cx="87" cy="82" rx="14" ry="10" transform="rotate(-20 87 82)" {{F}}/>
    <path d="{star(30, 38, 11)}" {{F}}/>
    <path d="{star(30, 60, 5)}" {{Fh}}/>'''),
  'code-assist': (('#34476B', '#141B2E'), '#5EF2B3', '''
    <path d="M44 42 L22 64 L44 86 M84 42 L106 64 L84 86" {S} stroke-width="9" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="M73 34 L55 94" {As} stroke-width="9" stroke-linecap="round"/>'''),
  'settings': (('#8C9BB2', '#47546A'), '#56637A', f'''
    <path d="{gear(64, 64, 8, 42, 32, 13)}" fill-rule="evenodd" {{F}}/>'''),
}

TEMPLATE = '''<?xml version="1.0" encoding="UTF-8"?>
<!-- UltraAI dashboard icon: {name}. 128 x 128 tile, generated as a set. -->
<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="{c0}"/>
      <stop offset="1" stop-color="{c1}"/>
    </linearGradient>
    <linearGradient id="gloss" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#FFFFFF" stop-opacity="0.32"/>
      <stop offset="1" stop-color="#FFFFFF" stop-opacity="0"/>
    </linearGradient>
  </defs>
  <rect x="4" y="4" width="120" height="120" rx="28" fill="url(#bg)"/>
  <rect x="4" y="4" width="120" height="64" rx="28" fill="url(#gloss)"/>
  <rect x="4.75" y="4.75" width="118.5" height="118.5" rx="27.25" fill="none" stroke="#FFFFFF" stroke-opacity="0.18" stroke-width="1.5"/>
  <g transform="translate(0 3)">{shadow}
  </g>{glyph}
</svg>
'''

for name, ((c0, c1), accent, glyph) in ICONS.items():
    g = glyph
    svg = TEMPLATE.format(name=name, c0=c0, c1=c1,
                          shadow=g.format(**paints(True, accent)),
                          glyph=g.format(**paints(False, accent)))
    with open(os.path.join(OUT, name + '.svg'), 'w') as f:
        f.write(svg)
    print(name)
