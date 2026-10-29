# Fontes correspondentes ao motor — Cue 1.1.4

O motor desta versão foi compilado localmente a partir dos 16 arquivos de
fonte identificados em sources.json. Todos esses arquivos originais estão
em **Cue-1.0-Engine-Source.zip** (o pacote do build 1.1.4 do motor, publicado com o
nome da versão do Cue), acompanhados dos avisos/licenças e das
receitas efetivamente usadas. Não depende de um link upstream "latest".

- DLL: libmpv-2.dll
- SHA256 DLL: `26fa28434e30aa319a8ab39fba76dbc4a84757b89349c1fc69b5e55ea6c02bca`
- SHA256 ZIP de fontes: `6ce73f8f9b7209e390f45c16f6ec0fbdfab383eddb7a7726656f1f24b4fcb13e`
- Distribuição combinada: LGPL-3.0-or-later, preservadas as licenças individuais.

O ZIP de fontes inclui os códigos de mpv, FFmpeg, libplacebo, libass,
HarfBuzz, FriBidi, FreeType, dav1d, zlib, Shaderc, glslang, SPIRV-Tools,
SPIRV-Headers, SPIRV-Cross, Vulkan-Headers e FastFloat. As ferramentas de
compilação de uso geral são descritas no README desse ZIP; os avisos e as
exceções de runtime de GCC/libstdc++/MinGW estão em notices/.

## Como distribuir

1. Disponibilizar o pacote Windows, o código do Cue e o ZIP de fontes do motor.
2. Manter acesso claro ao ZIP de fontes junto do download do aplicativo.
3. Preservar os avisos desta pasta e as licenças incluídas.
4. Manter a DLL substituível por versões modificadas compatíveis.

## Validação

SHA256, exports de API, importações de DLLs, configuração de codecs/filtros,
compilação e testes portáveis foram verificados. Não há dependências de
runtime externas além das DLLs do Windows nas tabelas de importação.
O usuário confirmou o funcionamento da build 1.1.4 no seu Windows. A validação
local foi portável/estática; Wine não pôde executar neste ambiente porque
sockets locais não estão disponíveis.

Este material cobre o motor. Não comprova a autoria dos assets visuais Omni.

## Distribuição 1.0

O instalador não carrega os ZIPs de código (são grandes). Eles ficam na página da
versão no GitHub, ao lado do instalador (https://github.com/gabccin/cue/releases/tag/v1.0):
Source code (zip), gerado pelo GitHub, e Cue-1.0-Engine-Source.zip. A pasta instalada traz
fontes/LEIA-ME.txt com esse endereço, e o leitor de licenças do Cue abre essa pasta.
