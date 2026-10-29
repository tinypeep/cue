# Ajustes 1.1.1

- Ordem de interface: pequena, média, padrão, grande. Padrão continua sendo o default e a média fica meio pixel de arte abaixo dele.
- Hover: largura e altura de saída aproximadamente reduzidas à metade; em 100% de DPI, 80×44 → 40×22, mantendo o tamanho exibido. Isso reduz em 75% os pixels transferidos/cacheados da miniatura nesse exemplo. Não altera o decoder do vídeo nem a resolução de amostragem de cores. O helper continua decodificando na resolução anterior e reduz apenas a saída do hover.
- Testes de escala atualizados e teste de tamanhos de miniatura adicionado. Compilação cruzada Windows x64; tempo real de abertura e reprodução dependem de validação nativa.

# Revisão 1.1 — desempenho e interface média

## Medição local desta revisão

Microbenchmark portável, GCC `-O2`, 1.000 atualizações com redesenho da barra (1280×720, escala padrão), mediana de três execuções:

| Métrica | Código recebido | Revisão 1.1 |
|---|---:|---:|
| Tempo de CPU do benchmark da barra | 232,95 ms | 52,62 ms |

Redução de aproximadamente **77% no tempo deste benchmark**, principalmente por simplificar a composição de pixels sobre buffers transparentes. Não é FPS de reprodução nem tempo de abertura do Windows. O benchmark da home por frame ficou próximo do anterior; a aplicação agora evita rasterizar a home a cada tick de 120 Hz e limita esse trabalho a 60 Hz, mantendo o relógio da animação.

Reprodução: `g++ -std=c++20 -O2 tools/benchmark_render.cpp src/ui/*.cpp src/core/*.cpp -o render-benchmark && ./render-benchmark`. Para comparar, use também o código anterior com o mesmo benchmark, máquina e compilador.

## Abertura e comandos

O motor começa logo após a primeira pintura, antes do próximo redesenho animado. `input.conf` não é regravado quando idêntico. A verificação dos recentes inicia depois da primeira pintura. Escritas de preferências saem da thread de UI; comandos comuns do motor evitam vetores e cópias extras; notificações repetidas são agrupadas. Animações de entrada, waveform, codecs e arredondamento nativo permanecem.

**Ainda sem medição de abertura real em Windows.** A compilação cruzada e os testes portáveis não medem carregamento da DLL, antivírus, driver de GPU, primeiro frame ou áudio no computador final. `cue.log` mantém os tempos dos marcos de abertura para comparar no mesmo computador e com a mesma mídia.

## Validação

`bash tools/test.sh` cobre lógica, entrada de arquivos, inicialização, buffers, duplo clique, luz ambiente, arrasto, áudio, quatro tamanhos de UI, persistência, DPI, hitboxes e rasterização inteira/fracionária. As prévias da opção média foram inspecionadas visualmente. O executável foi compilado para Windows x64; validação nativa de reprodução, fechamento durante salvamento e troca de DPI ainda precisa ocorrer em Windows.

---

## Histórico da revisão anterior (medições daquela versão)

# Desempenho e uso de memória

O Cue mantém o motor de reprodução e os formatos suportados. Esta revisão reduz o trabalho da interface e libera imagens temporárias depois de usá-las.

## Resultados da revisão

| Verificação | Antes | Depois |
|---|---:|---:|
| Executável Windows x64 | 1.888.256 bytes | 1.753.088 bytes |
| 200 mil atualizações do estado da barra, mediana de 3 execuções locais | 140,3 ms | 3,7 ms |
| Alocações temporárias nessas 200 mil atualizações | 400 mil | 0 |
| Dois buffers da abertura, caso de janela 3840×2160, após a confirmação de remoção | 63,28 MiB retidos | 0 MiB retidos |
| Comparações de pixels da interface, antes/depois | 36 estados | 36 idênticos |

A medição da barra usa a lógica portável compilada com GCC e `-O2`; não representa o consumo total de CPU do aplicativo. A contagem de buffers mede a memória devolvida ao alocador, não o uso físico de RAM do processo no Windows. A reprodução, GPU e fluidez final precisam ser avaliadas no Windows com arquivos reais.

## O que mudou

- O estado visível da barra é comparado por valores numéricos. Textos só são formatados quando precisam ser desenhados, e o layout só é recalculado quando muda uma entrada que altera sua geometria.
- Eventos do relógio continuam atualizando a posição da mídia, mas não refazem a interface escondida. Ao mostrar a barra, ela recebe o estado atual antes de aparecer.
- A interface parada não é acordada apenas porque está visível. A ocultação automática continua sendo verificada pelo timer; hover, painéis e animações continuam respondendo aos eventos.
- Quando a janela está minimizada, os timers de animação da interface param. A reprodução continua no motor.
- Grandes buffers de overlays são liberados somente depois que o motor confirma sua remoção. Buffers pequenos continuam disponíveis para reutilização, evitando alocações a cada hover.
- A imagem de fundo da home é descartada ao entrar na reprodução e reconstruída ao voltar. Isso também evita manter um bitmap do tamanho da janela sem necessidade.
- A compilação usa otimização entre arquivos e remoção de seções não usadas. O pacote não depende de novas DLLs de runtime.

## Verificação

`tests/performance_test.cpp` verifica o tempo de vida dos buffers, incluindo remoção com falha e upload pendente, e a invalidação da barra quando algo visível muda. Os testes de lógica, áudio, abertura, iluminação e encaminhamento de arquivos cobrem os comportamentos existentes.

O microbenchmark está em `tools/benchmark_ui.cpp`. Pode ser compilado à parte com GCC no Linux:

```bash
g++ -std=c++20 -O2 tools/benchmark_ui.cpp src/ui/play.cpp src/ui/gfx.cpp src/ui/face.cpp src/ui/atmosphere.cpp src/core/util.cpp src/core/sub_style.cpp src/core/sub_metrics.cpp -o ui-benchmark
./ui-benchmark
```

Os 36 estados comparados incluem três dimensões de janela, vídeo e áudio, toolbar, menus, velocidade e mudança de duração. A biblioteca de reprodução permanece sem alteração; ela é a maior parte do pacote e preserva a compatibilidade de codecs e aceleração de hardware.

## Abertura e duplo clique

A janela faz seu primeiro paint antes de liberar LoadLibrary para o motor, evitando que o loader lock atrase a criação da janela. O motor usa prioridade normal, e sua preparação se sobrepõe à saída animada da home. A iris continua com 0,36 s e rasterização limitada a 60 Hz. Registro de associações e fontes fica em uma thread de manutenção após a abertura. O cache persistente de shaders reduz recompilações em aberturas seguintes, quando há um cache compatível. Não foi medido um tempo de abertura no Windows nesta revisão.

Os marcos `first paint`, `engine DLL mapped`, `engine initialize`, `engine ok`, `open requested`, `file loaded`, `first video frame` e `iris start` no cue.log incluem milissegundos desde o início do processo. Assim é possível separar tempo do shell, carregamento e primeiro frame no computador real.

O duplo clique é recebido por um único caminho nativo; não envia os dois comandos de pausa que antes interrompiam a reprodução. O clique simples na área do vídeo espera o intervalo de duplo clique do Windows, enquanto botão e teclado continuam imediatos. `tests/double_click_test.cpp` cobre áudio/vídeo tocando/pausado, entrada e saída de fullscreen, clique simples único, reset, cancelamento por teclado, maximização e soltura duplicada. A compilação e os testes portáveis passaram; a interação nativa e a duração real da abertura ainda precisam ser verificadas no Windows.


## Revisão 1.1.19

O subclass da janela do motor fazia `GetWindowRect`, `GetCursorPos`,
`GetWindowLongPtr`, `GetDpiForWindow` e `IsZoomed` para toda mensagem, inclusive
mensagens de renderização. A política agora roda somente em eventos de cursor
e início de clique. Bordas usam 18 DIP e alvos de canto 32 DIP, inclusive no host.

O timer de interface passa de 8 para 16 ms. Uploads continuam limitados a 60 Hz
e respeitam buffers em voo. A frequência do vídeo permanece independente.
O modo sem fundo deixa de decodificar amostras a cada 0,35 s: o analisador
de cor do ambiente usa 0,9 s; não há mais seleção de tinta por região.

O blur é um shader local com máscara elíptica, aplicado em OUTPUT antes das
sobreposições. Só fragmentos dentro da nuvem amostram vizinhos. A opacidade
zero passa a imagem original; posição e intensidade são uniforms dinâmicos,
sem recompilar o shader a cada tick. O kernel 7×7 aproxima sigma de 14 px.
Há custo adicional de GPU enquanto o blur estiver visível; não foi medido
seu tempo de GPU ou a taxa de quadros no Windows nesta revisão.

Validação: suite portável completa, testes de reversão da animação, tinta branca,
sombra, geometria da nuvem e oito alvos de redimensionamento. Compilação cruzada
x64 aprovada. O shader foi compilado e renderizado no mpv/OpenGL Linux usando
uniforms compatíveis com a versão local da libplacebo; o motor Windows empacotado
aceita parâmetros DYNAMIC. Capturas de teste confirmam controles nítidos, máscara
sem caixa e posição consistente em vídeos com rotação de 0/90/180/270 graus.
Esses testes não substituem a avaliação nativa no Windows.


## Revisão 1.1.20

O kernel espaçado 7×7 foi substituído por dois filtros gaussianos separáveis
com amostras densas e emparelhadas por interpolação bilinear. As texturas
intermediárias têm metade da largura e da altura; a composição final mantém
o vídeo original fora da máscara e os controles na resolução nativa.
O raio equivalente segue 14 px a 96 DPI, com brilho de 75%.
As passagens ficam inativas quando a barra desaparece.
Capturas OpenGL no Linux confirmam blur contínuo e alinhamento com rotações
0/90/180/270; isso não mede custo de GPU ou fluidez no Windows.

A legenda usa um deslocamento animado independente: a barra pode terminar
de recolher enquanto a legenda ainda desacelera para sua posição original.
O sub-pos é enviado com três casas decimais, em vez de arredondado a 1%
da altura, e as atualizações acompanham o timer existente da interface.

Os testes cobrem as duas barras, retorno monotônico sem salto em um frame,
retomada durante o movimento, proporções diferentes por arquivo e todos os
pixels dos botões de janela em múltiplas escalas/DPI.


## Revisão 1.1.21

Animação e uploads usam prazos ancorados em uma fase comum de 60 Hz.
O intervalo de despertar é recalculado depois do trabalho de cada frame,
sem acumular o arredondamento de 16/17 ms. Eventos de entrada respeitam
o mesmo relógio. Uploads usam uma tolerância de 1,5 ms e continuam
preservando o último estado enquanto um buffer está em voo. Essa política
evita que um tick de 16 ms seja descartado por um limite independente
de 16,67 ms, reduzindo a animação a aproximadamente metade da cadência.

A simulação testa dez segundos de atualização, três fases iniciais,
arredondamento de timer em milissegundos, mínimo de 10 ms e um primeiro
frame mais caro. Também verifica recuperação após atraso sem reproduzir
uma fila de frames vencidos. Isso valida a política, não a taxa de quadros
real do Windows ou da GPU.

O movimento das legendas e a troca de aparência usam interpolação quíntica
com parada sem velocidade nem aceleração residual; a inversão conserva
a posição e a velocidade atuais. A legenda desce em 420 ms e sobe em 320 ms;
a troca de aparência leva 320 ms. A máscara, a cor e a geometria usam a
mesma evolução, e os ícones mantêm a rasterização nítida.

A nuvem mantém o filtro separável, com raio de 6 px, intensidade máxima
de 35% e brilho de 95%. Em uma área branca uniforme, o escurecimento
central combinado é de apenas 1,75%. A região vertical é cerca de um
terço menor na configuração padrão, reduzindo a interferência na cena.


## Versão 1.0

O atraso de clique era a espera por `GetDoubleClickTime`. Agora, a primeira
soltura envia imediatamente o destino da pausa. A janela de duplo clique
serve apenas para reconhecer o segundo clique e restaurar explicitamente
o estado inicial antes de alternar a tela cheia. Portanto, um duplo clique
pode ter uma breve mudança de reprodução entre seus dois cliques, mas
preserva o estado final. Respostas antigas de pausa não substituem o
feedback otimista enquanto a solicitação mais recente está em voo.

A troca de tela cheia usa três momentos: fade-out de 90 ms, alteração de
geometria com overlays ocultos e fade-in de 160 ms. A mudança só começa
depois que o motor confirma a remoção dos bitmaps antigos. O retorno dos
controles espera o HWND de vídeo alcançar as dimensões novas e a atualização
de `osd-width`/`osd-height`, com mínimo de dois frames e fallback de um segundo
para motores que não publiquem essas propriedades. Isso evita reapresentar
coordenadas/dimensões antigas durante múltiplos eventos WM_SIZE.

Testes portáveis verificam as bordas do novo viewport, vários layouts/DPI,
seletor de áudio condicional, seleção efetiva de trilha, pausas explícitas
imediatas e restauração no duplo clique. A compilação é Windows x64;
a sequência nativa com o compositor do Windows não foi executada aqui.

# Revisão 1.0.0 (instalador)

| Medição (GCC -O2, Linux, melhor de 15) | antes | agora |
|---|---:|---:|
| Composição de uma camada 900×140 sobre 1280×720, opaca | 1,02 ms | 0,71 ms |
| Mesma camada esmaecendo | 1,62 ms | 1,36 ms |

- Quadros presos ao vblank do compositor: sem batimento entre um timer de 16/17 ms e a tela,
  e sem elevar a resolução do timer do sistema (economia de energia em notebooks).
- A home entrega ao Windows só o retângulo alterado. Parada com os olhos piscando, a área
  copiada cai de 1280×720 para a dos olhos.
- Telas acima de 4,2 MP continuam rasterizando a home a no máximo 60 Hz.
- Ainda vale o aviso das revisões anteriores: fluidez real e tempo de abertura dependem de
  medição no Windows, com a GPU e os arquivos do usuário.
