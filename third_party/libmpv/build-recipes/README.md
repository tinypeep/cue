# Cue 1.1.4 — fontes do motor de reprodução

Este pacote contém os arquivos de fonte efetivamente usados na compilação
local de libmpv-2.dll, suas dependências incorporadas, as receitas de build e
os avisos de licença. Não contém mídias de exemplo nem dados pessoais.

## Compilação

Ambiente: Linux x86_64, GCC/MinGW-w64 POSIX (usado GCC 13), Python 3.12,
GNU Make, pkgconf e NASM 2.16 ou posterior. As ferramentas Python usadas
estão fixadas em requirements-build.txt. As ferramentas de compilação de
uso geral não estão embutidas no pacote; obtenha-as de suas distribuições.

Em Ubuntu/Debian, os pacotes necessários incluem g++-mingw-w64-x86-64,
mingw-w64-tools, make, nasm, pkgconf, python3 e python3-venv. Instale as
ferramentas Python em um venv, ative-o, e execute:

```sh
python3 -m pip install -r requirements-build.txt
bash build-all.sh
```

As fontes são verificadas por SHA256 antes da extração. Nenhum download de
fontes de bibliotecas é feito durante a compilação: os arquivos já estão em
archives/. O resultado fica em output/libmpv-2.dll. Uma diferença de sistema,
compilador ou ferramentas pode alterar o hash; não se promete reprodução
bit a bit com ferramentas diferentes. As fontes e configurações correspondem
ao binário identificado em binary-manifest.json.

## Configuração

- mpv em modo LGPL, sem cplayer e sem partes GPL-only.
- FFmpeg sem GPL/nonfree: decodificadores, demuxers e filtros nativos;
  encoder PNG/rawvideo para screenshots; dav1d para AV1 e zlib.
- libass, FreeType, HarfBuzz e FriBidi para legendas e fontes.
- Renderização D3D11, Shaderc, SPIRV-Cross e libplacebo; D3D11VA e WASAPI.
- As bibliotecas são incorporadas estaticamente à DLL substituível.
- Não são incluídos backends opcionais de DVD/Blu-ray, VapourSynth, Vulkan,
  Lua/JavaScript ou bibliotecas externas de codificação de vídeo.

Alguns nomes de pkg-config foram criados pelas receitas para usar bibliotecas
estáticas; zlib usa uma cópia libz.a de libzs.a. Nenhum patch funcional foi
aplicado ao código de reprodução. As ligações de diretórios das dependências
de Shaderc e fast_float são reconstruídas pelas receitas.

## Licenças

A distribuição combinada do motor é oferecida sob LGPL-3.0-or-later,
selecionando versões posteriores quando as dependências LGPL permitem,
para compatibilidade com componentes Apache-2.0. As licenças individuais
permanecem preservadas em notices/ e nos arquivos de fonte originais.
FreeType é usado sob FreeType License (FTL). FastFloat é usado sob MIT.
GCC/libstdc++ e MinGW têm suas respectivas permissões/exceções de runtime;
as ferramentas do Windows são bibliotecas de sistema e não são fornecidas.

Portions of this software are copyright © The FreeType Project
(www.freetype.org). All rights reserved.

## Publicação

Publique este ZIP ao lado do pacote Windows da MESMA versão e do código do
Cue. Indique o download das fontes do motor junto ao download do programa.
Preserve os ZIPs e o manifesto; não substitua por um link móvel "latest".
Os avisos de notices/ acompanham também a distribuição Windows. Usuários
podem substituir a DLL por uma versão modificada compatível com a API.

Este pacote resolve a entrega das fontes do motor compilado aqui; não
comprova a autoria da fonte Cue Omni ou dos desenhos do aplicativo.
