![Cue](docs/cue-banner.png)

# Cue · .tinypeep

**O vídeo é a interface.**

Um player de vídeo e áudio gratuito e de código aberto para Windows. Leve, local e feito para deixar o conteúdo em primeiro plano.

**[Baixar Cue](https://github.com/tinypeep/cue/releases/latest)** · [English](README.md) · [Histórico de versões](docs/CHANGELOG.md)

## Feito para assistir

- Interface pixelada com dois estilos de controles e quatro tamanhos que acompanham a janela e o DPI da tela, até 8K.
- Legendas com ajustes de fonte, tamanho, posição e sincronia.
- Iluminação ambiente que acompanha as cores da cena.
- Reprodução de áudio com uma waveform pixelada reativa.
- Janela única, posições salvas e prévias na timeline.
- Seis idiomas, com seleção automática pelo Windows ou escolha manual.
- Controle remoto no celular com uma rodinha de clique das antigas: pareia com QR code, sem instalar app.

## Comece aqui

Baixe `Cue-1.1-Setup.exe` na [última release](https://github.com/tinypeep/cue/releases/latest) e execute. **Windows x64.** Não exige administrador.

Abra um arquivo, arraste para o Cue ou use **Abrir com** no Explorador de Arquivos.

| Atalho | Ação |
|---|---|
| Espaço | Reproduzir / pausar |
| F ou duplo clique | Tela cheia |
| ← / → | Voltar / avançar 5 segundos |
| C | Controles de legenda |
| Ctrl+O | Abrir arquivo |
| Esc | Fechar painel, sair da tela cheia ou voltar ao início |

Mais controles e preferências: [guia de uso em inglês](docs/GUIDE.md).

## Compilar

Use `build.bat` no prompt de desenvolvimento MSVC x64, `make CROSS=` com MinGW-w64 ou `make` no Linux com `g++-mingw-w64-x86-64`.
Coloque a `libmpv-2.dll` da release em `build/Cue/`. `make setup` gera o instalador.

Testes portáveis: `bash tools/test.sh`. Reprodução e interações nativas precisam de validação no Windows.

[Arquitetura](docs/ARCHITECTURE.md) · [Idiomas](docs/LANGUAGES.md) · [Publicação](docs/UPDATES.md) · [Desempenho](docs/PERFORMANCE.md) · [Verificações](docs/RELEASE-CHECKLIST.md)

## Licença

O código do Cue usa **GPL-3.0-or-later**. Fontes e componentes de terceiros mantêm suas próprias licenças. Veja [LICENSE](LICENSE), [autoria](COPYRIGHT.md) e [licenças dos componentes](docs/LICENSES.md).

As fontes correspondentes ao motor estão em `Cue-1.1-Engine-Source.zip`, na [página da versão](https://github.com/tinypeep/cue/releases/tag/v1.1).
