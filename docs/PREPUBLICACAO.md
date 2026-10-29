# Pré-publicação: Cue 1.0

## Conferido

- Versão 1.0 em `src/core/version.h`; instalador, propriedades do executável, painel Sobre
  e verificação de atualizações leem dali. A tag da release deve ser `v1.0`.
- Compilação Windows x64 sem avisos; 25 testes portáveis aprovados; primeiro uso, legenda e
  tela cheia exercitados no Wine (a experiência final é a do Windows).
- Instalador: instala em `C:\Cue` sem administrador, atualiza por cima, desinstala; quando
  substitui uma versão de teste (1.0.x ou betas), não mostra "versão antiga → nova".
- Atualizações: lê `releases/latest` de `gabccin/cue`, aceita `v1.0` ou `1.0`, baixa o asset
  `.exe` com "setup" no nome e confere o SHA-256 do campo `digest` do GitHub.
- Sem credenciais, tokens, caminhos pessoais ou logs no código. `.gitignore` exclui
  `build/`, `dist/`, logs e `state.json`.
- Licenças: código próprio GPL-3.0-or-later (`LICENSE`, `COPYRIGHT.md`); fontes OFL com
  avisos; avisos do motor em `third_party/libmpv/`.

## Antes de publicar

1. O repositório `gabccin/cue` precisa ser **público** (a API sem login não enxerga
   repositórios privados, e o "procurar atualizações" responde 404).
2. Anexe à release o pacote do motor renomeado para **Cue-1.0-Engine-Source.zip** (as fontes
   da `libmpv-2.dll` incluída; SHA-256 `6ce73f8f…cb13e`).
   O `fontes\LEIA-ME.txt` instalado aponta para esse arquivo na página da release; sem ele a
   distribuição do motor fica sem as fontes correspondentes.
3. Anexe `Cue-1.0-Setup.exe`. O código do Cue é o "Source code" que o GitHub gera para a
   tag (o `tools/release.sh` faz tudo).
4. O executável não é assinado: o SmartScreen pode avisar nas primeiras instalações até o
   arquivo ganhar reputação. Não é um erro do Cue.

## Histórico: revisão da beta 1.1.8

## O que foi concluído

- Motor compilado localmente a partir de 16 arquivos de fonte arquivados.
- Fontes, versões, SHA256, receitas de compilação, configuração e avisos
  estão em Cue-v1.1.4-motor-fontes.zip. Preservar esse ZIP junto da release.
- Distribuição combinada do motor LGPL-3.0-or-later; código próprio do Cue GPLv3+.
- Fontes Inter/Atkinson preservadas com OFL e avisos originais.
- DLL antiga nonfree removida; metadados e versão do manifesto sincronizados.
- Acesso às licenças no painel Sobre; sem informação técnica na tela inicial.
- Código/examples revisados para retirar referência residual à cidade;
  não identificadas credenciais, logs, histórico ou dados de uso nos ZIPs.
- Compilação Windows x64, 14 testes portáveis e integridade dos ZIPs verificados.
- API, importações do motor, filtros de waveform/miniaturas e decodificadores
  centrais conferidos estaticamente.

## Confirmações do autor

- O usuário confirmou o funcionamento no PC da versão 1.1.4, após a troca do motor.
- O usuário confirmou que a fonte e os SVGs próprios Omni foram criados pelo
  Claude para seu projeto. A origem declarada e as licenças foram registradas
  em ASSETS.md. Essa declaração não é tratada como prova de infração nem como
  garantia absoluta de originalidade.
- Cue Omni recebeu metadados e texto SIL OFL 1.1, sem alteração dos contornos.
  SVGs/glifos/sprites próprios acompanham GPL-3.0-or-later.
- Não foi identificado impedimento adicional concreto na revisão realizada.

## Publicação

O pacote Windows completo já contém o código do Cue e as fontes do motor
em fontes/. Preservar esses arquivos junto ao aplicativo.
Avisos e links claros para as fontes precisam permanecer disponíveis ao
redistribuir o programa. A entrega dos ZIPs aqui não publica um repositório.
O executável é sem assinatura digital; Windows pode avisar ou bloquear apps
sem reputação/assinatura conforme a política local. Não desativar proteções.
A revisão não é uma varredura antimalware, pesquisa de marca ou parecer jurídico.

## Verificação da interface 1.1.8

Metadados: testes portáveis de formatação, áudio/vídeo, campos ausentes,
menu do botão direito, limites de tamanho/DPI, estabilidade durante reprodução
e fechamento sem pausa. O painel foi renderizado para inspeção visual.
O leitor nativo Windows foi compilado; a interação nativa precisa ser
conferida no Windows. O motor permanece o mesmo testado na versão 1.1.4.

## Ícones e marca 1.1.8

Os seis SVGs enviados acompanham o código em assets/icons/. A geração
verifica que cada célula seja opaca ou transparente, sem suavização. As
ações e os painéis foram renderizados para conferir alinhamento e espaço
entre ícones e textos. As propriedades do executável e os créditos usam
.tinypeep. Nomes internos e pastas antigas são mantidos por compatibilidade.

## Revisão 1.1.8

Compilação Windows e testes portáveis cobrem controles, reposicionamento,
formatos vertical/horizontal, C, alternância de tempo e feedback de pausa.
A correção da transição de tela cheia e o som nativo precisam ser confirmados
no Windows: este ambiente não executa a interface Windows.
