# Cue · arquitetura

**Cue** (antes "Player"). Família .tinypeep, linguagem do Omni (design system "Omni").

O arrasto da faixa superior de reprodução é encaminhado de forma assíncrona a `WM_NCLBUTTONDOWN/HTCAPTION`. O Windows passa a controlar o movimento, incluindo Snap e restauração de uma janela maximizada; o timer da UI não reposiciona mais a janela. O gesto da UI é limpo antes da passagem, e uma soltura atrasada do botão não aciona pausa. Durante `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE`, a luz reaproveita os pixels existentes e adia reconstruções de forma; a amostragem e as miniaturas também aguardam o fim da interação. `tests/window_drag_test.cpp` cobre essa passagem de estado e o retorno dos cliques e da ocultação automática.

### Arquivos somente de áudio

O hook `on_preloaded` consulta as faixas reais antes de criar os decodificadores. Quando há áudio sem vídeo (desconsiderando capas de álbum), configura opções locais para manter uma saída visual e usar o relógio do áudio. `core/audio_mode.h` seleciona a faixa e monta um grafo `lavfi-complex`: `asplit` preserva a saída sonora e envia uma cópia dos samples para `showwaves`. A imagem nasce em 160×48 a 30 Hz, ganha escala por vizinho mais próximo e uma grade de pixels, e é centralizada em 1024×576. Nenhum segundo decodificador, captura de tela ou análise completa do arquivo é necessário.

A barra permanece visível no modo de áudio; miniaturas e amostragem da luz ambiente ficam desativadas. As opções locais são restauradas pelo motor ao descarregar o arquivo, para que o vídeo seguinte retome sua configuração normal. Se o grafo falhar ao iniciar, uma única tentativa abre novamente o áudio sem visualização.

O arrasto da timeline em áudio atualiza a indicação do tempo sem enviar buscas intermediárias ao motor. Ao soltar, envia uma única busca exata, evitando descarregar o filtro a cada movimento do mouse. O grafo descarta os dois quadros iniciais de preparação com `trim`, preservando os timestamps e o áudio. O arrasto com prévia em vídeos continua funcionando como antes.

O hook `on_unload` solta a referência à janela do motor antes de sua destruição, inclusive na troca entre vídeo e áudio. O procedimento original fica associado a cada HWND, evitando uma referência global nula ou pertencente à janela seguinte em callbacks pendentes.

`tests/audio_test.cpp` cobre detecção, capas, preferência de faixa, permanência dos controles e retorno ao modo de vídeo. `tests/audio_engine_test.cpp` é uma verificação adicional para Windows, compilada com `src/win/mpv.cpp`; recebe o diretório da DLL, um MP4 somente de áudio, um OGG e um MP4 com vídeo. Verifica decodificação, dimensões da visualização, relógio, pausa, busca e troca de arquivos com a DLL usada na distribuição.

## 1. O que vem do Omni (reuso direto)

| Omni | Uso no Cue |
|---|---|
| Cores `bg screen sheet tip-bg hover press edge faint dim fg pixel` | Tokens idênticos em `src/ui/gfx.h` (`tok::`) |
| Omni Mono (bitmap 12 linhas, larguras variáveis, acentos PT-BR) | Toda a UI, glifos copiados 1:1 do bundle do design system |
| `art-px` = 2/3/4 px físicos por px de arte (escala inteira) | `s` em toda a UI; nunca escala fracionária |
| `corner` (canto de 1 px de arte vazio) | Linhas com hover, botões, toasts; dock e painéis com canto em degrau |
| Ícones pixel monocromáticos (bloco 1, 5..9 px de arte) | Reusados: `k-video sound-on sound-off k-audio dots check folder chev-right`. Novos no mesmo padrão: `play pause rewind forward cc fs-enter fs-exit minus plus resume grip chev-left` |
| Face (olhos bloco 3, molas, piscar 2,2..6,4 s, olhar ≤ 3 px de arte) | Personagem da home, mais calmo (piscar 3..8 s, sem boca, sem café/dança/confete) |
| Estados de ícone: `dim` repouso, `fg` hover, `pixel` ação principal | Dock e painéis |
| Tooltip `tip-bg` + `edge`, 450 ms | Timestamp da timeline, toasts |
| Durações `hover 110 · enter 260 · tip-in 140 · blink 110` | `tok::` |
| Molas (olhar 30·0,82; layout 17·0,86; painel 19·0,68) | `Spring` em `src/ui/anim.h` |
| Tom: pt-BR, minúsculas, sem ponto final, sem exclamação, sem emoji | Todo texto, exceto "Recentes" (o briefing pede maiúscula) |
| Edge acende para `dim` ao arrastar algo | Borda da janela (DWM) acende ao arrastar vídeo na home |

Adaptações intencionais: personagem só na home; janela com barra de título nativa escura (Snap Layouts, sem desenhar controles próprios); dock translúcido (o Omni não tem translucência).

## 2. Stacks comparadas

| | C++20 + Win32 + libmpv | Rust + windows-rs + libmpv | C#/.NET (WinUI 3/WPF) + libmpv |
|---|---|---|---|
| Startup a frio | ~instantâneo, exe de ~400 KB | ~instantâneo | runtime .NET: lento no primeiro start |
| RAM ociosa | mínima (UI em software) | mínima | 60–150 MB |
| Playback | libmpv com saída d3d11 nativa | igual | igual, mas "airspace" XAML sobre vídeo complica overlay |
| UI pixel-perfect | renderer próprio em escala inteira | igual | brigando com layout/AA do XAML |
| Build verificável hoje | sim (MinGW aqui, MSVC no Windows) | não neste ambiente (std do Windows bloqueado) | não |
| Manutenção | código pequeno, sem dependências | igual + mesma linguagem do Omni | ecossistema grande |

**Escolha: C++20 + Win32 puro + libmpv carregada dinamicamente.**

- Startup: a janela é criada e pintada antes de liberar o carregamento da DLL na thread do motor. A saída animada da home ocorre em paralelo com esse carregamento. A UI só toca no mpv depois do aviso `WM_APP_ENGINE`.
- Aquecimento do vídeo: amostragem de cor e luz ambiente aguardam 1,5 s, e miniaturas 3 s, após o primeiro `PLAYBACK_RESTART`, com a abertura e a mudança de geometria concluídas. A troca de arquivo limpa a luz e reinicia esse prazo. As janelas de halo são criadas sob demanda; a máscara de abertura é rasterizada e enviada no máximo a 60 Hz.
- RAM: sem framework de UI; a home é um buffer BGRA desenhado em software e enviado com `SetDIBitsToDevice`.
- Playback: libmpv decide hardware decoding (`hwdec=auto-safe`, fallback para software) e renderiza em d3d11 numa janela filha. A UI nunca roda na thread de vídeo.
- Engine: libmpv (mesma base do mpv) cobre MKV/MP4/MOV/WebM/AVI, legendas embutidas, SRT, WebVTT, ASS/SSA e PGS via libass/FFmpeg.
- UI custom: dock, painéis e toasts são desenhados pelo mesmo renderer pixel da home e compostos sobre o vídeo pelo próprio mpv (`overlay-add`, BGRA pré-multiplicado). Translucidez real, zero janelas extras, zero blur.
- High-DPI: Per-Monitor v2, `WM_DPICHANGED` aplica o retângulo sugerido e refaz tudo. A escala `s`
  (px de tela por px de arte) é calculada em `src/core/ui_scale.h`; pequena, padrão e grande usam múltiplos inteiros, e média usa o ponto intermediário entre pequena e padrão, com bordas arredondadas a pixels físicos:
  100/150/200% dão 2/3/4 exatos; 125/175% escolhem o múltiplo maior só se a tela de 480x270 px de arte
  ainda couber (janela maximizada, tela cheia), senão o menor; janelas enormes com escala baixa
  (TV, 4K a 100%) sobem um degrau; nunca passa do que cabe em 320x180 de arte. Há histerese pra não
  piscar no resize. A home não espalha em telas largas (composição de no máximo 700 px de arte,
  centralizada) e o dock cresce com a janela até 420 px de arte.
- Janela: na primeira vez abre em 16:9 com ~2/3 da largura da tela (mínimo 960x540 DIPs); depois
  reabre onde foi deixada, no mesmo tamanho físico, maximizada se estava.
- Packaging: `Cue.exe` + `libmpv-2.dll` + `fonts/`. Sem instalador obrigatório, roda de qualquer pasta.
- Manutenção: renderer, layout e lógica de UI são portáveis (compilam no Linux para prévias e testes); só `src/win/` toca o Windows.

Rust foi a segunda opção pela família com o Omni. O porte é direto (o renderer é o mesmo modelo), mas aqui não daria para compilar para Windows nem provar que funciona.

## 3. Arquitetura

```
             ┌────────────── thread de UI (Win32) ──────────────┐
 arquivo ──▶ │ App (src/win/app.cpp)                            │
 Ctrl+O/V    │   ├─ HomeView  ─▶ Canvas ─▶ SetDIBitsToDevice    │
 drop        │   ├─ PlayView  ─▶ Canvas ─▶ mpv overlay-add      │
 teclado ──▶ │   ├─ Store (JSON em %APPDATA%\tinyoffice\Cue) │
 mouse  ──▶  │   └─ Mpv (libmpv-2.dll via LoadLibrary)          │
             └──────────────▲────────────────────┬──────────────┘
                 wakeup ─▶ PostMessage           │ comandos async
             ┌──────────────┴────────────────────▼──────────────┐
             │ libmpv: demux, decode (hw), d3d11, libass, áudio │
             │ janela filha de vídeo (wid)                      │
             └──────────────────────────────────────────────────┘
```

- Uma janela de reprodução por sessão do Windows. `SingleInstance` mantém um mutex durante a vida do processo; novas aberturas enviam os caminhos UTF-8 por `WM_COPYDATA` e saem antes de carregar o motor. A janela copia e valida o pedido, depois o processa no event loop com `handle_files`/`loadfile replace`. Pedidos recebidos durante a inicialização aguardam o motor em `pending_open_`. O helper `--thumb-host` e `--register` passam antes desse controle. Eventos do mpv chegam por `PostMessage` e são drenados na thread de UI.
- Durante o playback a janela filha do mpv cobre a área. O teclado chega pelo `input.conf` (`script-binding main/...`). O subclass repassa movimentos e botões ao loop da UI sem bloquear o motor; `WM_LBUTTONDBLCLK` é o segundo down da sequência, nunca um clique extra. Os eventos guardam suas coordenadas originais. O polling é apenas fallback de movimento.
- Animação: timer de 16 ms só enquanto alguma mola anda. Parado, a UI não consome CPU.
- Regra PLAYBACK > INTERFACE: overlays só são reenviados quando mudam; comandos ao mpv são assíncronos; scrubbing manda um seek por vez.
- Resume: hook `on_load` do mpv aplica `start` antes de abrir qualquer arquivo (Ctrl+O, Recentes, drop na janela de vídeo).

## 4. Pastas

```
player/
  src/core/   modelo, JSON, Recentes/estado, mapeamento de estilo de legenda → mpv   (portável)
  src/ui/     renderer pixel, assets do Omni, molas, Face, HomeView, PlayView, painéis (portável)
  src/win/    WinMain, janela, DPI, fullscreen, drop target, diálogos, clipboard, libmpv   (Windows)
  assets/fonts/  Cue Omni, Cue Clean (Inter), Cue Readable (Atkinson Hyperlegible)
  res/        ícone e manifesto
  tools/      gen_assets.py, build_fonts.py, preview.cpp (prévias PNG no Linux), fetch-libmpv.ps1
  docs/       ARCHITECTURE.md, LICENSES.md
```

## 5. Estados de UI

```
Home.Idle ──hover Recent──▶ Home.LookingAtRecent
   │  └─drag over──▶ Home.DragOver (olhos seguem, edge acende) ──drop──▶ Opening
   │  └─7 cliques no rosto──▶ Home.Horror(n) ──fim──▶ Home.Idle
   └─Ctrl+O / clique / Open with──▶ Opening ──file-loaded──▶ Play

Play.Clean (só vídeo) ──mouse/tecla──▶ Play.Dock (some em 2 s parado)
Play.Dock ──CC──▶ Panel.CC ──estilo──▶ Panel.Style (legenda arrastável)
          ──áudio──▶ Panel.Audio     ──•••──▶ Panel.More
          ──arrastar fundo──▶ Dock.Dragging ──soltar──▶ snap
          ──timeline──▶ Scrubbing
Play ──fim do arquivo──▶ pausa no último quadro (play recomeça)
Play ──Esc / setinha / fechar vídeo──▶ Home.Idle
```
O dock não some com: scrubbing, painel aberto, legenda sendo arrastada, dock sendo arrastado, mouse em cima do dock.

## 6. Componentes

| Componente | Arquivo | Papel |
|---|---|---|
| Home | `ui/home.*` | composição 42/58, micro-instrução, hit test, foco por teclado |
| Face | `ui/face.*` | olhos, piscar, olhar, pensar, seguir drag e Recentes |
| Recents | `ui/home.*` + `core/store.*` | até 6 linhas: ícone, nome cortado no meio, posição, linha de 1 px |
| FloatingDock | `ui/play.*` | layout em 2 linhas, translucidez, arraste com snap magnético, visibilidade |
| Timeline | `ui/play.*` | faint/fg/pixel, timestamp no hover, scrub com seek único em voo |
| CCPanel | `ui/play.*` (Panel) | trilhas, + legenda, estilo, sincronia |
| SubtitleStyleEditor | `ui/play.*` (Panel) | 3 fontes, tamanho, opacidade texto/fundo, posição por arraste, original/meu estilo |
| SubtitleRenderer integration | `core/sub_style.*` | estilo → propriedades do libass via mpv, trilha bitmap/ASS esconde o que não funciona |
| AudioTrackPicker | `ui/play.*` (Panel) | nomes de idioma legíveis, sem jargão |
| HorrorEasterEgg | `ui/home.*` | 3 microcenas + encarada rara, prioriza a não vista |

## 7. Wireframes

Home
```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│                          Recentes                         ··· │
│        ▆▆    ▆▆                                              │
│        ▆▆    ▆▆          ▣ Dune Part Two.mkv        1:24:12  │
│                          ▣ Severance S02E03.mkv       31:09  │
│      abra um vídeo  ▭    ▣ entrevista.mp4             08:42  │
│                          ▣ filme.mov                  52:17  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

Playback (mouse em movimento)
```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│                        (vídeo)                               │
│                                                              │
│                  esta é a legenda                            │
│      ╭──────────────────────────────────────────────╮        │
│      │ ◁) ▬▬   «   ▶   »            CC  ♪  ⛶  •••   │        │
│      │ 31:09 ━━━━━━━━━●────────────────────── 52:17 │        │
│      ╰──────────────────────────────────────────────╯        │
└──────────────────────────────────────────────────────────────┘
```

Editor de estilo
```
                    ┄┄┄┄┄┄┄┄┄┄┄┄ safe top ┄┄┄┄┄┄┄┄┄┄┄┄
                    ┄┄┄┄┄┄┄┄┄┄┄┄ centro   ┄┄┄┄┄┄┄┄┄┄┄┄
                  ⁞ [ esta é a legenda ]   ← arrastável
                    ┄┄┄┄┄┄┄┄┄┄┄┄ safe bottom ┄┄┄┄┄┄┄┄┄
          ╭───────────────────────────╮
          │ ‹ estilo                  │
          │ Omni  Clean  Readable     │
          │ tamanho  ━━━━●──     46   │
          │ texto    ━━━━━━━●   100%  │
          │ fundo    ━━━●────    45%  │
          │ posição  arraste a legenda│
          │ usar como padrão · resetar│
          ╰───────────────────────────╯
```


## Rodada de polish (playback → resposta → usabilidade → delight)

- **Entrada.** O mouse chega da janela do mpv pelo subclass (`WM_MOUSEMOVE` repassado na hora, coalescido) e o
  app desenha no mesmo instante (`kick()`), sem esperar timer. O hover responde no primeiro frame
  (`hover_step`: degrau visível e depois easing). Teclado controla o playback sem revelar o dock; só mouse
  (com limiar de 3 px de arte contra tremida do sensor), início de vídeo e transições revelam.
- **Popovers.** O primeiro clique fora fecha e é consumido (nem pausa, nem seek, nem o controle de trás). O painel
  que fecha esmaece onde estava. Esc volta um nível (estilo → CC, velocidade → mais → fechado).
- **Legenda.** Sempre arrastável, sem nenhuma marca visual: só o cursor muda (mãos em pixel art, abertas e
  fechadas). Clique na legenda pertence a ela e nunca pausa.
- **Dock.** Núcleo ‹15 · play/pause · 15› (ícones próprios); velocidade no Mais e um indicador discreto quando ≠ 1×
  (`audio-pitch-correction`). Play/pause é otimista: a ação sai no clique e o morph acompanha. Magnetic hover de
  1–1,5 px de arte (a hitbox não se move). A timeline acorda pela proximidade do cursor.
- **Cor do filme.** O motor de prévia (nunca o de playback) entrega um frame pequeno a cada ~0,75 s com luz ativa, ou 4 s sem ela; não repete a decodificação quando a mídia está pausada. A amostra usa o instante atual, independentemente dos buckets de cache da timeline. `atmosphere.cpp`
  acha a família de cor representativa (ignora pretos e estouros, pesa saturação e tons médios, doma neon) e um
  `ColorSmoother` interpola devagar. Vai só na parte tocada da timeline, no handle e em alguns estados ativos.
- **Luz ambiente.** Janela layered, click-through e sem foco, logo abaixo do player no z-order, com um brilho suave
  das bordas do frame. Segue a geometria real (`WM_WINDOWPOSCHANGED`), some em tela cheia, maximizado e na home.
  Roda e repinta a ~30 Hz no timer lento, fora do caminho de animação, com rampas de cor pré-multiplicadas em cache. O loop de pixels faz uma consulta em tabela, sem multiplicar/dividir RGBA a cada pixel.
- **Janela segue o conteúdo.** Com `dwidth/dheight` e a rotação do vídeo, um vídeo vertical ganha janela vertical
  num tamanho confortável (84% da altura útil, mínimo pra caber o dock), animada em 0,26 s enquanto o vídeo já toca.
  Horizontal fica como está; volta ao tamanho horizontal anterior quando um horizontal abre ou ao voltar pra home.
  Resize manual vale para aquela mídia. Em tela cheia só faz letterbox e adapta na saída.
- **Escalas.** Pequena / padrão / grande mudam o múltiplo inteiro de px de arte (o sistema inteiro muda de tamanho,
  os pixels continuam inteiros); vídeo e legenda não mudam. Persistido.
- **SettingsStore.** `schema: 2` com `prefs` / `session` / `media`, migração do v1, padrões para o que faltar,
  `.bak` do último bom, arquivo corrompido posto de lado, escrita atômica e debounce de 400 ms. Por mídia: posição,
  duração, última abertura, faixas (com idioma, para não errar num arquivo trocado), atraso e visibilidade da
  legenda e o estilo próprio.
- **Reduce motion** do Windows desliga magnetic, transição de geometria, viagem do dock na tela cheia e a íris.

## Leveza

- **Prévias e cor do filme num processo à parte.** `Cue.exe --thumb-host` (thumbhost.cpp) roda o segundo motor
  em modo de segundo plano do Windows (CPU, disco e memória com prioridade baixa) e só no processador, sem disputar
  a GPU com o playback. Conversa por dois pipes, um pedido por vez; um job object garante que ele morre junto com o
  player, e se ele falhar o player só fica sem prévias.
- **Troca de mídia no auxiliar.** Cada abertura e resposta leva uma geração. Respostas atrasadas do arquivo ou processo anterior são ignoradas; falhas são limpas por arquivo e um auxiliar que terminou pode ser reiniciado. Durante um novo load, `END_FILE/STOP` do arquivo antigo é consumido sem abortar a espera por `FILE_LOADED`.
- **Nada de reenviar estilo de legenda.** Cada propriedade só vai ao mpv quando muda: reescrever a fonte fazia o
  libass recarregar fontes e congelava o vídeo a cada resize e entrada/saída de tela cheia.
- **Resize direto na janela do mpv** (assíncrono), sem esperar ele perceber o pai mudar.
- **Vídeo acima de 1440p** desenha só quadros reais (`video-sync=audio`); HDR sem análise de pico por quadro.
- **Interface parada não acorda** a cada quadro do vídeo; timer fino do Windows só durante animações.
- **Luz ambiente** cortada pela área do monitor, forma calculada uma vez por tamanho, recolorida a ~30 Hz durante transições e sem repaint quando a cor não muda,
  força aplicada só como opacidade.

## Abrir e fechar rápido

- **Abrir.** Depois do primeiro paint, a thread do motor mapeia a DLL, cria o mpv, aplica as opções e descobre os
  nomes de propriedade, enquanto a home anima sua saída. O estado é copiado em `engine_cfg_` antes do sinal.
  Fontes e associações do Explorer são registradas em manutenção posterior; shaders têm cache persistente.
- **Sem log do mpv por padrão.** O `log-file` do mpv grava em nível verbose independente de `msg-level`: centenas de
  linhas em disco a cada abertura de app e de vídeo. Só com `Cue.exe --log`.
- **Fontes.** O registro só é escrito quando falta ou mudou, e o aviso `WM_FONTCHANGE` vai sem esperar ninguém
  (antes podia esperar até 200 ms por janela travada no sistema).
- **Fechar.** Na ordem do que a pessoa percebe: some a janela (e o botão da barra de tarefas), some a luz, o som
  pausa; depois salva estado e posição; o processo de prévias é encerrado sem espera (antes eram até 0,8 s com a
  janela ainda na tela); o motor ganha até 1,2 s, já invisível, pra soltar o áudio; e o processo termina com
  `TerminateProcess`, pulando o desmonte demorado da DLL.

## Abertura e gestos

A thread do motor aguarda o primeiro paint antes de mapear a DLL: o loader lock do Windows pode bloquear a criação da janela mesmo com LoadLibrary em outra thread. Depois, inicializa em prioridade normal, em paralelo com a saída da home. O cache persistente de shaders fica na pasta de estado. Registro de Abrir com e instalação de fontes por usuário rodam em uma thread de manutenção após a abertura; as fontes de legenda já são acessíveis por `~~/fonts`. Os logs incluem tempo desde o início do processo para separar primeiro paint, DLL, inicialização, arquivo carregado, primeiro frame e iris.

O primeiro clique na área de vídeo fica pendente pelo intervalo de duplo clique do Windows. Um segundo clique correspondente cancela a pausa pendente e envia apenas Fullscreen (ou WinMax na faixa superior). Teclas, controles, reset e arrasto cancelam gestos pendentes. O timer continua ativo até classificar o gesto. O estado de fullscreen é alterado antes das chamadas Win32 que enviam mensagens síncronas de tamanho, sem recriar o motor.

## Cantos da janela

`apply_window_corners` define `DWMWA_WINDOW_CORNER_PREFERENCE` explicitamente: `DWMWCP_ROUND` em janela normal e `DWMWCP_DONOTROUND` em fullscreen/maximizada. A preferência é atualizada em mudanças de tamanho/estado e forçada novamente ao trocar tema ou composição DWM. Valores iguais são ignorados no resize. Não há região de janela ou camada de recorte da UI, preservando composição, sombra e movimento nativos. No Windows 10 o atributo não é suportado; a chamada fica sem efeito.


## Revisão 1.1 — resposta da interface

- Preferência `ui_scale`: IDs anteriores preservados (`-1`: pequena, `0`: padrão, `1`: grande); `2`: média. A ordem visual e dos atalhos é `-1, 2, 0, 1`. Não muda o esquema do arquivo.
- Renderer: escalas `double`, retângulos e células resolvidos em pixels físicos. Caminho inteiro rápido para os tamanhos existentes. Spans horizontais agrupam pixels de glyphs/ícones; composição sobre destino transparente evita cálculo de blending desnecessário.
- A home continua atualizando sua animação nos ticks existentes, mas limita a rasterização a 60 Hz. A primeira pintura de cada novo período de atividade é imediata.
- `input.conf` só é escrito quando o conteúdo muda. A thread do motor é liberada antes do próximo frame animado. A primeira home usa os recentes salvos; a verificação no disco começa depois da primeira pintura.
- Eventos mpv: um wake pendente por vez; lotes de até 128 eventos devolvem o controle ao loop Win32. O sinal é limpo antes da drenagem para não perder um wake concorrente.
- Comandos mpv comuns usam ponteiros em uma array local em vez de copiar strings para um vetor adicional. Comandos maiores que a capacidade continuam usando o caminho dinâmico.
- Salvamento: snapshot imutável do estado em worker dedicado. Apenas um escritor por vez; pedidos novos aguardam por timer, sem bloquear a UI. Encerramento e logoff aguardam o escritor anterior e salvam o estado mais recente antes de terminar.
- Painéis que apenas mudam de posição reutilizam seus pixels, refazendo alpha só quando conteúdo/opacidade muda. O protocolo de buffers do motor permanece com confirmação antes da reutilização.


## Revisão 1.1.1 — ordem dos tamanhos e miniaturas

A ordem agora é pequena → média → padrão → grande. A média usa `base - 0.5`; padrão (`0`) continua sendo o default. IDs persistidos permanecem iguais. A histerese da média remove seu deslocamento negativo antes de calcular a base.

O pedido de frame ao helper inclui uma largura de saída: zero para amostragem de cores (resolução original), `thumb_hover_width()` para timeline e prefetch. O helper reduz a saída por nearest-neighbour antes de enviá-la pelo pipe. Frames de cor só são reduzidos para o cache de hover depois da análise de cores. O motor principal não recebe essas opções. A largura da miniatura exibida continua `80*s`; a imagem de hover usa `20*s` em vez de `40*s` (com piso de 16 pixels), poupando transferência e cache. A decodificação de origem do helper permanece compartilhada com a amostragem de cores.

## Revisão 1.0.0 (instalador): atualizações, boas-vindas e movimento

- **Movimento.** `Spring` (src/ui/anim.h) resolve o oscilador amortecido em forma fechada:
  sub-, criticamente e superamortecido, sem integração numérica. A mesma curva a qualquer
  taxa de quadros; `Spring::rd(response, damping)` usa os parâmetros do SwiftUI. `Fade` e
  `hover_step` mantêm o contrato (quem desenha usa `ease_out(v)`), mas a saída agora é um
  ease-out sobre o valor exibido (`eased_fall`, sem estado, reversível no meio).
- **Ritmo.** `VsyncPacer` (src/win/vsync.cpp): uma thread espera `DwmFlush` e posta um
  `WM_APP_VSYNC` por quadro, só quando a UI pediu. Home e boas-vindas animam na taxa do
  monitor; o player usa um divisor dela perto de 60 Hz (mesma razão de antes: o motor
  redesenha o OSD a cada overlay). Sem DWM, volta para o `SetTimer` e `timeBeginPeriod`.
- **Present parcial.** A home desenha num buffer de trás, compara linha a linha com o que
  está na tela e invalida só o retângulo diferente; troca os buffers sem copiar.
- **Boas-vindas.** `OnboardingView` (src/ui/onboarding.*) ocupa o lugar da home enquanto
  `onboarding_` está ativo; usa sua própria escala de pixel (legível no tamanho padrão) e
  desenha a amostra da barra na escala real escolhida. Ações: luz, tamanho, terminar.
  Ao terminar, os olhos vão por mola até o rosto da home (`enter(now, keep_face)`).
  Quatro telas: oi (pipoca), iluminação, tamanho e "vem mais coisa por aí" (o que vem,
  com etiquetas de pipoca: milho, panela, estoura).
- **Mover a legenda.** Ao arrastar, a PlayView manda `PA::SubEdit`. O App pausa (e
  retoma ao soltar, se tocava), anima `sub-scale` em degraus de 1,5% (a legenda levanta)
  e o shader espalha o cinza em anel de blocos a partir da legenda (`cue_ax/ay/ar`) e
  devolve a cor da mesma forma ao soltar (`cue_bx/by/br`).
- **Atualizações.** `core/release.*` (portável: versões, prazo de 15 dias, leitura da
  release) e `win/updater.*` (WinHTTP, SHA-256 por BCrypt, inicia o instalador). Estado em
  `session.update` do state.json.
- **Instalador.** `installer/`: ver docs/ATUALIZACOES.md.

### Tela cheia

`step_fullscreen_transition` conduz o snap pelos parâmetros `cue_on`, `cue_dark`,
`cue_px` e `cue_gray` do shader (`blur_shader.h`, última passada em OUTPUT, antes da
legenda e do OSD), enviados com os da nuvem em `glsl-shader-opts`. Etapas: 1 saída,
2 espera o quadro preto e a remoção dos overlays e troca a janela, 3 espera o
renderizador no tamanho novo, 4 entrada.
