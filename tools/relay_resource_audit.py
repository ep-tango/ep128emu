#!/usr/bin/env python3
"""Static/platform-package gate for Windows tape relay sample lookup.

Verify original sample identity, both accepted Windows package layouts and
Win32 source branches; audio heard on hardware remains a separate GUI gate.
"""
from pathlib import Path
import hashlib
import wave
import zipfile
import re
from io import BytesIO
root=Path(__file__).resolve().parent.parent
sample=(root/'resource/EP_rele.wav').read_bytes()
with wave.open(BytesIO(sample)) as wav:
    assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate())==(2,2,48000)
    assert wav.getnframes()==9472
print('RELAY_SOURCE_48KHZ_STEREO_PCM16_PASS',hashlib.sha256(sample).hexdigest())
code=(root/'gui/gui.cpp').read_text()
assert 'ep128emuPlayWindowsRelayClick()' in code
assert 'GetModuleFileNameW' in code and 'PlaySoundW(path.c_str()' in code
assert 'path += L"EP_rele.wav";' in code
assert 'path = exeDir + L"resource\\\\EP_rele.wav";' in code
assert 'GetFileAttributesW(path.c_str())' in code
assert 'SND_FILENAME | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP' in code
for name in ('EP128EMU-W07AA-WIN7-TEST.zip','ep128emu-EF08AB-win64-TEST.zip'):
    file=Path.home()/'Downloads'/name
    assert file.is_file()
    with zipfile.ZipFile(file) as z:
        matches=[n for n in z.namelist() if n.endswith('EP_rele.wav')]
        assert len(matches)==1 and z.read(matches[0])==sample,(name,matches)
        parent=matches[0].split('/')[-2]
        assert parent in ('resource','ep128emu-EF08AB-win64-TEST')
        print('RELAY_PREVIOUS_WINDOWS_RESOURCE_SHA_PASS',name,parent)
print('RELAY_WIN7_RESOURCE_AND_WIN64_ROOT_SOURCE_LOOKUP_PASS')
