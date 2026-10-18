# Análise técnica — Layout flexível (painéis arrastáveis, estilo VSCode/JetBrains)

> **STATUS (2026-10-17): implementado na branch `feat/flex-layout`** (fases 0 a 4, sem dependência nova — opção D,
> zonas próprias). Decisões tomadas (padrão depois mudado a pedido: Comandos à esquerda, Saída à direita, abas à mostra; o antigo virou a perspectiva "Clássico"): Comandos no centro e Saída embaixo (visual atual preservado), Comandos pode ser
> fechado (estado vazio com "Restaurar layout"), flutuantes escondem com o Kai, **uma** Saída com abas arrastáveis (a aba
> vira um painel-espelho `run:<id>`), `layout.json` separado. Guia de uso em `docs/layout.md`; código em `src/ui/layout/`.
> O que mudou em relação ao plano: o painel Comandos/Saída com **uma só** aba não mostra a faixa de abas (o visual padrão
> não muda) — o "Reorganizar painéis" do menu Exibir a mostra em todas as zonas; a perspectiva "Compacto" é Comandos sem a
> Saída (rodar algo a reabre). O texto abaixo é a análise original.
>
> Documento de análise/planejamento, escrito antes da implementação. Escrito em
> 2026-10-17 a partir da branch `feat/ai-assistant` (commit `922bae6`).
> Baseado na leitura de `docs/philosophy.md`, `src/ui/main-window.{h,cpp}`,
> `src/core/config-manager.h`, `src/ui/features/output/terminal-drawer.*`,
> `src/ui/features/output/output-panel.*`,
> `src/ui/features/settings/tabs/layout-tab.cpp` e
> `src/ui/features/ai/ai-chat-window.h`.
>
> Todas as regras de `AGENTS.md` valem integralmente (async-only, i18n,
> tokens de raio, testes junto com a mudança, checklist manual na entrega).
> Os números de linha citados são da data acima e podem ter mudado.

---

## 0. Pedido e veredito

**Pedido:** deixar o layout do Kai flexível como no PhpStorm/JetBrains ou no
VSCode. A janela vira um "quadro" com vários painéis dentro, e cada painel
pode ser arrastado para outro lugar, redimensionado, fechado e restaurado
depois. Hoje a Saída, a árvore e as barras de ação ficam em lugares fixos,
ajustáveis só por combos nas Configurações. Lembrete do próprio pedido: o
Kai **não é uma IDE**, mas deve ficar mais dinâmico e vivo na janela.

**Veredito:** faz sentido e é viável. O recomendado é o **modelo de zonas**
(VSCode/JetBrains), não o docking livre estilo Visual Studio, e uma
implementação **própria e leve** (sem dependência nova). O custo principal
não está no docking em si: está em desacoplar o `MainWindow` (8.7k linhas)
da topologia fixa atual.

---

## 1. Estado atual (inventário)

| Área | Como funciona hoje | Onde |
|---|---|---|
| Saída vs. área principal | Um único `QSplitter` externo com 2 filhos (`m_mainAreaWrapper` e `m_terminalDrawer`), **recriado** a cada troca de posição | `MainWindow::applyOutputPosition`, `src/ui/main-window.cpp:1724` |
| Posição da Saída | Combo bottom/left/right em Configurações → Layout | `SettingsData::outputPosition`, `src/core/config-manager.h:133`; `layout-tab.cpp:75` |
| Proporção da Saída | `m_outputRatio` (0..1), persistida em `outputSplitterSizes` com debounce de 400 ms | `main-window.h:772-789`; `config-manager.h:138` |
| Grupos de ação (Item/Exibição/Execução) | 3 grupos × 5 posições (upper/bottom/left/side/hidden) via combos, em 4 containers | `applyActionGroupPlacement`; `config-manager.h:127-129` |
| Destacar a Saída | Uma janela destacada **por comando**, com `AppWindowFrame` (sem moldura) | `TerminalDrawer::detachOutput`, `terminal-drawer.cpp:603` |
| Chat de IA | `QDialog` separado (`AiChatWindow`) | `main-window.cpp:7886` |
| Docs/notas | Ficam **dentro** da Saída (DocViewer criado sob demanda) | `OutputPanel::ensureDocViewer`, `output-panel.cpp:2602` |
| Histórico, processos, notificações, ambientes | Diálogos modais | `src/ui/features/history/*`, `environments/*` |
| Modo foco | `m_commandsCollapsed`: esconde a área de comandos e a Saída ocupa tudo (só na sessão) | `MainWindow::setCommandsCollapsed` |
| Tela de boas-vindas | Esconde o splitter inteiro | `setWelcomeMode` |
| Borda da árvore arrastando o divisor externo | Truque para quando há coluna de ações entre árvore e Saída | `dragTreeBorder`, `updateTreeBorderResizeHandles` |
| Altura das barras | `syncPanelBarHeights` iguala as barras de ação e o cabeçalho da Saída | `main-window.h` |

### O que já ajuda

- `applyOutputPosition` prova que **reparentar painéis já construídos funciona**:
  os widgets mudam de splitter sem ser recriados, e as conexões e o estado do
  `TerminalDrawer` sobrevivem. Essa é a base de qualquer sistema de docking.
- `AppWindowFrame` já resolve a janela própria sem moldura, com cantos/tokens
  (usada no detach), e pode virar a janela flutuante de painel.
- O DocViewer já é criado sob demanda (custo zero até ser usado).
- `MainWindow` já herda de `QMainWindow`, embora isso não seja essencial
  na abordagem recomendada.

### O que atrapalha (o custo real)

- `main-window.cpp` tem ~8.7k linhas, com `m_terminalDrawer` citado 234 vezes
  e `m_commandTree` 145 vezes.
- Várias regras pressupõem "exatamente 2 regiões em um splitter":
  `m_outputRatio`, `applyOutputRatio/captureOutputRatio`, `dragTreeBorder`,
  `syncPanelBarHeights`, modo foco, boas-vindas, `DrawerPosition` (eixo do
  colapso no `TerminalDrawer`).
- Tudo isso precisa migrar para um gerenciador de layout genérico. Esse é o
  grosso do trabalho e o maior risco de regressão.

---

## 2. Encaixe na filosofia (`docs/philosophy.md`)

O "não é IDE ou editor de código" trata de **funcionalidades** (LSP,
refactoring, debugger), não de layout. Painéis arrastáveis não violam isso.
O Kai já tem conteúdo que justifica painéis: comandos, saídas por execução,
docs/notas, chat de IA, telas KIP, resposta HTTP, histórico e processos.

| Princípio | Risco | Mitigação |
|---|---|---|
| 1. Não atrapalha / nunca abre sozinho | Janelas flutuantes reaparecendo sozinhas (boot, troca de monitor) | Flutuantes escondem junto com a principal (atalho global/bandeja) e só voltam com ela. Nunca abrem sozinhas. Se ficarem fora da tela, são trazidas de volta (como a lógica de troca de monitor já faz). |
| 2. Nativo e leve | Dependência pesada, custo de memória | Implementação própria com QSplitter/QTabBar e painéis criados sob demanda. |
| 5. Falha com segurança | Layout corrompido, id desconhecido, painel "perdido" | JSON inválido volta ao layout padrão e registra no log. ids desconhecidos são descartados. "Restaurar layout" sempre disponível. A janela nunca fica sem nenhum painel. |
| 6. Portátil e versionável | Layout é estado pessoal | Fica no armazenamento interno (JSON local), **fora** do YAML do projeto. |
| 7. Explícito e manual | Trocar o layout sozinho (ex.: por projeto) | Qualquer troca automática de perspectiva é opcional e vem desligada. |
| 8. De one-liner a pequeno app | — | Ganho: um comando KIP pode virar um painel fixo (ver §5). |
| 9. Design consistente e acessível | Visual "de outro app" (docking de terceiros) | Tudo com tokens de tema, densidade e raio. Abas e "soltar aqui" seguem o design system. Teclado de primeira classe. |

**Mitigação geral:** o **layout padrão continua idêntico ao de hoje**. A
flexibilidade fica disponível, mas ninguém é obrigado a usar ("o caso simples
continua simples").

**Proposta de linha para a filosofia** (adicionar quando implementar):
> Layout flexível sim: painéis podem ser movidos, fechados e restaurados; o
> layout padrão é simples e é o que todo mundo vê até mexer; janelas
> flutuantes nunca aparecem sozinhas.

---

## 3. Opções técnicas

### A. `QDockWidget` nativo do Qt — não recomendado
- **Prós:** sem dependência; `saveState/restoreState` prontos; o `MainWindow` já é `QMainWindow`.
- **Contras:** visual datado; flutuantes ganham a moldura do sistema (briga com o design sem moldura); indicação fraca de onde soltar; estilizar com os tokens de raio é difícil; exige um widget central; aninhamento limitado a 4 áreas ao redor do centro; estado em blob binário.

### B. Qt Advanced Docking System (ADS) — plano B
- LGPL 2.1, só Qt Widgets, usado em muitos apps Qt.
- **Prós:** sobreposições de "soltar aqui" estilo Visual Studio, grupos de abas, flutuantes, painéis que se escondem nas laterais, perspectivas, widget central opcional, opção de barra de título própria nas flutuantes.
- **Contras:** dependência de ~15k linhas (entraria via FetchContent como o QHotkey); estado salvo opaco (XML/blob); o QSS precisa ser reescrito com os tokens; dá liberdade demais (grade livre = mais jeitos de bagunçar); LGPL com link estático no Windows exige atenção.
- Faz sentido **se** um dia houver demanda real por grade livre (dividir zonas em N partes arbitrárias).

### C. KDDockWidgets — descartado
- Licença GPL/comercial, conflita com o plano de licença OSS (em espera). Também é mais pesado.

### D. Zonas próprias — **recomendado**
- 4 zonas fixas: **esquerda, centro, direita e baixo**, como o VSCode (barra lateral primária, editor, barra lateral secundária, painel) e o JetBrains (âncoras de tool window).
- Cada zona é um **grupo de abas** (QTabBar + QStackedWidget). Arrastar uma aba move o painel para outra zona, uma zona vazia some e o painel pode virar janela flutuante.
- Com ~6–8 painéis conhecidos, o escopo é pequeno: splitters já existem, o "soltar aqui" são 4–5 áreas destacadas, a flutuante reaproveita o `AppWindowFrame` e o layout é salvo num JSON legível.
- Fica 100% no design system, sem dependência nova e alinhada ao "nativo e leve".

**Insight importante:** VSCode e JetBrains *parecem* totalmente livres, mas são
modelos **restritos** (zonas nomeadas + arrastar entre elas). É isso que os
torna previsíveis, e é o que serve ao Kai.

---

## 4. Arquitetura proposta (opção D)

Pasta nova: `src/ui/layout/`.

| Arquivo | Responsabilidade |
|---|---|
| `layout-model.{h,cpp}` | Dados puros, sem widgets: zonas → lista ordenada de ids de painel, aba ativa por zona, tamanhos (proporções) dos splitters, visibilidade/colapso de zona, painéis flutuantes (geometria, sempre no topo), painéis fechados. Serialização JSON, **validação/reparo** (ids desconhecidos, duplicados, zona inválida, nenhum painel), **migração** de `outputPosition` + `outputSplitterSizes`. Totalmente testável. |
| `view-registry.{h,cpp}` | Registro de painéis: id estável (`commands`, `output`, `document`, `ai_chat`, `processes`, `history`, `notifications`, `variables`, `kip:<commandId>`, ...), chave i18n do título, ícone Lucide, se pode fechar, se é único ou múltiplo, fábrica sob demanda. |
| `dock-host.{h,cpp}` | Widget raiz que monta as zonas com QSplitter (esquerda \| [centro / baixo] \| direita), aplica o modelo e emite mudanças para salvar (com debounce, como já é feito com a proporção). |
| `view-zone.{h,cpp}` | Grupo de abas de uma zona. Origem do arrasto (QDrag ao puxar a aba), menu da aba (fechar, mover para..., flutuar, maximizar). |
| `drop-overlay.{h,cpp}` | Sobreposição durante o arrasto que destaca a zona e o lado de destino, desenhada com tokens. |
| `floating-view-window.{h,cpp}` | Janela flutuante de um painel, baseada no `AppWindowFrame`. |

### Regras
- **Painéis são criados uma vez e reparentados** (padrão já validado em `applyOutputPosition`). Mover não recria nada.
- O `MainWindow` deixa de cuidar de splitter, proporção e posição da Saída: só registra os painéis e reage a sinais. Isso ajuda a quebrar o arquivo gigante.
- O `TerminalDrawer` perde o conceito de `DrawerPosition`: o eixo do colapso passa a vir da zona em que ele está.
- O modo foco vira o caso particular de **maximizar painel**.
- A tela de boas-vindas vira um estado do centro/host, e não "esconder o splitter".
- Os grupos de ação **ficam como estão** na fase 1: são barras ligadas ao painel Comandos, não painéis. Uma revisão possível depois é transformá-los em barra do próprio painel.
- Configurações → Layout: o combo "Posição da Saída" sai (ou vira "Restaurar layout padrão" e "Perspectiva").
- Persistência: arquivo próprio (ex.: `layout.json`) ou bloco em `settings.json`. Leitura/escrita assíncrona, conforme as regras de I/O do projeto.

### Formato sugerido (rascunho)
```json
{
  "version": 1,
  "zones": {
    "left":   { "views": ["commands"], "active": "commands", "size": 0.28, "collapsed": false },
    "center": { "views": ["output"], "active": "output" },
    "right":  { "views": [], "size": 0.25 },
    "bottom": { "views": ["document"], "size": 0.35 }
  },
  "floating": [
    { "view": "kip:abc123", "geometry": [100, 100, 420, 600], "alwaysOnTop": true }
  ],
  "closed": ["history"],
  "maximized": null
}
```

### Testes (Qt Test, escritos junto)
- `LayoutModel`: ida e volta pelo JSON; reparo de JSON corrompido, id desconhecido, painel duplicado e "nenhum painel"; migração de `outputPosition` bottom/left/right + `outputSplitterSizes` para um layout equivalente.
- Invariante: sempre existe um jeito de restaurar Comandos/Saída; "Restaurar layout" devolve o padrão exato.
- UI: mover a Saída entre zonas **não perde** a execução em andamento nem as abas de execução (as conexões do TerminalDrawer seguem vivas).
- UI: fechar um painel e reabrir pelo menu Exibir devolve-o ao último lugar.
- Flutuante: esconder a janela principal esconde as flutuantes; elas não reaparecem sozinhas.

---

## 5. Painéis candidatos

| Painel | Hoje | Vira | Zona padrão |
|---|---|---|---|
| Comandos (árvore + ações) | fixo | painel | esquerda (ou centro no layout atual — ver §7) |
| Saída (abas por execução) | fixo, posição por combo | painel | baixo |
| Documento/Nota | dentro da Saída | painel próprio: ler o README **enquanto** o comando roda | centro/direita (abre ao pedir) |
| Chat de IA | `QDialog` | painel lateral, estilo chat do VSCode | direita |
| Processos em background | diálogo | painel ao vivo | baixo/direita |
| Histórico de execuções | diálogo | painel | baixo |
| Notificações | diálogo | painel | direita |
| Variáveis resolvidas (o inspetor dinâmico já existe) | dentro do editor | painel "o que o ambiente ativo resolve agora" | direita |
| Comando KIP fixado | tela dentro da Saída / janela destacada | painel `kip:<id>` | qualquer |

Os diálogos de **edição** (editor de comando, ambientes, configurações)
continuam diálogos: são tarefas pontuais, não painéis de acompanhamento.

---

## 6. Funcionalidades

1. **Execução vira painel / logs lado a lado:** arrastar uma aba da Saída para outra zona deixa dois logs visíveis ao mesmo tempo (ex.: backend + frontend). Generaliza o "destacar" atual.
2. **Comando KIP fixado como painel:** um comando KIP vira um mini-app permanente na janela (dashboard de status, formulário de deploy). É o princípio 8 levado ao fim e a funcionalidade mais "Kai" da lista.
3. **Perspectivas/presets:** "Compacto" (só comandos), "Runner" (comandos + saída), "HTTP" (comandos + resposta à direita), "Leitura" (docs grande), além das salvas pelo usuário. Troca por atalho ou pela busca fuzzy. Associar perspectiva a um projeto é opcional e desligado por padrão (princípio 7).
4. **Maximizar painel:** duplo clique na aba ou atalho põe o painel em tela cheia; repetir volta. Substitui o modo foco atual.
5. **Recolher para a lateral** (estilo JetBrains): uma zona recolhida vira uma faixa fina de ícones, e um clique abre o painel ao lado do conteúdo (empurrando-o, com divisória para redimensionar; feedback do uso real: por cima do conteúdo cobria outras zonas e não redimensionava). Bom para janela pequena.
6. **Flutuante "sempre no topo":** log ou KIP flutuando sobre a IDE enquanto se programa. Combina com um utilitário de bandeja.
7. **Teclado:** Alt+1..9 foca/alterna cada painel (como o JetBrains), Ctrl+Tab cicla entre painéis e há um atalho para "Restaurar layout". Todos remapeáveis pelo gerenciador de atalhos existente (`actionShortcutSpecs`).
8. **Fechar e restaurar:** X na aba; volta pelo menu **Exibir** da barra superior, pela busca fuzzy ou pelo atalho do painel, sempre para o último lugar onde estava.
9. **Menu da aba:** fechar, fechar outros, mover para (esquerda/direita/baixo/centro), flutuar, maximizar.

### Fora de escopo (por ora)
- Grade livre com N divisões arbitrárias (plano B: ADS).
- Layout por monitor.
- Exportar layout no YAML do projeto (layout é pessoal).

---

## 7. Decisões em aberto (perguntar antes de implementar)

1. **Onde fica a árvore de Comandos no padrão?** Hoje ela é a "área principal" (cima/centro) e a Saída fica embaixo. Mapear para centro=Comandos, baixo=Saída preserva o visual 100%. A outra opção, esquerda=Comandos e centro=Saída/Docs, é mais "IDE" e muda o padrão.
2. **O painel Comandos pode ser fechado?** Sugestão: pode (com maximizar/Restaurar à mão), mas nunca sobra uma janela vazia: estado vazio com o botão "Restaurar layout".
3. **Flutuantes ao esconder o Kai:** escondem todas (sugestão padrão) ou "sempre no topo" sobrevive ao esconder? O princípio 1 favorece esconder todas.
4. **Uma ou várias Saídas?** Uma Saída com abas que podem ser arrastadas para fora (cada execução arrastada vira painel próprio) ou várias instâncias do painel Saída.
5. **Arquivo separado (`layout.json`) ou bloco em `settings.json`?** O separado facilita "Restaurar layout" sem tocar nas outras configurações.

---

## 8. Riscos técnicos

| Risco | Detalhe | Mitigação |
|---|---|---|
| Regressão no `MainWindow` | Muitas regras supõem 2 regiões fixas | Fase 1 só reproduz o layout atual sobre o novo host, com testes de UI, antes de abrir a flexibilidade |
| Wayland | O app **não pode posicionar** janelas de topo: restaurar posição de flutuante não é confiável | Flutuantes são extra, não base; no Wayland, restaurar só o tamanho |
| WSLg | Arrastar janela sem moldura e DnD já são instáveis | Validar no protótipo (Fase 0); arrasto entre zonas é interno à janela (QDrag dentro do app costuma funcionar) |
| QSS e tokens de raio | Abas/overlay novos precisam de raio por token; armadilha conhecida: raio > metade da altura fica quadrado | Usar `radiusMdForHeight`; testar dentro do MainWindow (memória `reference_qss_radius_square_corner`) |
| Fundos pretos | O QSS global pinta QWidget puro de escuro | Hosts transparentes (`makeTransparentHost`) nos containers de zona |
| Conflito de branch | A implementação mexe pesado em `main-window.cpp`, o mesmo arquivo da `feat/ai-assistant` | Implementar em **branch separada** (ex.: `feat/flex-layout`) **depois** do merge da IA |
| Painel KIP sem comando | Comando apagado/renomeado com painel `kip:<id>` fixado | Reparo do modelo descarta o painel e registra no log |

---

## 9. Fases e estimativas (aproximadas)

| Fase | Escopo | Estimativa |
|---|---|---|
| **0 — Protótipo** | Host de zonas com Comandos + Saída + Docs, arrastar entre zonas, salvar/restaurar. **Validar no WSLg e no Windows** antes de seguir. Decidir D vs. B aqui. | 2–3 dias |
| **1 — Base** | `LayoutModel` + testes, `dock-host`, `view-zone`, `drop-overlay`, registro de painéis, migração de `outputPosition`/`outputSplitterSizes`, fechar/restaurar (menu Exibir), "Restaurar layout". **Padrão idêntico ao atual.** Remover o combo de posição da Saída. Modo foco → maximizar. | 1–1,5 semana |
| **2 — Promover painéis** | Docs/Notas, Chat de IA, Processos, Histórico, Notificações, Variáveis resolvidas. | ~1 semana |
| **3 — Conforto** | Flutuantes (`AppWindowFrame`), sempre no topo, perspectivas, recolher para a lateral, atalhos Alt+N / Ctrl+Tab, menu da aba. | 1–2 semanas |
| **4 — Diferenciais** | Execuções lado a lado (arrastar aba de execução para fora) e comando KIP fixado como painel. | ~1 semana |

Em cada fase: i18n em `en.json` + `pt.json` (rodar `test_i18n_sync`), tokens
de raio, render offscreen antes de reportar e checklist manual na entrega
(AGENTS.md §7–§10). Atualizar `docs/` (output.md, settings.md, shortcuts.md)
e a ajuda no app, e `docs/philosophy.md` com a linha da §2.

---

## 10. Próximo passo sugerido

1. Responder às decisões em aberto da §7.
2. Criar `feat/flex-layout` a partir da `main` depois do merge da `feat/ai-assistant`.
3. Fazer o protótipo da Fase 0 e validar no WSLg e no Windows.
