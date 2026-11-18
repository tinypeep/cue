<p align="center">
  <img src="docs/readme/hero.pt-BR.png" alt="Cue reproduzindo um vídeo, com a luz ambiente brilhando ao redor da janela" width="100%">
</p>

<h1 align="center">Cue</h1>

<p align="center">
  <b>O vídeo é a interface.</b><br>
  Um player de vídeo e áudio gratuito e de código aberto para Windows. Leve, local e feito para deixar o conteúdo em primeiro plano.
</p>

<div align="center">

**[Baixar Cue](https://github.com/tinypeep/cue/releases/latest)** · [English](README.md) · [Histórico de versões](docs/CHANGELOG.md) · [Manifesto](MANIFESTO.pt-BR.md)

</div>

<p align="center">
  <img src="docs/readme/light.pt-BR.png" alt="Luz que acompanha a cena: iluminação ambiente, direto do vídeo." width="100%">
</p>
<p align="center">
  <img src="docs/readme/music.pt-BR.png" alt="Música também fica bonita: capa e espectro em pixels." width="49.2%">
  <img src="docs/readme/remote.pt-BR.png" alt="Seu celular é o controle: leia um QR code, sem instalar app." width="49.2%">
</p>
<p align="center">
  <img src="docs/readme/mini.pt-BR.png" alt="Mini player: continue assistindo enquanto trabalha." width="100%">
</p>

## Feito para assistir

- Interface pixelada com dois estilos de controles e quatro tamanhos que acompanham a janela e o DPI da tela, até 8K.
- Legendas com ajustes de fonte, tamanho, posição e sincronia.
- Iluminação ambiente que acompanha as cores da cena.
- Reprodução de áudio com uma visualização de espectro pixelada que acompanha a música.
- Janela única, posições salvas e prévias sob demanda na timeline.
- A seguir: fila temporária de arquivos e pastas, a um clique na barra de controles e também no controle remoto.
- Oito idiomas, com seleção automática pelo Windows ou escolha manual.
- Controle remoto no celular com uma rodinha de clique das antigas: pareia com QR code, sem instalar app. Modos de tempo, volume e a seguir.

## Comece aqui

Baixe `Cue-1.4-Setup.exe` na [última release](https://github.com/tinypeep/cue/releases/latest) e execute. **Windows x64.** Não exige administrador.

Abra um arquivo, arraste para o Cue ou use **Abrir com** no Explorador de Arquivos.

| Atalho | Ação |
|---|---|
| Espaço | Reproduzir / pausar |
| F ou duplo clique | Tela cheia |
| ← / → | Voltar / avançar 5 segundos |
| Page Up / Page Down | Capítulo anterior / próximo |
| 0–9 | Ir para 0–90% do vídeo (player principal) |
| C | Controles de legenda |
| N / P | Próximo arquivo, quando o Cue encontrar um / arquivo anterior da fila |
| Ctrl+O | Abrir arquivo |
| Q | Abrir a fila |
| Ctrl+Shift+O | Abrir pasta |
| Esc | Fechar painel, sair da tela cheia ou voltar ao início |

Mais controles e preferências: [guia de uso em inglês](docs/GUIDE.md).

## Compilar

Use `build.bat` no prompt de desenvolvimento MSVC x64, `make CROSS=` com MinGW-w64 ou `make` no Linux com `g++-mingw-w64-x86-64`.
Coloque a `libmpv-2.dll` da release em `build/Cue/`. `make setup` gera o instalador.

Testes portáveis: `bash tools/test.sh` (ou `tools\native-tests.bat` no Windows com MSYS2). Reprodução e interações nativas precisam de validação no Windows.

[Arquitetura](docs/ARCHITECTURE.md) · [Idiomas](docs/LANGUAGES.md) · [Publicação](docs/UPDATES.md) · [Desempenho](docs/PERFORMANCE.md) · [Verificações](docs/RELEASE-CHECKLIST.md)

## Agradecimentos

A tradução em russo foi revisada com a ajuda de [omni38](https://github.com/omnizs38). Veja os [agradecimentos](docs/THANKS.md).

## Licença

O código do Cue usa **GPL-3.0-or-later**. Fontes e componentes de terceiros mantêm suas próprias licenças. Veja [LICENSE](LICENSE), [autoria](COPYRIGHT.md) e [licenças dos componentes](docs/LICENSES.md).

As fontes correspondentes ao motor estão em `Cue-1.4-Engine-Source.zip`, na [página da versão](https://github.com/tinypeep/cue/releases/tag/v1.4).

Forks e versões modificadas: [NOTICE](NOTICE.md) · [Identidade e branding](docs/BRANDING.md).
