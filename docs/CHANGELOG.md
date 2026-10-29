# Alterações

## 1.0

Primeira versão pública.

**Instalação e atualizações**
- Instalador próprio no visual do Cue (instala em C:\Cue, atalhos, desinstalação pelo
  Windows). Procura atualização no GitHub a cada 15 dias; baixa, confere o SHA-256 e
  atualiza sozinho. Ver docs/ATUALIZACOES.md.

**Primeiro uso** (quatro telas curtas)
- "oi, sou o Cue", com o personagem comendo pipoca.
- Iluminação dinâmica, com um toggle em pixel.
- Tamanho da interface (pequena, média, grande, maior), aplicado na hora; média é o padrão.
- "vem mais coisa por aí": controle pelo celular (estoura na próxima build), transmissão
  sem fio (ainda na panela) e macOS e Linux (ainda é milho), com ícones animados.
- As telas usam uma escala próxima da interface escolhida (no máximo 1,5x), para não
  ficarem enormes em monitores grandes.

**Tamanho da interface**
- A escala cresce com a janela (a partir de 1280 DIPs de largura) até 1,4x e, dali em
  diante, na metade do ritmo: na tela cheia os controles ficam um pouco maiores que na
  janela, sem estourar. Vale para os quatro tamanhos (pequena 0,5, média 0,75, grande 1 e
  maior 1,5). A largura manda: widescreen e 16:9 da mesma largura têm os mesmos controles.
- O cálculo depende só da janela, do DPI e da escolha. Antes, um vídeo widescreen podia
  abrir com uma escala herdada da janela anterior e só "corrigir" depois de mexer no
  tamanho. Agora o tamanho espera você soltar a borda ao redimensionar e, quando a janela
  se ajusta a um vídeo, já começa no tamanho final.

**Barra de controle**
- Organizada por sentido: à esquerda o que muda como o vídeo toca (volume e velocidade,
  com o × desenhado em pixel na altura do número), no centro exato a reprodução
  (« ▶ »), à direita em grupos o repetir, a vista (legenda, áudio, tela cheia) e o "…".
  Mesmo passo entre os botões de cada grupo; "…" no peso dos outros ícones; play com
  um triângulo mais cheio, sem o rabinho.
- Pular 15 s com as setas « » (o valor fica na dica). Repetir (tecla L), aceso com um
  pontinho quando ligado; a escolha fica salva.
- No fim do vídeo a barra fica à vista e o play recomeça do início.
- Layout vertical recomposto para vídeos em pé; animações em mola (estilo Apple).

**Menus**
- "…": abrir e fechar no topo, depois luz ambiente (com ícone), força, tamanho da
  interface e fundo dos controles, e "sobre o Cue". Todos os textos numa coluna só,
  títulos de seção legíveis e com respiro, seletores com opções do tamanho das palavras.
- Os menus nunca passam das bordas da barra.
- Legenda: faixas com nome de site ou grupo no título viram "English 1", "English 2";
  "carregar legenda"; sincronia com vírgula ("+0,3 s"); ✓ e ícones na mesma coluna.

**Tela cheia**
- Transição em "snap" dentro do shader do vídeo: a imagem se quebra em pixels, perde a
  cor e escurece; volta encaixando como um ímã. Sem flash e sem quadro duplicado.
- Sem filete escuro nos cantos arredondados da janela.

**Legenda**
- Ao arrastar a legenda, o vídeo pausa e o cinza se espalha em anel de pixels a partir
  dela, que levanta na mão; barra e botões saem de cena. Ao soltar, ela assenta, a cor
  volta do mesmo jeito e o vídeo continua (se estava tocando). A barra só volta quando o
  mouse se mexe, para a legenda não pular.
- O crescimento da legenda anda em degraus de 1,5%: o libass redesenha o texto poucas
  vezes, sem engasgar.

**Código**
- Removidos o palco de zoom antigo da tela cheia (zoom_stage), helpers sem uso e os
  ícones de 15 s; previews e testes atualizados (quatro telas no primeiro uso).

## Histórico das versões de teste

As betas usavam a numeração antiga (1.1.x); a 1.0 é a primeira versão pública.

## 1.1.21 beta

- Nuvem discreta: blur de 6 px, intensidade de 35%, brilho de 95% e máscara
  mais baixa/próxima dos controles. Mantém o vídeo e a textura mais presentes.
- Legenda retorna em 420 ms, com aceleração e desaceleração progressivas e
  parada suave. Subida em 320 ms. Inversões preservam posição e velocidade.
- Troca de barra em 320 ms: largura, botões, cor dos ícones, card e nuvem
  seguem a mesma curva, com posições arredondadas a pixels físicos.
- Cadência de 60 Hz por prazos fixos, em vez de somar 16 ms ao último tick.
  Uploads e parâmetros compartilham a mesma fase, evitando saltos alternados
  causados por comparar 16 ms com um limite independente de 16,67 ms.
- Proteções de buffers em voo e preferências de movimento reduzido preservadas.
- Testes de movimento, inversão e simulação de timer com variação de milissegundos.
  Compilação x64 e shader OpenGL de teste aprovados; 60 fps na máquina do usuário
  continua dependendo da execução nativa, monitor e carga de reprodução.

## 1.1.20 beta

- Legendas sobem e voltam com uma mola criticamente amortecida independente
  da visibilidade da barra, nos dois estilos. Posição enviada ao motor com
  precisão fracionária, preservando a posição escolhida e o arraste manual.
- Todo vídeo novo ajusta a janela à sua proporção real: vertical, quadrado,
  4:3, 16:9 ou ultrawide. A abertura direta ignora o tamanho salvo anteriormente.
- Interface pequena passa a ser o padrão; sequência pequena, média, grande,
  maior. Ctrl+0 volta à pequena. O antigo padrão migra uma vez; escolhas
  explícitas das demais escalas continuam disponíveis.
- Redimensionamento reduzido a 6 DIP nas bordas e 14 DIP nos cantos. As áreas
  de fechar/maximizar/minimizar e voltar ficam reservadas aos próprios botões.
- Blur em duas passagens gaussianas contínuas, com texturas intermediárias
  na metade da resolução e composição radial na resolução final. Remove
  as réplicas em grade causadas pelo kernel anterior espaçado.
- Testes de movimento das legendas, migração de preferências, proporções e
  proteção dos botões. Compilação x64 e renderização OpenGL de teste aprovadas;
  a interação nativa continua exigindo validação no Windows.

## 1.1.19 beta

- Modo sem fundo com tinta branca uniforme, sombra leve apenas nos controles e
  nuvem de blur radial no vídeo: Gaussian aproximado de 14 px, brilho 0,75,
  dissolução suave sem borda, card ou corte retangular. O shader roda antes da
  composição de ícones e legendas; preserva os pixels nítidos.
- Transição de 380 ms anima largura, posição dos controles, tamanho do play,
  transparência do card e intensidade da nuvem. Pode inverter durante o movimento.
- Redimensionamento: faixas de 18 DIP e cantos de 32 DIP, com a mesma política
  na janela principal, host e janela de vídeo. Tela cheia e maximizado preservados.
- Correção de custo no thread do vídeo: consultas Win32 para hit testing somente
  em cursor/clique, em vez de cada mensagem de renderização. UI limitada a 60 Hz;
  amostragem de cores reduzida a 0,9 s, sem análise regional dos controles.
- Mantém o motor e codecs existentes. Shader com parâmetros dinâmicos e caminho
  rápido quando oculto; sem capturar ou copiar frames do vídeo para criar o blur.
- Compilação Windows x64 e testes portáveis aprovados. Shader e alinhamento
  verificados em renderizador OpenGL de teste no Linux, incluindo rotações
  0/90/180/270. Fluidez e interação nativa ainda exigem teste no Windows.

## 1.1.17 beta

- Contraste local em todos os elementos sem fundo, incluindo tempos,
  progresso, volume e ícones. Amostras atrás de cada controle escolhem
  um tom legível da cor local; a timeline é tratada por trechos.
- Grade leve extraída dos mesmos frames reduzidos do analisador existente,
  com mapeamento da proporção do vídeo e das faixas pretas da janela.
- Atualização mais frequente enquanto os controles estão visíveis, sem
  criar outro decoder. Histerese reduz trocas de tom desnecessárias.
- Permanecem sem card, sombra, contorno ou degradê.

## 1.1.16 beta

- Sem fundo usa a cor dominante da cena, como a barra com fundo, sem
  inverter entre preto/branco e sem sombra ou contorno nos elementos.
- Remove a análise de luminosidade extra; mantém suavização da cor existente.
- Moldura normal com um pixel nativo simétrico e extensão DWM consistente
  para o recorte dos cantos. Maximizado/tela cheia mantêm seus próprios
  tratamentos. Redimensionamento por todas as bordas preservado.
- Recorte nativo compilado; confirmar no Windows, pois não há execução
  da interface Windows neste ambiente.

## 1.1.15 beta

- Mais → fundo dos controles: com fundo / sem fundo, com escolha persistida.
- Sem fundo ocupa aproximadamente 85% da largura (limitado em telas grandes),
  transporte centralizado e timeline longa; vertical conserva três linhas.
- Cor da cena reaproveitada do analisador existente, com luminosidade da
  região inferior, suavização e limiares separados para claro/escuro.
- Contorno de um pixel nos elementos melhora leitura em áreas contrastantes;
  sem card ou degradê. Espaços vazios deixam os cliques chegar ao vídeo.
- Sem decoder ou biblioteca adicional. A aparência atual permanece padrão.

## 1.1.14 beta

- Corrige a interpretação do volume: maior em altura, com trilho de duas
  células e alça permanente de 7 células (9 no hover). Mesma largura e
  tratamento visual em janelas horizontais e verticais.
- Botões de janela com área de clique de 14 células, ícones 25% maiores
  e fechar com cruz de 5 células, tanto em Home quanto na reprodução.

## 1.1.13 beta

- Ao iniciar sem arquivo, ignora uma geometria vertical salva e abre Home
  horizontal. A abertura direta de mídia mantém a adaptação ao vídeo.
- Slider de volume maior no vertical (68 pixels de arte de linha) e menor
  no horizontal (45 pixels de arte), corrigindo a inversão dos tamanhos.

## 1.1.12 beta

- Volume, velocidade e Mais voltam ao topo no vertical; timeline e tempos
  ficam na base. Transporte centralizado permanece na linha intermediária.
- Remove o fundo circular permanente do play/pause no vertical, mantendo
  o mesmo tratamento visual do horizontal e o hover habitual.

## 1.1.11 beta

- Hierarquia vertical: timeline e tempos no topo; transporte central com
  destaque circular discreto no play/pause; áudio, velocidade e Mais na base.
- Slider de volume vertical limitado a 90px de linha; controles secundários
  da última linha com 80% de opacidade. Horizontal preservado.

## 1.1.10 beta

- Reorganiza a barra vertical: volume longo, velocidade e Mais na primeira
  linha; reprodução centralizada, CC à esquerda e tela cheia à direita
  na segunda; tempos e timeline na última.
- Preserva o layout horizontal e os ícones compactos de 15s.

## 1.1.9 beta

- Nome exibido alterado para Cue beta na janela, créditos, propriedades
  do executável e registro Abrir com. Marca de pré-lançamento no recurso
  de versão do Windows. O número continua identificando a build.
- O canal permanece beta até o autor decidir anunciar uma versão final.

## 1.1.9

- Restaura os ícones anteriores de voltar/avançar 15s. Os tooltips e os
  saltos continuam em 15s; os demais ajustes da 1.1.8 foram preservados.

## 1.1.8

- Tela cheia elimina estilos de janela maximizada/minimizada durante a
  transição, evita reentrada, respeita o monitor em mudanças de DPI e
  atualiza novamente o tamanho do vídeo e das camadas ao terminar.
- Barra mais estreita, 12% acima da borda inferior, volume mais longo e
  disposição de três linhas para janelas verticais.
- Mais espaço entre ícones e textos e nas linhas do menu Mais.
- C abre CC; tooltips de avanço/retorno indicam 15s; novos ícones da sheet.
- Play/pause central aparece em cliques no vídeo, botão e teclado.
- Som sintetizado discreto de 80ms somente após frame salvo com sucesso.
- Frames em Imagens/Cue/Frames; migra a pasta anterior se possível.
- Clique em um dos tempos alterna tempo decorrido/restante à esquerda.
- Sobre o Cue aparece somente em Mais. Luz ambiente ativada por padrão
  para preferências novas; escolhas já salvas continuam respeitadas.

## 1.1.7

- Adiciona os seis SVGs fornecidos pelo usuário às ações de salvar frame,
  retorno, informações, abrir arquivo, fechar mídia e sobre o Cue.
- Atualiza a marca para .tinypeep nos créditos, documentação e propriedades
  do executável. Identificadores internos e pastas existentes são preservados
  para manter Recentes, preferências e associações do Windows funcionando.
- Os ícones são compilados na grade pixelada; não há leitura de SVG durante
  a reprodução nem uma nova dependência no pacote.

## 1.1.6

- Botão direito → sobre o arquivo: painel ancorado no canto superior direito,
  com nome, tamanho, duração, contêiner, resolução, FPS e codecs; canais e
  frequência para áudio. A waveform não é confundida com vídeo do arquivo.
- Metadados consultados no motor somente ao abrir o painel, sem novo decoder,
  ffprobe, leitura integral do arquivo ou trabalho adicional na abertura.
- Leitor de licenças/créditos dentro do Cue, com seletor de documentos e textos
  completos, seleção/cópia, rolagem, teclado e acesso aos códigos-fonte.
- Acesso a licenças também pelo botão direito na tela inicial.
- Pacote Windows completo inclui fontes do motor e código do Cue em fontes/.
  Os ZIPs de fonte são dados estáticos e não são carregados durante o playback.

## 1.1.5

- Ícone do aplicativo substituído pelo ICO fornecido pelo usuário, preservando
  suas resoluções de 16, 24, 32, 48, 64, 128 e 256 pixels.
- Ícones normal e pequeno da janela usam os tamanhos correspondentes do ICO.
- Geração de assets da interface preserva o ícone fornecido.
- Motor e ZIP de fontes do motor continuam sendo os da versão 1.1.4.
