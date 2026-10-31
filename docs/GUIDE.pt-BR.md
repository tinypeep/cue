# Guia de uso

[English](GUIDE.md)

## Reprodução

Abra um arquivo com `Ctrl+O`, arraste-o para o Cue ou use o menu **Abrir com** do Explorador de Arquivos. O Cue usa uma única janela: abrir outro arquivo substitui a mídia atual e salva a posição dela. Abrir o Cue de novo traz essa janela para a frente.

O Cue reproduz os formatos de vídeo mais comuns por meio da libmpv, incluindo MKV, MP4, MOV, WebM e AVI. Arquivos só de áudio em MP4, OGG, Opus, MP3, M4A, AAC, WAV e FLAC usam uma waveform pixelada reativa. A capa do álbum não impede o modo de áudio. Se a visualização falhar ao iniciar, o Cue tenta de novo sem ela.

No fim de um vídeo, o Cue pausa no último frame. O botão de reproduzir recomeça do início. Ative a repetição para tocar em loop contínuo.

## Controles

| Tecla / ação | Comportamento |
|---|---|
| Espaço / clique no vídeo | Reproduzir / pausar |
| F / Alt+Enter / duplo clique | Alternar tela cheia |
| ← / → | Voltar / avançar 5 segundos |
| Shift+← / Shift+→ | Voltar / avançar 30 segundos |
| Botões « / » | Voltar / avançar 15 segundos |
| L | Alternar repetição; a preferência fica salva |
| ↑ / ↓ / roda do mouse | Volume, até 125%; laranja acima de 100% |
| M | Mudo |
| [ / ] | Ajustar a velocidade de reprodução |
| Botão 1× | Controle de velocidade (0,25×–3×), passos de 0,05 e predefinições |
| \ | Voltar a velocidade para 1× |
| C | Painel de legendas |
| S | Ligar / desligar legendas |
| Z / X | Legendas 0,1 segundo antes / depois |
| Ctrl+O | Abrir um arquivo |
| Ctrl+V | Abrir arquivos de vídeo ou legenda copiados no Explorador de Arquivos |
| Ctrl+T | Sempre no topo (o alfinete ao lado dos botões da janela) |
| Ctrl+mais / Ctrl+menos / Ctrl+0 | Próximo tamanho da interface / anterior / padrão |
| Tab / Shift+Tab | Navegar por controles e painéis |
| Esc | Fechar o painel, sair da tela cheia e, por fim, voltar ao início |
| Clique em qualquer um dos tempos | Alternar entre tempo decorrido e restante à esquerda |

O ícone de música ao lado de **CC** só aparece em arquivos com várias faixas de áudio. O menu dele também oferece **várias faixas**, em que você pode marcar qualquer uma das faixas para tocarem juntas (ou **todas**): feito para gravações em partes, como uma captura do OBS com o jogo, o microfone e o chat de voz em faixas separadas. O Cue mistura esses arquivos por conta própria quando nenhuma das faixas indica um idioma nem tem nome de dublagem ou comentário.
Clique com o botão direito para ver as informações do arquivo, salvar um frame sem legendas ou fechar o vídeo. A confirmação do frame salvo pode abrir a pasta dele com a imagem selecionada.

## Legendas e aparência

Abra **CC → estilo** para mudar a fonte, o tamanho, a opacidade, o fundo e a posição. Alguns formatos de legenda podem limitar as opções de estilo. Arraste as legendas para reposicioná-las; a reprodução pausa durante o gesto e continua depois, se estava tocando. Se elas ficarem difíceis de ler, **CC → restaurar padrão** (aparece, com uma seta circular, sempre que a aparência difere do padrão) traz de volta o seu visual e a sua posição padrão com um clique; clique de novo logo em seguida para desfazer.

O menu **…** contém luz ambiente, interface, fundo dos controles, idioma e sobre o Cue. Há quatro tamanhos: pequena, média, grande e maior; média é o padrão em instalações novas. Cada um fica a um quarto de passo do seguinte, e cada passo continua visivelmente diferente em qualquer janela ou tela. **Ctrl +** / **Ctrl −** (ou Ctrl e a roda do mouse) passam pelos tamanhos de qualquer lugar, e **Ctrl 0** volta para média; o nome do tamanho aparece por um instante. Os tamanhos se adaptam à largura da janela e ao DPI, com um layout empilhado para vídeo vertical. O tamanho do vídeo e das legendas não depende do tamanho da interface.

A luz ambiente acompanha as cores da cena e surge aos poucos depois que a reprodução começa. Ela fica oculta na tela inicial e em tela cheia ou com a janela maximizada.

O Cue está disponível em português do Brasil, inglês, espanhol, francês e chinês simplificado. O modo automático segue as preferências de idioma de exibição do Windows e, se não houver correspondência, usa o inglês. Mude o idioma em **… → idioma** ou no **…** ao lado de Recentes, na tela inicial. As mudanças valem na hora e ficam salvas.

## Controle remoto no celular

Abra **… → controle remoto** enquanto um filme toca, ou **··· → controle remoto** ao lado de Recentes, na tela inicial. O Cue mostra um QR code com o endereço de rede e um código de quatro dígitos. Aponte a câmera do celular para o QR code (ou digite o endereço no navegador do celular e informe o código). O celular e o computador precisam estar na mesma rede. Nada é instalado no celular.

Na primeira vez, o Windows pode perguntar se o Cue pode usar a rede: permita em redes **privadas**. Em uma rede marcada como pública, o Windows impede que os celulares alcancem o Cue.

| No celular | O que faz |
|---|---|
| Girar a rodinha | Percorre o filme; giros mais rápidos dão saltos maiores (5 s, 15 s, 1 min). O Cue muda de posição quando você levanta o dedo |
| Botão central | Alterna a rodinha entre tempo e volume; em menus e pastas, confirma |
| « / » | Volta / avança 15 segundos. Segure « para voltar dez segundos com as legendas visíveis nesse trecho |
| ▶❚❚ ou toque duplo nos olhos | Reproduzir / pausar; ao continuar, volta 3 segundos |
| menu | Legendas (ligar/desligar, sincronia, tamanho, posição), faixa de áudio, timer para dormir |
| Botão de pasta | Navega pelas pastas compartilhadas e abre um arquivo no computador |
| A barra | Toque ou arraste para mudar a posição (ou ajustar o volume, no modo de volume) |

Em menus e pastas, o botão « vira uma seta de voltar; **menu** fecha. A posição da legenda é ajustada arrastando em uma telinha no celular, ou aos poucos com a rodinha e os botões − / +.

**Próximo episódio.** O Cue procura o arquivo seguinte ao atual na mesma pasta: pelo número do episódio (S01E04, 1x04, Ep 4) ou pelo nome. Nos últimos 90 segundos, o celular oferece esse arquivo. Com um celular conectado, ele começa sozinho logo depois que o episódio termina, a não ser que o timer esteja definido para o fim do episódio.

**Pastas compartilhadas.** A tela de pareamento mostra as pastas como etiquetas: Vídeos e Downloads começam ligadas, Área de Trabalho começa desligada e **+** adiciona qualquer pasta. Um celular só pode listar e abrir o que está dentro de uma pasta ligada. As miniaturas são as mesmas que o Windows mostra no Explorador de Arquivos, reduzidas ao tamanho de pixel do controle remoto; arquivos sem miniatura recebem uma paisagem pixelada.

**Privacidade e pareamento.** O Cue escuta apenas na rede local (porta 7878, ou a próxima livre) e só responde a endereços de redes privadas. O código só é aceito enquanto a tela de pareamento está aberta; vários códigos errados bloqueiam o pareamento por um minuto. Um celular pareado guarda um token no navegador; o Cue lembra os oito últimos. **Desligar**, na tela de pareamento, encerra o servidor e avisa os celulares conectados; ele fica desligado até o controle remoto ser aberto de novo.

O navegador do celular não consegue usar os botões de volume para controlar o filme, e só celulares Android vibram com a rodinha (o Safari não dá às páginas da web acesso ao motor de vibração do iPhone); todo celular recebe o tique suave.

## Arquivos e configurações

- Preferências, arquivos recentes e posições: `%APPDATA%\tinyoffice\Cue\state.json` (inclui as pastas compartilhadas do controle remoto e os celulares pareados).
- Backup: `state.json.bak`; um estado corrompido é preservado como `.corrupt`.
- Frames salvos: a pasta Imagens do Windows, em `Cue\Frames`.
- Cache de shaders: `%APPDATA%\tinyoffice\Cue\shader-cache`.

O estado antigo de `tinyoffice\Player` e os frames de `.tinyoffice Player` são migrados na primeira abertura. Os nomes internos das pastas foram mantidos por compatibilidade.

| Comando | Finalidade |
|---|---|
| `Cue.exe --welcome` | Mostrar a apresentação inicial de novo |
| `Cue.exe --register` | Registrar “Abrir com” para o usuário atual; não muda o aplicativo padrão |
| `Cue.exe --log` | Ativar o `mpv.log` na pasta de estado |

Sobre o Cue inclui créditos, um leitor de licenças no próprio aplicativo, links para o código-fonte e a busca por atualizações. O Cue procura atualizações a cada 15 dias. Veja [instalação e atualizações, em inglês](UPDATES.md).
