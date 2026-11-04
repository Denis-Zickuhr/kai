# UI feature reorganization plan

## Objetivo

`src/ui/` hoje tem ~90 arquivos soltos numa única pasta (mais `main-window.cpp`
com 5353 linhas). O padrão já iniciado em `src/ui/features/settings/`
(`settings-dialog.h/cpp`) deve ser generalizado: cada feature ganha sua
própria pasta em `src/ui/features/<feature>/`, e dentro dela, quando o
arquivo principal for grande, quebrar em subpasta `tabs/` (ou `widgets/`,
`dialogs/`) com uma classe por arquivo.

`src/core/` e `src/utils/` já estão razoavelmente bem separados (lógica de
domínio vs. utilitários genéricos) e não são o foco deste plano — o problema
está concentrado em `src/ui/`.

## Por que isso é seguro de automatizar

- O CMakeLists.txt do projeto **lista arquivos explicitamente** (sem
  `file(GLOB ...)`), em `CMakeLists.txt` (raiz), alvos `kai-core`,
  `kai-engine`, `kai-ui`, `kai-cli`. Isso significa que qualquer mover de
  arquivo tem que atualizar o caminho correspondente nessa lista — é
  mecânico, mas obrigatório.
- Todos os `#include` internos já usam caminho com prefixo de pasta
  (`"ui/xxx.h"`, `"core/xxx.h"`, `"utils/xxx.h"`), então mover um arquivo
  para `ui/features/<nome>/xxx.h` só exige atualizar o prefixo dos includes
  que o referenciam — não há includes relativos "cegos".
- **Use as refactor tools do CLion/JetBrains ("Move Class/File")** em vez de
  mover manualmente com `mv` + sed. O CLion entende `#include` e
  `CMakeLists.txt` como referências reais e atualiza ambos ao mover um
  arquivo dentro do IDE. Isso elimina a classe de erro mais comum
  (esquecer de atualizar um include ou a lista do CMake).

## Ordem de execução

Uma feature por PR, nesta ordem (das mais isoladas para as mais acopladas
ao `main-window`):

1. **`features/output/`** — pouco acoplamento, fácil de validar
   - `output-panel.h/cpp`, `code-output-view.h/cpp`, `log-line-view.h/cpp`,
     `log-viewer-dialog.h/cpp`, `ansi-text-parser.h/cpp`,
     `pty-terminal-widget.h/cpp`, `terminal-drawer.h/cpp`,
     `terminal-profiles-editor-widget.h/cpp`

2. **`features/history/`**
   - `run-history-dialog.h/cpp`, `notification-history-dialog.h/cpp`,
     `process-list-dialog.h/cpp`

3. **`features/collections/`** (import/export/organização de coleções)
   - `collection-editor-dialog.h/cpp`, `collection-selector-dialog.h/cpp`,
     `folder-editor-dialog.h/cpp`, `folder-delete-dialog.cpp` (+ `.h` se
     existir), `import-dialog.h/cpp`, `import-selection-dialog.h/cpp`,
     `export-dialog.h/cpp`, `project-import-options-dialog.h/cpp`,
     `project-selector.h/cpp`, `project-detection-strategy.h/cpp`

4. **`features/environments/`**
   - `environment-manager-dialog.h/cpp`, `declared-env-vars-editor-widget.h/cpp`,
     `env-extractors-editor-widget.h/cpp`, `env-var-autocomplete.h/cpp`,
     `dynamic-vars-inspector-widget.h/cpp`

5. **`features/command-editor/`** — o maior cluster, mexe mais no
   `main-window.cpp`, fazer por último entre os "grandes"
   - `command-editor-dialog.h/cpp`, `command-json-editor-dialog.h/cpp`,
     `command-tree-widget.h/cpp`, `parameter-editor-widget.h/cpp`,
     `parameter-form-dialog.h/cpp`, `execution-conditions-editor-widget.h/cpp`,
     `hooks-editor-widget.h/cpp`, `output-responders-editor-widget.h/cpp`,
     `curl` afins ficam em `core/`, não mover
   - Dentro desta feature, depois de mover, considerar quebrar
     `command-editor-dialog.cpp` em `command-editor-dialog.cpp` (shell) +
     `tabs/` se ele também crescer como o settings

6. **`features/settings/`** — já existe a pasta; só falta quebrar o
   arquivo grande em tabs
   - Mover a lógica de `settings-dialog.cpp` (850 linhas) para
     `features/settings/tabs/appearance-tab.h/cpp`,
     `features/settings/tabs/general-tab.h/cpp`,
     `features/settings/tabs/shortcuts-tab.h/cpp` etc., mantendo
     `settings-dialog.h/cpp` como o shell que monta o `QTabWidget` e
     instancia cada tab
   - Primeiro identificar os grupos de código dentro do arquivo (grep por
     `void SettingsDialog::setup...Tab` ou comentários de seção) antes de
     cortar

7. **`ui/shared/`** (ou `ui/widgets/` — decidir nome antes de começar) —
   tudo que não pertence a uma feature específica, usado por várias:
   - `json-viewer-widget.h/cpp`, `foldable-json-view.h/cpp`,
     `json-editor-dialog.h/cpp`, `json-syntax-highlighter.h/cpp`,
     `key-value-editor-widget.h/cpp`, `table-utils.h/cpp`,
     `row-edit-dialog.h/cpp`, `date-picker-dialog.h/cpp`,
     `icon-picker-widget.h/cpp`, `icon-picker-dialog.h/cpp`,
     `lucide-icons.h/cpp`, `loading-overlay.h/cpp`, `fuzzy-search.h/cpp`,
     `collapsible-section-card.h/cpp`, `expand-collapse-bar.h/cpp`,
     `inline-code-field.h/cpp`, `shortcut-capture-field.h/cpp`,
     `shortcuts-manager-widget.h/cpp`, `storage-manager-widget.h/cpp`,
     `draggable-tree-widget.h`, `draggable-table-widget.h`,
     `no-scroll-combo-filter.h`, `name-uniqueness.h`, `dialog-utils.h`,
     `notification-gate.h`, `quick-body-editor-dialog.h/cpp`,
     `help-dialog.h/cpp`, `help-content.cpp`, `top-utility-bar.h/cpp`,
     `action-sidebar.h/cpp`, `action-group-container.h/cpp`,
     `welcome-screen.h/cpp`

8. **Ficam em `src/ui/` (raiz)** — não são features, são o "shell" do app:
   - `main-window.h/cpp`, `app-stylesheet.h/cpp`

## Como instruir o agente barato (por feature)

Para cada item da lista acima, o prompt para o agente deve conter:

1. A lista exata de arquivos daquela feature (copiar do bloco acima).
2. Instrução explícita: **use o refactor "Move" do CLion (ou
   `File > Refactor > Move`) arquivo por arquivo**, não `mv` manual — isso
   atualiza includes e `CMakeLists.txt` automaticamente. Se o agente não
   tiver acesso ao CLion (rodando via CLI puro), o fallback é: mover com
   `git mv`, depois `grep -rl '"ui/<nome-antigo>.h"' src/ tests/` e corrigir
   cada include manualmente, e por último editar `CMakeLists.txt` na raiz
   trocando o caminho antigo pelo novo.
3. Depois de mover todos os arquivos da feature: `cmake --build` (ou o
   comando de build do projeto) e rodar a suíte de testes relevante em
   `tests/` antes de commitar.
4. Um commit por feature, nunca uma pasta inteira num commit só (facilita
   reverter se quebrar).
5. Não tocar em `src/core/`, `src/engine/`, `src/cli/`, `src/utils/` neste
   plano — fora de escopo.

## Riscos / pontos de atenção

- `tests/CMakeLists.txt` provavelmente referencia alguns desses headers
  diretamente (ex: `test_command_editor_dialog`, `test_drag_reorder`,
  `test_output_panel_usability` etc. aparecem em `dist/dev/tests/`) — os
  includes desses testes também precisam ser atualizados junto com o mover
  do arquivo de produção correspondente.
- `main-window.cpp` inclui direto quase todas as features; cada PR de
  feature vai tocar em `main-window.cpp` só para ajustar os includes (não
  a lógica) — normal, não é motivo de escopo aumentar.
- Não criar abstrações novas (interfaces, namespaces, base classes) neste
  processo — é reorganização de arquivos/pastas, não refatoração de design.
