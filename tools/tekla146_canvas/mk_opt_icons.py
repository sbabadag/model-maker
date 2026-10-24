"""Download Tekla 146 option pictures (GUIDs from the saved help page) into
resources/tekla146/opt/, emit the qrc block + C++ table (param -> choice -> resource)."""
import json, re, subprocess, os, sys, hashlib

ROOT = '/home/ubuntu/model-maker-overnight'
OPTS = json.load(open('/home/ubuntu/.hermes/cache/scratch/occbuild/opts.json'))
md = open('/home/ubuntu/.hermes/cache/web/support.tekla.com-e933830612.md').read()
full = {}
for m in re.finditer(r'GUID-([0-9A-F]{8})-([0-9A-F-]{27})-publishing\.png', md):
    full[m.group(1)] = m.group(1) + '-' + m.group(2)

# param -> (section key in opts.json, [option index per engine choice, None = no picture])
MAP = {
    'beamWebCut': ('Picture tab / Beam web cut', [0, 1, 2]),
    'beamFlangeCut': ('Picture tab / Beam flange cut', [0, 1, 2]),
    'beamBottomFlangeCut': ('Picture tab / Beam bottom flange cut', [0, 1, 2]),
    'shearTabPosition': ('Plates tab / Shear tab position', [4, 2, 3]),
    'tabPositionSloped': ('Plates tab / Shear tab position (sloped)', [0, 1, 2]),
    'tabEndCut': ('Plates tab / Shear tab end cut', [0, 1, 2]),
    'tabChamferType': ('Plates tab / Chamfer type', [0, 1, 2, 3, 4]),
    'tabOrientation': ('Plates tab / Shear tab orientation', [0, 1, 2, 3, 4]),
    'plateWasherType': ('Plates tab / Plate washer', [0, 1, 2, 3, 4]),
    'stiffenerCreation': ('Stiffeners tab / Stiffener creation', [0, 1, 2, 3, 4]),
    'stiffenerOrientation': ('Stiffeners tab / Stiffener orientation', [0, 1, 2]),
    'stiffenerChamferType': ('Stiffeners tab / Chamfer type', [0, 1, 2, 3, 4]),
    'haunchCreation': ('Haunch tab / Haunch plate creation', [0, 1, 2, 3]),
    'notchShape': ('Notch tab / Notch shape', [0, 1, 2, 3, 4, 5]),
    'notchSizeRef': ('Notch tab / Notch size', [0, 1, 2, 3]),
    'notchFlangeCutShape': ('Notch tab / Flange cut shape', [0, 1, 2]),
    'notchRounding': ('Notch tab / Notch dimension rounding', [0, 1, 2]),
    'notchPosition': ('Notch tab / Notch position', [0, 1, 2]),
    'notchChamfer': ('Notch tab / Notch chamfer', [0, 1, 2, 3]),
    'flangeNotchSideTop': ('Notch tab / Side of flange notch', [0, 1, 2, 3, 4]),
    'flangeNotchSideBottom': ('Notch tab / Side of flange notch', [0, 1, 2, 3, 4]),
    'flangeNotchShapeTop': ('Notch tab / Flange notch shape', [0, 1, 2, 3, 4, 5, 6]),
    'flangeNotchShapeBottom': ('Notch tab / Flange notch shape', [0, 1, 2, 3, 4, 5, 6]),
    'flangeNotchDepthRefTop': ('Notch tab / Flange notch depth', [0, 1, 2]),
    'flangeNotchDepthRefBottom': ('Notch tab / Flange notch depth', [0, 1, 2]),
    'boltStagger': ('Bolts tab / Staggering of bolts', [0, 1, 2, 3]),
    'boltStaggerExtra': ('Bolts tab / Staggering of bolts', [0, 4, 5]),
    'boltGroupOrientation': ('Bolts tab / Bolt group orientation', [0, 1, 2, 3, 4]),
    'boltingDirection': ('Bolts tab / Bolting direction', [0, 1, 2]),
    'weldAccessHoleType': ('Beam cut tab / Weld access holes', [0, 1, 2, 3, 4, 5, 6]),
    'beamEndPreparation': ('Beam cut tab / Beam end preparation', [0, 1, 2, 3, 4, 5]),
    'backingBars': ('Beam cut tab / Weld backing bars', [0, 1, 2, 3]),
    'backingBarLengthType': ('Beam cut tab / Weld backing bar length', [0, 1, 2]),
    'seatPosition': ('Angle box tab / Seat angle position', [0, 1, 2, 3, 4]),
    'seatStiffType': ('Angle box tab / Stiffener type', [0, 1, 2, 3]),
    'seatRotation': ('Angle box tab / Seat angle rotation', [0, 1, 2]),
    'seatOrientation': ('Angle box tab / Seat angle orientation', [0, 1, 2, 3]),
    'seatSideStiff': ('Angle box tab / Side stiffener position', [0, 1, 2, 3, 4]),
    'seatMiddleStiff': ('Angle box tab / Middle stiffener position', [0, 1, 2, 3]),
    'seatChamferType': ('Angle box tab / Chamfer type', [0, 1, 2, 3, 4]),
}

outdir = f'{ROOT}/resources/tekla146/opt'
os.makedirs(outdir, exist_ok=True)
base = 'https://support.tekla.com/dist/suf/product-guides/tekla-structures/en/2025/GUID-%s-publishing.png'
need = {}
for param, (sec, idxs) in MAP.items():
    lst = OPTS[sec]
    for i in idxs:
        g8 = lst[i][0][:8]
        need[g8] = full.get(g8) or lst[i][0]
        assert len(need[g8]) > 8, (param, need[g8])

bad = []
for g8, g in sorted(need.items()):
    fn = f'{outdir}/{g8}.png'
    if os.path.exists(fn) and os.path.getsize(fn) > 100:
        continue
    r = subprocess.run(['curl', '-s', '-o', fn, '-w', '%{http_code}', base % g], capture_output=True, text=True)
    if r.stdout != '200' or os.path.getsize(fn) < 100:
        bad.append((g8, r.stdout))
print('downloaded', len(need) - len(bad), 'bad', bad)

# qrc
qrc = ''.join(f'        <file alias="opt/{g8}.png">tekla146/opt/{g8}.png</file>\n' for g8 in sorted(need))
open('/home/ubuntu/.hermes/cache/scratch/occbuild/opt.qrc.part', 'w').write(qrc)

# C++ table
lines = ['// UretIlmIs dosya (tools/tekla146_canvas/mk_opt_icons.py) — elle DUZENLEME.',
         '// Tekla 146 yardim sayfasindaki secenek resimleri: parametre -> secenek sirasina gore',
         '// kaynak yolu (":/tekla146/opt/XXXXXXXX.png"). Bos = o secenegin resmi yok.',
         '#include "model_maker/component_option_icons.hpp"', '', 'namespace mm {', '',
         'const std::vector<std::wstring>* optionIconsFor(const std::wstring& param) {',
         '    static const std::map<std::wstring, std::vector<std::wstring>> kTable = {']
for param, (sec, idxs) in MAP.items():
    items = ', '.join('L":/tekla146/opt/%s.png"' % OPTS[sec][i][0][:8] for i in idxs)
    lines.append(f'        {{L"{param}", {{{items}}}}},')
lines += ['    };', '    const auto it = kTable.find(param);', '    return it == kTable.end() ? nullptr : &it->second;', '}', '', '}  // namespace mm', '']
open(f'{ROOT}/src/component_option_icons.cpp', 'w').write('\n'.join(lines))
print('params', len(MAP), 'unique images', len(need))
