# Licenças — Cue 1.0

O código próprio do Cue usa GPL-3.0-or-later. LICENSE contém o texto integral;
COPYRIGHT.md identifica a autoria e delimita os componentes de terceiros.

## Motor

A DLL desta versão foi compilada a partir de fontes arquivadas, sem as partes
GPL-only do mpv e sem habilitar GPL/nonfree no FFmpeg. A distribuição combinada
é oferecida sob LGPL-3.0-or-later, selecionando versões posteriores permitidas
pelas licenças LGPL para compatibilidade com os componentes Apache-2.0.
As licenças individuais e avisos permanecem em third_party/libmpv/notices/.
FreeType é usado sob FTL e FastFloat sob MIT.

As fontes correspondentes aos 16 componentes, com versões e SHA256, acompanham
**Cue-1.0-Engine-Source.zip** (build 1.1.4 do motor), publicado na página da versão no
GitHub, ao lado do instalador e do código do Cue (Source code, gerado pelo GitHub).
Veja third_party/libmpv/SOURCES.md e manifest.json.
A DLL continua substituível. Em **… → sobre o Cue → licenças**, o app abre o
leitor de avisos e textos completos de licença. A antiga mpv.dll marcada nonfree não está incluída.

## Fontes do aplicativo e identidade visual

- Cue Clean: Inter 500, The Inter Project Authors; SIL OFL 1.1, avisos incluídos.
- Cue Readable: Atkinson Hyperlegible 400, Braille Institute of America;
  SIL OFL 1.1, avisos incluídos.
- Cue Omni: criada pelo Claude para o projeto, conforme confirmado pelo autor;
  SIL OFL 1.1, com texto e metadados de licença incluídos.
- SVGs, glifos e sprites próprios Omni: criados pelo Claude para o projeto,
  conforme confirmado pelo autor; GPL-3.0-or-later. Ver docs/ASSETS.md.

As fontes OFL permanecem sob OFL; a GPL do código não substitui essas licenças.
Esta revisão não conclui sobre originalidade integral, patentes de codecs ou
registros de marca. A disponibilização dos ZIPs precisa preservar os avisos
incluídos e as condições das licenças aplicáveis.

Na versão 1.0, os códigos-fonte correspondentes ficam na página da versão
(https://github.com/gabccin/cue/releases/tag/v1.0). A pasta instalada traz fontes/LEIA-ME.txt com esse endereço, e o
botão códigos-fonte do leitor abre essa pasta.

## Instalador

- `installer/lzma/` — decodificador LZMA do LZMA SDK (Igor Pavlov), domínio público. Usado só
  pelo `Cue-Setup.exe` para descompactar o pacote; não faz parte do `Cue.exe`.
- O instalador e o empacotamento (`installer/`, `tools/make_setup.py`) seguem a licença do
  código do Cue (GPL-3.0-or-later).
