# Correções — Autosync de Comandos por Projeto + Cron Scheduler (v3)

> Documento de CORREÇÃO sobre a implementação já feita pelo Haiku a partir de
> `specs/planning/kai-autosync-cron-analysis.md` (v2). Este documento **não
> substitui** o v2 — as decisões de design ali continuam valendo. Este
> documento aponta **o que foi implementado errado, incompleto ou
> simplesmente não implementado**, com referência exata a arquivo/linha do
> código atual (working tree, não commitado), e o que fazer para corrigir.
>
> Metodologia: cada item abaixo foi **verificado lendo o código real**
> (`git status`/`git diff` + leitura de arquivo), não é suposição. Onde o
> problema é reproduzível (ex: ícone que não existe no pool), isso está
> confirmado via grep/find no filesystem.

---

## 0. Resumo executivo — por que "ficou uma merda"

A implementação cobriu a **casca visual** (widgets, campos, layout) mas
**pulou a peça central da feature**: a classe `ProjectSyncManager` — que
faz o discovery de arquivos, o watch do filesystem e a sincronização nas
duas direções — **nunca foi criada**. Não existe `project-sync-manager.h`
nem `.cpp` em lugar nenhum do repositório. Resultado: a tela de
configuração salva dados, mas **nada acontece com eles** — não há
varredura de diretório, não há escrita de `kai.json`/`kai.yml`, não há
leitura de mudanças externas. Isso explica 100% o "botei uma pasta e ele
não achou nada nela".

Além disso, a tela de "Diretórios de busca" (chamada de `AutosyncTab` no
código, a spec v2 chamava de `search-directories-tab`) não seguiu o padrão
pedido explicitamente — tabela 100% editável em vez de somente-leitura +
formulário —, tem um bug real que zera o campo mais importante da feature
(`kaiFolderId`), usa um nome de ícone que não existe no projeto, não liga
os campos condicionais ao master switch, e o campo CRON no editor de
comando foi posicionado na seção errada com estilo hardcoded.

**Prioridade de correção, na ordem que resolve os sintomas relatados:**
1. Implementar `ProjectSyncManager` de verdade (resolve "discovery não funciona").
2. Reescrever `AutosyncTab` seguindo o padrão `ParameterEditorWidget` (resolve "tabela devia ter só nome + form editar").
3. Corrigir o formulário para usar `FolderPickerWidget` de verdade (resolve "tem que usar o seletor de pastas no form").
4. Trocar o ícone da aba (resolve "tinha que ter ícone").
5. Ligar os campos condicionais ao master switch (resolve "só ligar se o de sync em si tiver ligado").
6. Mover e restilizar o campo CRON (resolve "ficou feio, não segue o padrão").
7. (Bônus, não relatado mas encontrado) Completar o `FolderPickerWidget` V2 (pills coloridas + busca), que a spec v2 já pedia e não foi feito.

---

## 1. BUG CRÍTICO: `ProjectSyncManager` nunca foi implementado

**Evidência:**
```
$ grep -rln "ProjectSyncManager\|project-sync-manager" src tests
src/core/models.h   # só a struct SearchDirectory, nada de lógica
```
Nenhum arquivo `project-sync-manager.h`/`.cpp` existe. `CMakeLists.txt` não
referencia nada parecido. `MainWindow` não instancia nada parecido
(comparar com `m_cronScheduler`, que ESTÁ instanciado em
`main-window.h:361` e conectado em `main-window.cpp:188` — o cron foi
integrado corretamente, o sync não foi integrado de forma nenhuma).

**O que existe hoje (só a casca):**
- `core::SearchDirectory` (struct de dados) — OK, existe.
- `SettingsData::autoSyncEnabled/autoSyncFromFileEnabled/autoSyncFromKaiEnabled/searchDirectories` — OK, existe.
- Persistência em `settings.json` — presumivelmente OK (não verificado a fundo, mas o padrão dos outros campos de `SettingsData` foi seguido).
- **Nada** que leia esses diretórios, varra o filesystem, ou escreva/leia um `kai.json`/`kai.yml`.

**O que fazer:**
Implementar a classe exatamente como especificado no documento v2, seção
1.4 (`ProjectSyncManager`) — não é uma reinterpretação, é a mesma spec que
não foi seguida da primeira vez. Destaco os pontos que são fáceis de
esquecer de novo:

1. **`rescan()` precisa de fato encontrar o `kai.json`/`kai.yml`** sob
   `Folder::workingDir` (ou `projectPath` como fallback) de cada pasta com
   `isProject == true`, **e também** sob `SearchDirectory::externalPath`
   de cada diretório de busca cadastrado — os dois mecanismos alimentam o
   watcher (ver v2 §1.2/§1.3). Teste manual mínimo pra validar: criar uma
   pasta de teste com um `kai.json` válido dentro, apontar
   `SearchDirectory::externalPath` pra ela, reiniciar o Kai (ou disparar
   `rescan()`), e confirmar que o conteúdo aparece importado na árvore.
   **Esse teste manual específico é o que falhou pro usuário** — ele
   "botou uma pasta e não achou nada" — então é o critério de aceite
   mínimo antes de considerar a etapa concluída.
2. `QFileSystemWatcher` real, escutando o diretório (não só o arquivo —
   ver v2 §1.3 sobre troca de inode).
3. Hash de conteúdo persistido (`project-sync-state.json`) — sem isso,
   todo boot reprocessa tudo do zero.
4. Sinais (`externalChangeApplied`, `syncConflictDetected`,
   `syncPausedChanged`) conectados no `MainWindow` de forma real, não só
   declarados.
5. As duas ações manuais (`syncKaiToFileManually`/`syncFileToKaiManually`)
   — mesmo que o menu de contexto (v2 §1.5) ainda não esteja pronto, os
   métodos públicos precisam existir e funcionar, porque são o único jeito
   de testar a direção KAI→arquivo manualmente sem esperar um save
   automático.

**Critério de aceite desta etapa:** um teste em
`tests/test_project_sync_manager.cpp` que (a) cria um diretório temporário
com um `kai.json` de exemplo, (b) chama `rescan()`, (c) verifica que um
`Folder`/`Command` correspondente aparece no `CommandsData` resultante.
Sem esse teste passando, a feature não está implementada — é só o que o
usuário testou manualmente e falhou.

---

## 2. Tela "Diretórios de busca" não segue o padrão pedido (tabela + form)

**Evidência:** `src/ui/features/settings/tabs/autosync-tab.cpp:73-110`.

O código cria uma `QTableWidget` de **3 colunas totalmente editáveis
inline** (Nome, Pasta, Pasta KAI) — cada célula é um `QTableWidgetItem`
com texto livre, sem nenhuma restrição de edição
(`setEditTriggers` não é sequer chamado, então o Qt usa o default
`AllEditTriggers`).

**Padrão pedido (spec v2 §1.6, texto literal): "mesmo padrão visual e de
interação da adição de parâmetros"** — isto é, o padrão de
`ParameterEditorWidget` (`src/ui/features/command-editor/parameter-editor-widget.h/.cpp`),
que é bem documentado no próprio código-fonte
(`parameter-editor-widget.cpp:13-24`):

> "A tabela é SOMENTE-LEITURA — ela apenas EXIBE os parâmetros. A edição
> de uma linha é feita por um FORMULÁRIO CONTEXTUAL (...) acionado pelo
> ícone de lápis INLINE na própria linha (...) ou por duplo-clique na
> linha; o de lixeira remove."

**O que fazer — reescrever `AutosyncTab`/tabela de diretórios seguindo
literalmente esse padrão:**

1. Tabela com **3 colunas visuais**: drag-handle (opcional, diretórios de
   busca provavelmente não precisam de reorder manual — pode omitir essa
   coluna se não fizer sentido) + **Nome** (só o nome, texto puro, igual
   ao `kColName` do parameter-editor) + **Ações** (lápis + lixeira
   inline, usando `LucideIcons`, mesmo padrão de
   `table-utils.h`/`makeDragHandleCell`/ícones de ação já usados em
   `parameter-editor-widget.cpp`).
2. **Nenhuma célula de texto livre além do nome.** Path externo e pasta
   Kai NÃO aparecem como colunas de texto — só existem dentro do
   formulário.
3. Criar `SearchDirectoryRowDialog` (novo — mesmo espírito de
   `ParameterRowDialog`, se esse nome de classe existir em
   `parameter-editor-widget.cpp`; conferir e replicar a convenção de
   nome), um `QDialog` com:
   - Campo "Nome" (`QLineEdit`, opcional).
   - Campo "Pasta" (path externo) — `QLineEdit` + botão "..." →
     `QFileDialog::getExistingDirectory` (like já existe em
     `onAddDirectory` hoje, só que dentro do formulário, não solto).
   - Campo "Pasta KAI" — **`FolderPickerWidget`** (ver §3 abaixo — hoje
     não é usado aqui, é o bug mais grave desta tela).
4. Duplo-clique na linha ou o lápis abre o formulário pré-preenchido.
   Lixeira remove a linha direto (sem formulário), com o mesmo cuidado
   de confirmação que outras listas do Kai já têm (se
   `parameter-editor-widget` confirma antes de remover, replicar; se não,
   não inventar uma confirmação nova).
5. Fonte de verdade é `m_searchDirectories` (a lista em memória), a
   tabela é **derivada** dela — mesmo comentário presente em
   `parameter-editor-widget.h:26` ("A fonte de verdade é m_params (...);
   a tabela é derivada"). Hoje `AutosyncTab::searchDirectories()`
   (linha 128-145) faz o oposto: **lê de volta da tabela**, célula por
   célula — isso é exatamente o padrão que o comentário do
   `parameter-editor-widget.cpp:43-47` documenta como o jeito ANTIGO e
   errado que foi corrigido lá. Não repetir o erro aqui.

---

## 3. Bug: `kaiFolderId` nunca é preenchido + FolderPickerWidget não usado

**Evidência:** `src/ui/features/settings/tabs/autosync-tab.cpp:147-160`
(`onAddDirectory`):
```cpp
void AutosyncTab::onAddDirectory()
{
    QString dirPath = QFileDialog::getExistingDirectory(this, ...);
    if (!dirPath.isEmpty()) {
        int row = m_searchDirsTable->rowCount();
        m_searchDirsTable->insertRow(row);
        m_searchDirsTable->setItem(row, 0, new QTableWidgetItem(dirPath));  // Nome = path externo (!)
        m_searchDirsTable->setItem(row, 1, new QTableWidgetItem(dirPath));  // Pasta = path externo
        m_searchDirsTable->setItem(row, 2, new QTableWidgetItem(QString())); // Pasta KAI = SEMPRE VAZIO
        m_searchDirsTable->selectRow(row);
    }
}
```
`kaiFolderId` nunca é setado — fica sempre `QString()`. Mesmo que o
`ProjectSyncManager` estivesse implementado (§1), ele nunca saberia pra
qual `Folder` do Kai mapear aquele diretório externo, porque o dado nunca
existiu. Este é provavelmente o bug mais direto por trás de "botei uma
pasta e ele não achou nada".

**O que fazer:** já coberto pela reescrita da §2 — o formulário
(`SearchDirectoryRowDialog`) usa um `FolderPickerWidget`
(`src/ui/shared/folder-picker-widget.h`, já existe e compila, só não está
sendo usado aqui) para escolher a pasta Kai, exatamente como já é feito em
`folder-editor-dialog.cpp:159`, `collection-editor-dialog.cpp:107`,
`command-editor-dialog.cpp:262`. Não é preciso inventar nada novo — é
literalmente copiar o padrão de uso já presente nesses 3 arquivos.

`FolderPickerWidget` precisa ser alimentado com `setFolders(allFolders)`
(o `AutosyncTab`/`SearchDirectoryRowDialog` precisa receber a lista de
pastas do app — hoje `AutosyncTab` nem recebe isso no construtor,
conferir `AutosyncTab::AutosyncTab(const core::SettingsData &currentSettings, ...)`
em `autosync-tab.h:20` — falta um parâmetro `const QVector<core::Folder> &allFolders`
e repassar isso do `SettingsDialog` que já tem essa lista em mãos via
`commandsData`).

---

## 4. Ícone da aba não existe no pool — renderiza vazio

**Evidência:**
```cpp
// settings-dialog.cpp:104-105
{utils::tr(QStringLiteral("settings.group.autosync")), QStringLiteral("sync"),
 wrapPage(m_autosyncTab)},
```
```
$ find assets/icons/lucide -iname "sync*"
(nada)
$ find assets/icons/lucide -iname "refresh*"
assets/icons/lucide/refresh-cw.svg
assets/icons/lucide/refresh-ccw.svg
```
`LucideIcons::icon("sync", ...)` → `resourcePathFor("sync")` →
`:/icons/lucide/sync.svg` → **arquivo não existe no `.qrc`** →
`QFile::open` falha → `recoloredSvg` retorna bytes vazios → o
`QListWidgetItem` da aba recebe um `QIcon` vazio. Isso bate exatamente com
a reclamação "a aba tinha que ter um ícone" (ela tinha o CÓDIGO pra ter
ícone, só que apontando pra um arquivo inexistente).

**O que fazer:** trocar `QStringLiteral("sync")` por
`QStringLiteral("refresh-cw")` em `settings-dialog.cpp` (nome de ícone que
de fato existe em `assets/icons/lucide/refresh-cw.svg`). Validar
visualmente rodando o app — não só confiar que o nome "parece certo",
porque foi exatamente esse tipo de erro que causou o bug original. Se
quiser, `git grep -o '"[a-z0-9-]*"' assets/icons/icons.qrc` ou
`ls assets/icons/lucide/` lista todos os nomes válidos antes de escolher
qualquer ícone novo em qualquer feature futura.

---

## 5. Campos condicionais não têm lógica de habilitar/desabilitar

**Evidência:** `src/ui/features/settings/tabs/autosync-tab.cpp:52-64` —
`m_autoSyncFromFileField` e `m_autoSyncFromKaiField` são criados e
populados com o valor salvo, mas **nunca há um `connect` entre
`m_autoSyncEnabledField` e os outros dois**, nem uma chamada inicial de
`setEnabled()` baseada no estado do master switch. Os 3 checkboxes ficam
sempre clicáveis, independente uns dos outros.

Isso contradiz a spec v2 (§0.3, decisão confirmada com o usuário):
> "autoSyncFromFileEnabled/autoSyncFromKaiEnabled só têm efeito com
> autoSyncEnabled == true" — e o próprio comentário que o Haiku escreveu
> em `config-manager.h:325-326` REPETE essa regra, mas ela nunca foi
> implementada na UI que lê/escreve esse campo.

**O que fazer**, em `AutosyncTab` (construtor, logo após criar os 3
checkboxes):
```cpp
auto updateDirectionFieldsEnabled = [this]() {
    const bool enabled = m_autoSyncEnabledField->isChecked();
    m_autoSyncFromFileField->setEnabled(enabled);
    m_autoSyncFromKaiField->setEnabled(enabled);
};
connect(m_autoSyncEnabledField, &QCheckBox::toggled, this, [updateDirectionFieldsEnabled](bool) {
    updateDirectionFieldsEnabled();
});
updateDirectionFieldsEnabled(); // estado inicial, respeitando o valor salvo
```
Também considerar (UX, não bloqueante): quando desabilitados, os
checkboxes de direção devem manter seu valor salvo em memória (não
resetar pra `false` só porque foram desabilitados) — `setEnabled(false)`
sozinho já garante isso no Qt (não altera `isChecked()`), então não é
preciso lógica extra além do `setEnabled`.

---

## 6. Campo CRON: seção errada + estilo hardcoded (não segue o padrão da UI)

**Evidência:** `src/ui/features/command-editor/command-editor-dialog.cpp:1227-1264`.

### 6.1 Seção errada
O campo é adicionado dentro de `windowBody`, que vira a seção
`"command_editor.advanced_settings.section.window"` → traduzido como
**"Janela e Auto-run"** (`assets/i18n/pt.json:936`). Cron não tem nada a
ver com comportamento de janela — foi encaixado ali só porque o
`autoRunField` (que é sim sobre boot) já estava naquele bloco de código, e
o Haiku colou o cron logo depois sem criar uma seção própria.

**O que fazer:** criar uma seção dedicada, no mesmo padrão de
`makeFlagsSection(...)` usado para as outras (`execution`, `integration`,
`window`) — ex: `command_editor.advanced_settings.section.scheduling`
("Agendamento"), com os campos `cronExpression`/`cronNotifyOnRun` MOVIDOS
pra dentro dela, fora de `windowBody`.

### 6.2 Estilo hardcoded (viola AGENTS.md §9 em espírito, e é inconsistente com o resto do MESMO arquivo)
```cpp
m_cronExpressionHintLabel->setStyleSheet(
    QStringLiteral("color: %1; font-size: 11px;").arg(utils::tokens::mutedFg()));
...
m_cronExpressionHintLabel->setStyleSheet(
    QStringLiteral("color: %1; font-size: 11px;").arg(utils::tokens::errorFg()));
```
`font-size: 11px` é um valor hardcoded — deveria vir de
`utils::tokens::fontSizeSmallPt()` (existe, ver
`src/utils/design-tokens.h:109`, é literalmente feito pra "legendas/
metadados", exatamente o caso de uso de um hint). Pior: **o mesmo diff**
(`autosync-tab.cpp:41-42`) usa corretamente
`layout_helpers::makeHintBanner(parent, text)` pra um hint equivalente —
ou seja, o padrão certo foi usado numa tela e ignorado na outra, no mesmo
lote de mudanças. Isso é o "não segue o padrão atual da UI" que o usuário
sentiu.

**O que fazer:**
- Preferencialmente, trocar o `QLabel` cru por
  `layout_helpers::makeHintBanner(windowBody /* ou o novo container de
  scheduling */, QString())` e atualizar seu texto via `setText()` nos
  dois branches (válido/inválido) — mas `makeHintBanner` provavelmente
  não expõe variação de cor (sucesso/erro); conferir a assinatura em
  `dialog-utils.h:325` antes de decidir. Se `makeHintBanner` for
  estritamente um banner neutro (sem suporte a cor de erro), manter o
  `QLabel` dedicado é aceitável, MAS o tamanho de fonte tem que vir de
  `tokens::fontSizeSmallPt()`, nunca `11px` literal:
  ```cpp
  QFont hintFont = m_cronExpressionHintLabel->font();
  hintFont.setPointSize(utils::tokens::fontSizeSmallPt());
  m_cronExpressionHintLabel->setFont(hintFont);
  m_cronExpressionHintLabel->setStyleSheet(
      QStringLiteral("color: %1;").arg(utils::tokens::mutedFg())); // só a cor, sem font-size
  ```
- Dar uma largura mínima/`sizePolicy` ao `m_cronExpressionField`
  (`QLineEdit`) coerente com os outros campos de texto da mesma tela —
  hoje ele divide a `cronRow` só com um checkbox e um `addStretch()`, sem
  nenhum controle de largura, o que pode deixá-lo desproporcional
  comparado a como o resto do editor distribui campos (comparar com como
  outros `QLineEdit`s do mesmo arquivo definem largura, se definem).

---

## 7. Bônus (não relatado pelo usuário, mas achado ao verificar `FolderPickerWidget`): V2 visual incompleto

**Evidência:** `src/ui/shared/folder-picker-widget.cpp` inteiro — é um
`QComboBox` populado com texto simples (`formatFolderPath` monta
`"A › B › C"` como STRING única por item), sem:
- Delegate customizado / pills coloridas por profundidade (pedido
  explícito da spec v2 §1.7: "cor de fundo = tom do accent variando por
  profundidade").
- Nenhum uso de `utils::tokens::radiusSm()` em lugar nenhum do arquivo.
- Nenhuma capacidade de busca/filtro (a spec v2 pedia melhorar o
  `QCompleter` existente; aqui não há `QCompleter` nenhum, é um combobox
  comum).
- Não usa `foldersInTreeOrder`/`capComboBoxWidth` de
  `dialog-utils.h`, que já existiam e resolvem parte disso.

**Isso não bloqueia as correções 1-6 acima** (o widget É funcional, só
não tem o refinamento visual pedido) — mas registro aqui porque é
provável que o usuário reclame disso na próxima rodada se não for
corrigido agora, já que é o mesmo padrão de "a spec pedia X, foi entregue
uma versão simplificada sem avisar". Se o tempo permitir na mesma leva de
correções, complementar; senão, pelo menos não regredir mais.

---

## 8. Gap: falta o campo visual de "Diretório de Trabalho" no editor de pasta

**Evidência:** `Folder::workingDir` existe no modelo
(`src/core/models.h:620`) e já tem resolução hierárquica FUNCIONANDO em
`ExecutionPipeline::effectiveWorkingDir`
(`src/engine/execution-pipeline.cpp:339-367` — sobe a cadeia comando →
pasta → pasta pai até achar um valor não-vazio, exatamente como a spec v2
pedia). **Mas não existe nenhum campo de UI** em
`src/ui/features/collections/folder-editor-dialog.cpp` que exiba ou edite
esse valor — busca por `workingDir`/`projectPath` no arquivo inteiro não
retorna nada. O usuário não tem como setar esse campo pela interface;
só existiria via edição manual do `commands.json` ou JSON avançado.

**Padrão de referência já existente no mesmo arquivo** (usar como modelo):
o campo de perfil de terminal (`m_profileField`, linhas 173-181 e
365-388) resolve o mesmo problema de herança hierárquica pra
`terminalTarget` com um combo "Herdar do pai" / "Local" / lista de
perfis. `workingDir` é mais simples — é só um path livre, não uma lista
fixa de opções — então **não precisa de um sentinel tipo `@parent`**: uma
string vazia já significa "herda automaticamente" (confirmado no trecho
do `effectiveWorkingDir` acima). O padrão de UI correto aqui é o mesmo já
usado para pastas/paths em outros lugares do editor: `QLineEdit` + botão
"..." (`QFileDialog::getExistingDirectory`), com placeholder indicando
que vazio = herda da pasta pai.

**O que fazer**, em `folder-editor-dialog.cpp`, dentro do `identityGrid`
(mesmo card "Geral" onde já estão nome/ícone/pasta-pai/ordem/perfil de
terminal/marcar como projeto/cli path):

1. Novo campo `m_workingDirField` (`QLineEdit`) + botão "..." ao lado
   (mesmo padrão do botão de pasta usado em `onAddDirectory` da correção
   §3, ou de qualquer outro seletor de diretório já existente no app —
   conferir se há um helper compartilhado tipo `makeDirectoryPickerRow`
   em `dialog-utils.h` antes de duplicar o botão + `QFileDialog` na mão).
2. Placeholder/hint (`layout_helpers::makeHintBanner` ou tooltip, seguir
   o padrão já usado por outros campos deste MESMO arquivo, ex:
   `m_cliPathField->setToolTip(...)` linha 200) explicando: "Vazio =
   herda o diretório de trabalho da pasta pai. Usado como cwd padrão dos
   comandos desta pasta e como raiz do Autosync."
3. Popular ao editar: `m_workingDirField->setText(existingFolder->workingDir)`
   junto dos outros campos (perto da linha 216-219).
4. Gravar ao salvar: `folder.workingDir = m_workingDirField->text().trimmed();`
   junto de onde `folder.terminalTarget`/`folder.cliPath` já são
   montados (perto da linha 410-414).
5. i18n: `folder.field.working_dir` (label) +
   `folder.field.working_dir.placeholder`/`.tip` — adicionar em
   `en.json` e `pt.json`, rodar `test_i18n_sync`.
6. Posicionamento sugerido no grid: logo abaixo do campo de Perfil de
   Terminal (linha ~180) e acima de "Marcar como projeto" — mantém
   agrupados os campos de comportamento de execução da pasta antes do
   campo de escopo/fronteira (`isProject`).

**Por que isso importa tanto quanto o resto:** sem esse campo, mesmo que
o `ProjectSyncManager` (§1) seja implementado corretamente, o usuário não
tem como apontar QUAL diretório do filesystem uma pasta-projeto
representa — a feature de discovery fica sem entrada manual possível,
dependendo 100% de diretórios de busca cadastrados separadamente (tela
de §2), o que não é o que a spec v2 desenhou (`workingDir` era pra ser o
mecanismo primário; diretórios de busca são um mecanismo complementar
para pastas externas não importadas ainda).

---

## 9. Checklist de aceite (o que rodar antes de dizer "pronto" de novo)

- [ ] Criar uma pasta de teste com `kai.json` válido, cadastrar como
      diretório de busca, confirmar que o conteúdo é importado (teste
      MANUAL, é o que falhou da última vez — não aceitar "os testes
      unitários passam" como substituto disso).
- [ ] `tests/test_project_sync_manager.cpp` existe e cobre pelo menos
      discovery básico (criar temp dir com kai.json → rescan → aparece em
      CommandsData).
- [ ] Tela de Diretórios de busca: tabela só mostra Nome + Ações; abrir
      form (lápis ou duplo-clique) mostra Nome/Pasta/Pasta KAI; Pasta KAI
      usa `FolderPickerWidget` de verdade (dropdown com pastas reais do
      Kai, não texto livre).
- [ ] Adicionar um diretório novo pelo formulário e confirmar que
      `SearchDirectory::kaiFolderId` salvo em `settings.json` não fica
      vazio.
- [ ] Abrir Configurações → validar visualmente que a aba de Autosync tem
      um ícone visível (não um espaço em branco).
- [ ] Desligar o master switch "Sincronização automática" → os 2 campos
      de direção ficam visualmente desabilitados (cinza/não clicáveis).
- [ ] Abrir o editor de um comando Shell → o campo CRON aparece numa
      seção própria (não dentro de "Janela e Auto-run") e o hint abaixo
      dele usa a mesma tipografia/cor do resto da UI, sem tamanho de
      fonte fora do padrão.
- [ ] Abrir o editor de uma pasta/projeto → existe um campo "Diretório de
      Trabalho" visível, editável, com botão de escolher pasta; salvar e
      reabrir preserva o valor; deixar vazio numa subpasta e confirmar
      que ela herda o valor da pasta pai (via effectiveWorkingDir).
- [ ] `./build.sh` limpo com `-Wall -Wextra`.
- [ ] `tests/test_i18n_sync.cpp` passando (qualquer chave nova de
      `scheduling`/`SearchDirectoryRowDialog` precisa estar em en.json e
      pt.json).
