#!/usr/bin/env python3
"""Builds Cue-<version>-Setup.exe: the setup stub + the app folder, LZMA-compressed.

    python3 tools/make_setup.py build/Cue build/setup/stub.exe dist/Cue-1.1.0-Setup.exe

The app folder must already hold libmpv-2.dll. The large source archives in fontes/ are not
packed into the installer: they are published next to it on the GitHub release (GPL 6(d)),
and fontes/LEIA-ME.txt says where. Prints the installer's SHA-256 (GitHub shows the same
digest for the uploaded asset, and Cue checks it before installing an update).
"""
import hashlib
import lzma
import os
import re
import struct
import sys
import zlib

def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    app, stub, out = sys.argv[1:]
    version = re.search(r'#define CUE_VERSION "([^"]+)"',
                        open(os.path.join(os.path.dirname(__file__), '..', 'src', 'core', 'version.h'), encoding='utf-8').read()).group(1)
    repo = re.search(r'#define CUE_UPDATE_REPO "([^"]+)"',
                     open(os.path.join(os.path.dirname(__file__), '..', 'src', 'core', 'version.h'), encoding='utf-8').read()).group(1)
    if not os.path.isfile(os.path.join(app, 'libmpv-2.dll')):
        sys.exit(f'falta {app}/libmpv-2.dll (copie a DLL do pacote Windows 1.0 para essa pasta)')
    files = []
    for root, dirs, names in os.walk(app):
        dirs.sort()
        for n in sorted(names):
            full = os.path.join(root, n)
            rel = os.path.relpath(full, app).replace(os.sep, '/')
            low = rel.lower()
            if low.endswith('.log') or low in ('install.txt', 'uninstall.exe'):
                continue
            if low.startswith('fontes/') and low.endswith('.zip'):
                continue
            files.append((rel, open(full, 'rb').read()))
    note = (
        'Códigos-fonte do Cue\r\n\r\n'
        f'O código do Cue {version} e as fontes do motor de reprodução (libmpv e bibliotecas) estão publicados\r\n'
        'junto deste instalador, na mesma página de versão do GitHub:\r\n\r\n'
        f'  https://github.com/{repo}/releases/tag/v{version}\r\n\r\n'
        '  Source code (zip)                 código do Cue (GPL-3.0-or-later), gerado pelo GitHub\r\n'
        f'  Cue-{version}-Engine-Source.zip       código-fonte do motor de reprodução (libmpv-2.dll)\r\n\r\n'
        'Licenças e avisos: LICENSES.md e a pasta third_party desta instalação.\r\n'
    ).encode('utf-8')
    files = [f for f in files if f[0].lower() != 'fontes/leia-me.txt'] + [('fontes/LEIA-ME.txt', note)]

    archive = bytearray()
    for rel, data in files:
        p = rel.encode('utf-8')
        archive += struct.pack('<H', len(p)) + p + struct.pack('<Q', len(data)) + data
    crc = zlib.crc32(archive) & 0xffffffff
    packed = lzma.compress(bytes(archive), format=lzma.FORMAT_ALONE,
                           filters=[{'id': lzma.FILTER_LZMA1, 'preset': 9 | lzma.PRESET_EXTREME, 'dict_size': 64 << 20}])
    stub_bytes = open(stub, 'rb').read()
    footer = b'CUESETUP' + struct.pack('<QQQQII', len(stub_bytes), len(packed), len(archive), len(files), crc, 0)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(stub_bytes)
        f.write(packed)
        f.write(footer)
    digest = hashlib.sha256(open(out, 'rb').read()).hexdigest()
    print(f'{out}: {len(files)} arquivos, {len(archive)/1e6:.1f} MB -> {os.path.getsize(out)/1e6:.1f} MB')
    print(f'sha256 {digest}')
    with open(out + '.sha256', 'w') as f:
        f.write(f'{digest}  {os.path.basename(out)}\n')
    return 0

if __name__ == '__main__':
    sys.exit(main())
