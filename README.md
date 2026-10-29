![Cue](docs/cue-banner.png)

# Cue · .tinypeep

**Versão 1.0** · Windows x64

Player de vídeo e áudio gratuito para Windows: bonito, leve, local, com legendas como recurso principal. Linguagem visual do Omni.

## Instalar

Baixe `Cue-<versão>-Setup.exe` na página de [releases](https://github.com/gabccin/cue/releases) e abra.
O instalador não pede administrador: o Cue vai para `C:\Cue` (dá pra escolher outra pasta), entra no
menu iniciar e em Configurações → Aplicativos (de onde também se desinstala).
`Cue-Setup.exe --silent` instala sem janela.

Na primeira abertura, quatro telas de boas-vindas apresentam o Cue, a iluminação dinâmica, o
tamanho da interface e o que vem por aí (`Cue.exe --welcome` mostra de novo).

O Cue procura versões novas a cada 15 dias no GitHub Releases. Como publicar uma: [docs/ATUALIZACOES.md](docs/ATUALIZACOES.md).

## Rodar

1. Compile (abaixo) ou use `build/Cue/Cue.exe`.
2. Use a `libmpv-2.dll` do pacote Windows 1.0 ou compile o ZIP **Cue-1.0-Engine-Source.zip** da release. Para copiar uma DLL já obtida, com verificação de SHA256: `powershell -ExecutionPolicy Bypass -File tools\fetch-libmpv.ps1 -LocalDll caminho\libmpv-2.dll`.
3. Abra o `Cue.exe`. Para aparecer em "Abrir com" do Explorer: `Cue.exe --register` (só no usuário atual, não muda o app padrão).

O código próprio do Cue está sob **GPL-3.0-or-later**. Veja `LICENSE`, `COPYRIGHT.md` e [licenças e pendências de publicação](docs/LICENSES.md). O motor (build 1.1.4) foi compilado a partir de fontes arquivadas, publicadas em cada release como **Cue-<versão>-Engine-Source.zip**. As licenças/avisos acompanham o motor. Os assets próprios Omni têm origem declarada pelo autor e licenças registradas em [ASSETS.md](docs/ASSETS.md).

A [revisão de pré-publicação](docs/PREPUBLICACAO.md) registra os testes, os limites da verificação e as pendências restantes.

O Cue usa uma única janela. Ao abrir outro vídeo pelo Explorer ou por “Abrir com”, ele substitui o vídeo atual nessa janela e guarda a posição do anterior. Se a janela estiver minimizada, ela é restaurada. Abrir o Cue novamente sem um arquivo traz a janela existente para a frente.

A iluminação dinâmica começa a ser preparada 1,5 segundo após o primeiro frame, depois das animações de abertura. A geração de miniaturas continua aguardando 3 segundos. O vídeo começa antes; a luz entra suavemente assim que as primeiras cores são lidas. Os dois intervalos reiniciam a cada troca de vídeo.

A luz interpola as cores a aproximadamente 30 Hz, com máscaras e cores pré-calculadas para reduzir o trabalho a cada atualização. A amostragem da cena usa o instante atual do vídeo, sem reutilizar miniaturas distantes na timeline. Ao abrir outro arquivo, os pedidos antigos são descartados e a iluminação reinicia para o novo vídeo.

### Áudio sem vídeo

MP4 com apenas uma faixa de áudio, OGG, Opus, MP3, M4A, AAC, WAV e FLAC abrem no mesmo player. O Cue identifica as faixas reais do arquivo; a extensão MP4 não implica que exista vídeo. Capas de álbum também permitem o modo de áudio.

Nesse modo, a barra de controles permanece visível e uma waveform pixelada reage aos samples decodificados, em branco sobre fundo preto. A visualização usa uma cópia do áudio já decodificado, a 30 quadros por segundo, sem outro processo ou análise do arquivo inteiro. Ao arrastar a timeline, o tempo escolhido aparece na barra e a reprodução continua; a busca acontece uma única vez ao soltar. Os quadros iniciais de preparação da waveform são descartados para evitar o piscar após uma busca. Pausar também pausa a waveform. Miniaturas e luz ambiente de vídeo não são iniciadas em arquivos só com áudio. Ao trocar para um vídeo, retornam o desenho e o comportamento normais.

Se a biblioteca de reprodução não conseguir iniciar a visualização, o Cue tenta abrir o áudio novamente com os controles e sem a waveform.

Pasta final:
```
Cue.exe
libmpv-2.dll
fonts\CueOmni.ttf  CueClean.ttf  CueReadable.ttf  (+ licenças)
LICENSES.md  LICENSE  COPYRIGHT.md
third_party\libmpv\  (licenças, versão, hashes e fontes)
```
Preferências e estado local: `%APPDATA%\tinyoffice\Cue\state.json` (com `state.json.bak`; um arquivo corrompido é guardado como `.corrupt` e o app segue com o backup ou os padrões).
O instalador não carrega os ZIPs de código (são grandes): `fontes\LEIA-ME.txt`, na pasta instalada, aponta para a página da release, onde ficam o código do Cue (Source code, gerado pelo GitHub) e `Cue-<versão>-Engine-Source.zip`.

Frames salvos: `Imagens\Cue\Frames`.
Na primeira abertura, o Cue traz a pasta antiga `tinyoffice\Player` (Recentes, posições, preferências) e os frames de `.tinyoffice Player`, e troca as fontes de legenda registradas com o nome antigo.
O cadastro de “Abrir com” também é atualizado automaticamente para **Cue** e para o caminho atual do executável. Isso não muda o aplicativo padrão escolhido no Windows. Se o menu estiver aberto durante a atualização, feche-o e abra novamente.
O registro de “Abrir com” e das fontes acontece em segundo plano, depois da abertura.
Diagnóstico do motor: `Cue.exe --log` grava `mpv.log` na pasta de estado (fora isso o mpv não escreve log, que deixava abrir mais lento).

## Interface

Em **… → sobre o Cue**, um painel centralizado mostra a versão e “made with ♥ by Gabriel Benzoni”. O nome e o botão **LinkedIn** abrem [Gabriel Benzoni no LinkedIn](https://www.linkedin.com/in/gabrielbenzoni); **GitHub** abre [gabccin](https://github.com/gabccin) no navegador padrão. O painel funciona também durante a reprodução de áudio, aceita teclado e fecha ao clicar fora; `Esc` ou a seta de voltar retornam ao menu. O botão **licenças** abre o leitor de créditos e textos de licença dentro do próprio Cue, com seletor por componente. **Códigos-fonte** abre a pasta `fontes`, com o endereço da release onde estão os ZIPs do código do Cue e das fontes do motor. O acesso a Sobre o Cue fica somente no menu Mais. Os créditos ficam fora da tela inicial.

Com um arquivo aberto, **botão direito → sobre o arquivo** mostra no canto superior direito: nome, tamanho (unidades decimais), duração, formato/contêiner, resolução original, FPS informado pelo arquivo e codec de vídeo; codec de áudio, canais e frequência. Em arquivos só de áudio, resolução/FPS do desenho da waveform não aparecem. Dados indisponíveis são identificados. O painel acompanha o redimensionamento e fecha no X, com Esc ou clique fora, sem pausar a reprodução. A leitura acontece somente quando solicitada e usa os dados já sondados pelo motor.

No menu **… → interface**, escolha **pequena · média · grande · maior**. Média vem selecionada em uma instalação nova. A escala cresce com a janela até um tamanho confortável e, depois disso, na metade do ritmo: na tela cheia os controles ficam um pouco maiores que na janela, sem tomar a imagem (vale para os quatro tamanhos). A largura manda, então um clipe widescreen e um 16:9 da mesma largura têm os mesmos controles. Enquanto você arrasta a borda, o tamanho espera você soltar; quando a janela se ajusta a um vídeo, já começa no tamanho final. Numa janela de 1280 px a 100% de DPI, as escalas são 1×, 1,5×, 2× e 3×. A escolha fica salva. `Ctrl +` e `Ctrl −` percorrem os quatro tamanhos; `Ctrl 0` volta à média. Em vídeo vertical a barra fica empilhada e maior.

A escala média desenha diretamente em pixels físicos, sem blur, supersampling ou buffers de tela dobrados. O tamanho do vídeo e das legendas não muda.

O ícone de música ao lado de **CC** abre a seleção de trilha de áudio e aparece somente quando o arquivo oferece mais de uma trilha. Clique simples no vídeo pausa/retoma imediatamente; `F` ou duplo clique alternam tela cheia com uma transição dos controles.

## Desempenho

A interface evita trabalho quando está escondida ou parada, suspende suas animações quando minimizada e libera os grandes buffers da abertura assim que o motor confirma que não os está usando. O desenho e os recursos de reprodução são preservados. Veja [as medições e os limites dos testes](docs/PERFORMANCE.md).

A janela pinta antes de carregar a DLL do motor. Ao abrir um arquivo pelo Explorer, a animação de saída da home acontece durante a preparação do motor. A abertura pixelada mantém sua duração de 0,36 s. Shaders compilados são reutilizados em `%APPDATA%\tinyoffice\Cue\shader-cache`; `cue.log` registra os marcos da abertura em milissegundos.

O duplo clique muda a tela sem pausar ou reiniciar a reprodução.

Os cantos da janela pedem explicitamente o arredondamento nativo do Windows 11. Ao restaurar a janela depois de fullscreen ou maximização, essa preferência é reaplicada; em fullscreen e maximizada, os cantos ficam retos. O recorte pertence ao compositor do Windows e inclui o vídeo, sem máscara por frame.

O arrasto da janela usa o mecanismo nativo do Windows. Durante o movimento, a iluminação reutiliza suas imagens; reconstruções de geometria e novas decodificações de miniaturas aguardam o fim da interação.

## Compilar

Testes portáveis: `bash tools/test.sh` (GCC, Linux). A execução real do motor e as interações do Windows precisam ser verificadas em Windows.

- **MSVC:** abra "x64 Native Tools Command Prompt for VS" e rode `build.bat`.
- **MinGW-w64 (MSYS2):** `make CROSS=`
- **Do Linux:** `make` (precisa de `g++-mingw-w64-x86-64`).
- **Instalador:** com `libmpv-2.dll` em `build/Cue/`, `make setup` gera `dist/Cue-<versão>-Setup.exe`
  (`installer/`: o programa, `tools/make_setup.py`: o empacotamento LZMA).

Sem dependências além do Windows SDK. A libmpv é carregada em tempo de execução, sem headers nem `.lib`.

## Atalhos

| Tecla | Ação |
|---|---|
| Espaço | play / pause (sem mostrar o dock) |
| ← → | 5 s |
| botões « » | exatamente 15 s (o valor aparece na dica) |
| L ou o botão de repetir | repete o vídeo sem parar (fica salvo) |
| arrastar a legenda | o vídeo pausa e o cinza se espalha a partir da legenda, que levanta na mão; os controles saem. Ao soltar, a cor volta do mesmo jeito e ele continua |
| [ ] ou o botão 1× da barra | velocidade: o botão abre um slider de 0.25× a 3× com − e + (passos de 0.05) e atalhos 0.5, 1, 1.25, 1.5, 2 e 3 |
| \\ | velocidade 1× |
| Ctrl + / Ctrl − / Ctrl 0 | próximo tamanho / tamanho anterior / padrão |
| Shift + ← → | 30 s |
| ↑ ↓ / roda do mouse | volume (vai até 125%, acima de 100% a linha fica laranja) |
| M | mudo |
| F, Alt+Enter, duplo clique | tela cheia |
| Esc | fecha painel, sai da tela cheia, depois volta pro início (ou a setinha no canto superior esquerdo) |
| S | liga/desliga legenda |
| Z / X | legenda 0,1 s antes / depois |
| Ctrl+O | abrir |
| Ctrl+V | abrir vídeo ou legenda copiados no Explorer |
| Tab / Shift+Tab | navegar no dock e nos painéis |
| clique direito | sobre o arquivo, salvar frame (sem legenda; o aviso "frame salvo" tem um botão que abre a pasta com o arquivo selecionado), fechar vídeo |
| C | painel de legendas |
| clique num dos tempos | alterna o tempo da esquerda entre decorrido e restante |

Quando o vídeo termina, ele pausa no último quadro (não volta pro início sozinho). Dar play ali recomeça do começo.

## Desenvolvimento

- `src/core` e `src/ui` são portáveis. `tools/preview.cpp` renderiza a home, o dock, os painéis e os easter eggs em PNG no Linux, para revisar o visual sem Windows:
  ```
  g++ -std=c++20 -O1 -DWITH_PLAY -o preview tools/preview.cpp tools/preview_play.cpp src/ui/*.cpp src/core/*.cpp
  ./preview out 2
  ```
- `tools/gen_assets.py` gera `src/ui/assets.inc` a partir do bundle do design system Omni (glifos, ícones, sprites) e dos ícones novos do Cue.
- `tools/build_fonts.py` gera as três fontes de legenda.
- `tools/brand_images.cpp` desenha, com o próprio renderizador do Cue, a capa do GitHub (`docs/cue-capa.png`, 1280×640), o banner do README (`docs/cue-banner.png`) e o ícone (`docs/cue-icone.png`), centralizados pela parte visível de cada bloco; cada imagem sai também em camadas (`*-camadas/`), e `tools/brand_psd.py` monta os PSDs a partir delas (`g++ -std=c++20 -I. -o brand tools/brand_images.cpp src/ui/*.cpp src/core/*.cpp && ./brand`).
- `tools/gen_action_icons.py` regenera os ícones de ação a partir dos SVGs em `assets/icons/` (precisa de Inkscape e Pillow só na máquina de desenvolvimento; o resultado compilado já acompanha o código).
- Arquitetura e decisões: `docs/ARCHITECTURE.md`. Licenças: `docs/LICENSES.md`.

As miniaturas ao passar o cursor na timeline usam cerca de um quarto dos pixels anteriores e mantêm o tamanho de exibição. A resolução usada para a amostragem da luz ambiente é preservada.
