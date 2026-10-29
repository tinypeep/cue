# Instalador e atualizações

## Como o Cue se atualiza

Sem servidor próprio: o Cue lê a **última release pública** do repositório definido em
`src/core/version.h` (`CUE_UPDATE_REPO`, hoje `gabccin/cue`):

    GET https://api.github.com/repos/gabccin/cue/releases/latest

- **Quando:** a cada 15 dias, alguns segundos depois da abertura (na mesma manutenção que
  registra fontes e "Abrir com"), numa thread de prioridade baixa. Também quando a pessoa
  clica em ··· → sobre o Cue → "procurar atualizações". Sem conexão, nada aparece e a
  próxima abertura tenta de novo.
- **O que conta como versão nova:** a tag (`v1.2.0` ou `1.2.0`) maior que `CUE_VERSION`.
  Rascunhos e pre-releases são ignorados pelo GitHub nessa rota.
- **Qual arquivo:** o asset `.exe` cujo nome tem "setup" (use `Cue-<versão>-Setup.exe`).
- **Segurança:** só HTTPS; o download é conferido pelo tamanho e pelo SHA-256 que o GitHub
  calcula para cada asset (campo `digest`), e precisa começar como executável. O instalador,
  por sua vez, confere o CRC-32 do pacote antes de gravar qualquer arquivo.
- **O que a pessoa vê:** uma linha no canto inferior esquerdo da tela inicial
  ("nova versão 1.2 · atualizar", com um x para pular essa versão), a mesma opção no menu
  ··· e no Sobre. Atualizar mostra o progresso, fecha o Cue guardando a posição do vídeo,
  roda `Cue-Setup.exe --update`, que instala por cima e abre o Cue de novo.

## Publicar uma versão

1. Crie o repositório **público** `gabccin/cue` (ou mude `CUE_UPDATE_REPO`).
2. Suba `CUE_VERSION_*` e `CUE_VERSION` em `src/core/version.h` e escreva a seção da versão
   em `docs/CHANGELOG.md` (vira o texto da release).
3. Coloque `libmpv-2.dll` em `build/Cue/`.
4. `bash tools/release.sh` — roda os testes, compila, gera o instalador e o zip do código e
   cria a release `v<versão>` com o `gh` (autenticado). Anexa também
   `fontes/Cue-motor-fontes.zip` (o pacote do motor) com o nome `Cue-<versão>-Engine-Source.zip`
   (ou `ENGINE_SRC=...`). O código do Cue vai no "Source code" que o GitHub gera para a tag.

Sem o script: `make setup`, depois crie a release no site do GitHub com a tag `v1.0.0` e envie
`dist/Cue-1.0-Setup.exe` (o nome precisa conter "setup" e terminar em .exe) e o pacote do motor
como `Cue-1.0-Engine-Source.zip`.

> GPL: o instalador não carrega os ZIPs de código (89 MB). Eles ficam na mesma página da
> release, e `fontes/LEIA-ME.txt` na pasta instalada aponta para lá.

## Instalador

`installer/setup.cpp` + `installer/setup_ui.cpp` (o mesmo renderizador pixel do Cue). O
executável é o programa + o pacote LZMA anexado no fim (`installer/payload.h`).

| Comando | O que faz |
|---|---|
| `Cue-Setup.exe` | instala, ou atualiza uma instalação existente |
| `Cue-Setup.exe --silent` | instala sem janela (código de saída 0 = ok) |
| `Cue-Setup.exe --update --wait <pid> --dir <pasta>` | usado pelo Cue: espera ele fechar, instala, reabre |
| `uninstall.exe --uninstall [--silent]` | desinstala (o que Configurações → Aplicativos chama) |

Instala em `C:\Cue` por padrão (a raiz do disco do sistema: qualquer usuário pode criar e ser dono dessa pasta, então instalar e atualizar seguem sem administrador). Escreve: atalhos (menu iniciar e,
opcionalmente, área de trabalho), `HKCU\...\Uninstall\Cue`, `HKCU\...\App Paths\Cue.exe` e,
pelo próprio `Cue.exe --register`, o "Abrir com". `install.txt` lista os arquivos
instalados; atualizações removem o que a versão anterior tinha e a nova não tem.

A desinstalação remove tudo isso, as fontes de legenda registradas para o usuário e, se
desmarcado "manter preferências e recentes", `%APPDATA%\tinyoffice\Cue`. Frames salvos em
Imagens\Cue\Frames nunca são apagados. Ela continua a partir de uma cópia em `%TEMP%` (um
programa não apaga a si mesmo enquanto roda), que se remove ao terminar.

Prévias sem Windows: `g++ -std=c++20 -O2 -o preview_setup tools/preview_setup.cpp installer/setup_ui.cpp src/ui/*.cpp src/core/*.cpp && ./preview_setup out`
e, das boas-vindas, `tools/preview_onboarding.cpp`.
