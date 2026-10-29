from pathlib import Path
import json,hashlib,tarfile
root=Path(__file__).resolve().parent
for row in json.loads((root/'sources.json').read_text()):
    archive=root/'archives'/row['archive']
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=row['sha256']:
        raise RuntimeError('Checksum incorreto: '+row['archive'])
    dest=root/'src'/row['name'];dest.mkdir(parents=True,exist_ok=True)
    with tarfile.open(archive) as stream:
        prefix=stream.getmembers()[0].name.split('/')[0]+'/'
        for entry in stream.getmembers():
            if not entry.name.startswith(prefix):continue
            entry.name=entry.name[len(prefix):]
            if entry.name:stream.extract(entry,dest,filter='data')
    print('OK',row['name'])
