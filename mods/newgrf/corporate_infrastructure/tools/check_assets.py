#!/usr/bin/env python3
from pathlib import Path
from PIL import Image
import json
root=Path(__file__).resolve().parent.parent
m=json.loads((root/'manifest.json').read_text())
assert len(m['assets']) == 6
assert len(m['originals']) == 7
nml=(root/'CorporateInfrastructure.nml').read_text()
for a in m['assets']:
    assert (root/'originals'/a['source']).is_file(), a['source']
    for s in a['sprites']:
        img=Image.open(root/s['filename'])
        assert img.mode=='RGBA',s['filename']
        assert img.size==(s['width'],s['height']),s['filename']
        assert img.getchannel('A').getbbox() is not None
        assert s['filename'] in nml
print('OK: 7 original PNGs, 6 object spritesets at 1x/2x, all references present.')
print('Not a substitute for compiling with NML or OpenTTD in-game testing.')
