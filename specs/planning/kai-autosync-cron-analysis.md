# Análise técnica — Autosync de Comandos por Projeto + Módulo Cron Scheduler

> Documento de análise/planejamento para implementação no Kai (C++20 / Qt6).
> Baseado na leitura de `specs/01..10`, `src/core/models.h`,
> `src/core/config-manager.h`, `src/core/environment-manager.h`,
> `src/core/yaml-bridge.h`, `src/core/notification-history.h`,
> `src/engine/execution-pipeline.*`, `src/ui/main-window.cpp` (autorun),
> `src/ui/shared/dialog-utils.h` (folder picker atual) e
> `src/core/folder-path-resolver.h`.
>
> **v2** — atualizado após respostas do usuário às 4 perguntas em aberto
> da v1 (formato do arquivo, campo `workingDir`, semântica dos campos de
> sync, master switch de notificação). Ver changelog no final da seção 0.
>
> Todas as regras de `AGENTS.md` se aplicam integralmente: async-only
> (nada de I/O síncrono na GUI thread), sinais/slots para desacoplar
> core/engine de ui, `m_` em membros privados, kebab-case em arquivos,
> PascalCase em classes, i18n obrigatório via `utils::tr("chave.i18n")`
> com entradas em `en.json` **e** `pt.json`, radius sempre via
> `utils::tokens::radiusSm/Md/Lg`, teste escrito junto (Qt Test) e
> feature-by-feature.

---

## 0. Decisões de design (confirmadas com o usuário)

1. **`Folder::workingDir` é um campo novo.** Não existe hoje nenhum
   campo de diretório com lógica de herança hierárquica pronta pra
   reaproveitar 1:1 (`projectPath` é literal, sem herança entre
   pastas). O que É reaproveitado é o **mecanismo** de resolução
   hierárquica já usado por `Folder::terminalTarget`/
   `kInheritTerminalTarget` (do nível mais específico pro mais
   genérico) — `workingDir` implementa o mesmo padrão, como campo
   próprio.
2. **Formato do arquivo sincronizado — depende da origem:**
   - Projeto que **já tem** um `kai.json` (import clássico, formato
     JSON de sempre, spec 02 §3): a sincronização continua lendo/
     escrevendo **esse mesmo arquivo, em JSON** — sem quebrar
     retrocompat com quem já usa import manual.
   - Quando o **Kai precisa criar** um arquivo de sync novo (projeto/
     pasta que ainda não tinha nenhum arquivo-alvo): o arquivo criado é
     **YAML simplificado** (`kai.yml`), no formato enxuto (`lean=true`,
     mesmo estilo do `ConfigManager::exportSelective`/`exportFolder`
     visto nos exemplos de export) passado por `YamlBridge`. Ou seja:
     Kai nunca reescreve um `kai.json` existente como `.yml` por conta
     própria, mas todo arquivo **novo** que ele autogera nasce em YAML.
   - `ProjectSyncManager` precisa então resolver, por projeto, qual dos
     dois é o "arquivo-alvo" (checar se já existe `kai.json`; senão,
     criar/usar `kai.yml`) — ver §1.2/§1.7.
3. **Sincronização automática = 3 campos binários**, não modos de 3
   vias:
   - `autoSyncEnabled` — a feature em si (master switch).
   - `autoSyncFromFileEnabled` — "Mudanças de arquivos de configuração"
     (direção arquivo → KAI).
   - `autoSyncFromKaiEnabled` — "Alterações via KAI" (direção KAI →
     arquivo).

   Os 3 são radio/checkbox ligado-desligado (não há um modo "Perguntar"
   selecionável dentro de cada um). **Perguntar é automático**: sempre
   que o `ProjectSyncManager` detecta um **conflito real** (os dois
   lados mudaram desde o último sync), ele SEMPRE abre o diálogo de
   conflito — independente do valor dos 3 campos acima, e mesmo com a
   sincronização automática desabilitada por completo (nesse caso o
   conflito só pode surgir via uma sincronização **manual** disparada
   pelo usuário, ver item 4).
4. **Escopo adicional (pedido explícito do usuário, expande a spec
   original):**
   - **Ações manuais de sync no menu de contexto**: clique-direito
     (ou tecla `Insert`, mesmo padrão do `contextMenuShortcut` já
     existente) numa pasta-**projeto** (`Folder::isProject == true`)
     ganha duas ações novas — "Sincronizar Kai → Arquivo" e
     "Sincronizar Arquivo → Kai" (nomes/ícones a refinar na UI, mas w/
     ícone de sync direcional). Aparecem **sempre** em pastas-projeto,
     tanto com a sincronização automática habilitada (útil pra forçar
     uma sincronização fora do ciclo automático, ou depois de um
     conflito ignorado) quanto desabilitada (aí é o único jeito de
     sincronizar).
   - **Indicador visual de "sincronização pausada"**: quando um
     conflito não resolvido deixa um projeto com a sync pausada
     (aguardando decisão do usuário), um ícone de "desconectado" (ex:
     `plug-off`/`link-off` do pool de ícones) aparece tanto no item da
     árvore (pasta-projeto) quanto na aba (quando a pasta-projeto é
     raiz de aba) — mesmo padrão visual de badge que já existe para
     status de processo em background.
   - **Diálogo de conflito**: precisa deixar o usuário **ver os dois
     arquivos** (Kai vs. arquivo externo), **ver a diferença** (diff)
     e **escolher qual manter** — ver §1.5, que trata isso como uma
     sub-feature com desenho próprio (o usuário sinalizou que esse
     diálogo "requer análise adicional").
   - **Não pode roubar foco**: o diálogo de conflito só pode aparecer
     enquanto a janela do Kai estiver em foco/ativa — se um conflito é
     detectado com o Kai em background (ex: minimizado, ou outra
     janela em foco), ele fica **pendente** (mesmo estado "pausado" do
     indicador visual acima) e o diálogo só abre quando o usuário volta
     pro Kai (ou aciona manualmente).
5. **Notificação CRON respeita o master switch global.** O toast do SO
   só aparece com `notificationsEnabled == true` **e**
   `Command::cronNotifyOnRun == true`; o registro no histórico
   (`NotificationHistory`) continua acontecendo sempre que
   `cronNotifyOnRun == true`, independente do master switch (mesmo
   padrão que já existe hoje pras demais notificações).

Se qualquer detalhe abaixo contradisser o que está aqui, esta seção 0
é a fonte de verdade — o resto do documento foi revisado pra ficar
consistente com ela.

---

## 1. AUTOSYNC DE COMANDOS POR PROJETO

### 1.1 Modelo de dados

**`src/core/models.h` — `struct Folder`:**
```cpp
// DIRETÓRIO DE TRABALHO (feature Autosync): raiz do projeto no filesystem
// usada (a) como cwd default herdado pelos comandos da pasta que não
// declararem working_dir próprio, e (b) como raiz de auto-discovery/
// sincronização (onde mora o kai.json/kai.yml sincronizado — ver
// ProjectSyncManager). Vazio = sem diretório de trabalho próprio;
// resolução hierárquica do nível mais específico pro mais genérico
// (comando -> pasta -> pasta pai -> ...), MESMO mecanismo de
// Folder::terminalTarget/kInheritTerminalTarget (campo novo, mecanismo
// de herança reaproveitado).
QString workingDir;
```
- `toJson`/`fromJson`: chave `"working_dir"`.
- `ExecutionPipeline` ganha uma resolução equivalente a
  `effectiveTerminalProfileName`, algo como
  `effectiveWorkingDir(command, folder, allFolders)`: usa
  `Command::workingDir` se não vazio; senão sobe a cadeia de pastas
  procurando o primeiro `Folder::workingDir` não vazio; senão cai no
  comportamento atual (sem cwd explícito / `projectPath` se existir,
  igual hoje).

**Novo — mapeamento de diretório de busca:**
```cpp
// Mapeamento de um diretório externo do filesystem para uma pasta do
// Kai (feature "Diretórios de busca" / Autosync).
struct SearchDirectory {
    QString id;
    QString name;         // opcional — rótulo livre
    QString externalPath; // pasta monitorada no filesystem
    QString kaiFolderId;  // Folder::id correspondente dentro do Kai

    QJsonObject toJson() const;
    static SearchDirectory fromJson(const QJsonObject &obj);
};
```

**`SettingsData` (config-manager.h) — novos campos:**
```cpp
// --- AUTOSYNC (Diretórios de busca) ---
// 3 campos binários (radio ligado/desligado cada) — NÃO existe modo
// "perguntar" selecionável: a pergunta ao usuário é automática sempre
// que um conflito real é detectado, independente destes 3 valores
// (ver ProjectSyncManager). autoSyncFromFileEnabled/autoSyncFromKaiEnabled
// só têm efeito com autoSyncEnabled == true.
bool autoSyncEnabled = false;            // master switch da feature
bool autoSyncFromFileEnabled = false;    // "Mudanças de arquivos de configuração" (arquivo -> KAI)
bool autoSyncFromKaiEnabled = false;     // "Alterações via KAI" (KAI -> arquivo)
QVector<SearchDirectory> searchDirectories;
```
- Persistidos em `settings.json`, mesmo padrão de `terminalProfiles`/
  `environments` (arrays embutidos no objeto settings).
- As ações manuais de sync (menu de contexto, item 4 da seção 0) NÃO
  dependem de `autoSyncEnabled` — funcionam mesmo com a feature
  desligada, contanto que a pasta seja projeto e tenha
  `workingDir`/`projectPath` resolvível.

**Estado runtime (não persistido em `settings.json` — é derivado, vive
no `ProjectSyncManager`):**
```cpp
// Projetos com sync PAUSADA aguardando decisão do usuário sobre um
// conflito (ver §1.4). Alimenta o badge "desconectado" na árvore/aba.
QSet<QString> pausedFolderIds; // ProjectSyncManager::m_pausedFolderIds
```

### 1.2 Sincronização KAI → Arquivos

- Ponto de disparo: qualquer `ConfigManager::saveCommands` bem-sucedido,
  com `autoSyncEnabled && autoSyncFromKaiEnabled` — ou disparo **manual**
  via menu de contexto (item 4, seção 0), que ignora esses 2 flags de
  propósito (é uma ação explícita do usuário).
  - **Como saber QUAIS pastas foram afetadas — `saveCommands(const
    CommandsData &data)` recebe o estado INTEIRO, não um diff.** Não
    tente calcular um diff contra o estado anterior. A forma correta e
    simples: em `notifyCommandsSaved(commandsData)`, iterar **todas** as
    pastas com `isProject == true` e `workingDir`/`projectPath`
    resolvível (não só as "afetadas") e, para cada uma, gerar o JSON via
    `exportFolder(...)` e comparar o hash contra `hashUltimoSync`
    daquela pasta — se o hash bateu, não mudou nada, pula sem escrever
    (barato: é só um hash de string, não uma escrita de disco). Só as
    pastas cujo hash realmente mudou chegam a acionar a checagem de
    conflito (§1.3) e a escrita de arquivo. Isso evita tanto "esqueci de
    detectar a pasta X mudou" quanto reescrever arquivo à toa.
- Resolve o arquivo-alvo (ver decisão §0.2):
  - se já existir `<root>/kai.json` → escreve JSON nele
    (`ConfigManager::exportFolder(folderId, ..., lean=false)`, ver
    §1.7 sobre por que `lean=false` é obrigatório aqui).
  - senão → cria/atualiza `<root>/kai.yml`, passando o mesmo JSON
    exportado por `core::jsonTextToYamlText` (`YamlBridge`) antes de
    escrever.
- Escrita sempre **atômica** (tmp + rename, mesmo padrão de
  `writeJsonAtomic`) e sempre **fora da GUI thread** (worker/
  `QtConcurrent::run`; nunca I/O síncrono na call stack da UI).
- **Antes de escrever de verdade**, roda a mesma checagem de conflito
  de §1.3 (o arquivo pode ter mudado externamente desde o último sync
  sem o Kai ainda ter processado o evento do watcher) — a direção
  KAI→arquivo NÃO tem prioridade sobre um conflito pendente; se houver
  um, pausa e pergunta, não sobrescreve.
- Depois de escrever com sucesso, atualiza o hash do conteúdo
  canônico (`QCryptographicHash::Sha256` do JSON, antes da conversão
  pra YAML se for o caso — o hash sempre compara na representação
  JSON interna, nunca no texto YAML formatado) como "última versão
  sincronizada" daquele projeto — base do controle de conflito da
  direção inversa (§1.3).

### 1.3 Sincronização Arquivos → KAI

- Um `QFileSystemWatcher` (por instância de `ProjectSyncManager`, ver
  §1.4) observa cada diretório-raiz de projeto sincronizado (watch do
  **diretório**, não só do arquivo — editores/git trocam o arquivo por
  um novo inode em vez de escrever in-place, e um watch de arquivo
  sozinho perde esse evento).
- **Debounce**: mudanças no filesystem chegam em rajada (git checkout,
  save do editor); usar `QTimer` de ~500ms por projeto para coalescer
  antes de processar.
- Disparo condicionado a `autoSyncEnabled && autoSyncFromFileEnabled`
  para o caminho **automático**; a ação manual do menu de contexto
  ("Sincronizar Arquivo → Kai") roda a mesma lógica de decisão
  ignorando esses 2 flags.
- **Matriz de decisão** (sempre roda, automático ou manual):
  1. Calcula `hashExterno` do conteúdo novo do arquivo-alvo (JSON
     canônico, convertendo de YAML pra JSON via `yamlTextToJsonText`
     primeiro se o arquivo for `.yml`).
  2. Compara com `hashUltimoSync` (guardado por projeto).
  3. Compara o **estado atual do Kai** para aquele projeto (serializar
     via `exportFolder` de novo) contra `hashUltimoSync`.
  4. Tabela:
     | Kai mudou desde o último sync? | Arquivo mudou? | Ação |
     |---|---|---|
     | não | não | nada (evento espúrio, ex: mtime touch) |
     | não | sim | aplica arquivo→Kai automaticamente |
     | sim | não | nada (Kai já é a fonte mais nova; a próxima escrita KAI→arquivo resolve) |
     | sim | sim | **conflito** — nunca resolve sozinho, mesmo com os flags automáticos ligados |
  5. **Em conflito:**
     - marca o projeto em `pausedFolderIds` → dispara o badge
       "desconectado" na árvore/aba imediatamente;
     - registra em `NotificationHistory` (`eventKey =
       "project_sync_conflict"`);
     - se a janela do Kai estiver **ativa/em foco**: abre o
       `SyncConflictDialog` (§1.5) imediatamente.
     - se **não** estiver em foco: fica pendente — o Kai escuta
       `QApplication::focusChanged`/`activationChanged` (ou o sinal
       equivalente já usado pra outras features de foco do app, ver
       `autoHideOnFocusLoss`) e abre o diálogo pendente na próxima vez
       que a janela ganhar foco.
     - resolvido o diálogo (usuário escolhe manter Kai ou usar
       arquivo, ou ignora/fecha sem decidir): se decidiu, aplica a
       direção escolhida e sai de `pausedFolderIds`; se só fechou sem
       decidir, o projeto **continua pausado** (badge fica), e as duas
       ações manuais do menu de contexto continuam disponíveis pra
       resolver depois.
  6. Aplicar arquivo→Kai reaproveita `ConfigManager::importFromJson` +
     uma variante de `mergeImportResult` em modo **substituição**
     daquele `folderId` específico (não "somar sempre") — ver §1.7.

### 1.4 `ProjectSyncManager` (novo — `src/core/project-sync-manager.h/.cpp`)

```cpp
class ProjectSyncManager : public QObject {
    Q_OBJECT
public:
    explicit ProjectSyncManager(ConfigManager &configManager, QObject *parent = nullptr);

    // Recalcula os diretórios observados (chamado ao carregar settings/
    // commands, e sempre que searchDirectories ou pastas-projeto mudam).
    void rescan(const core::CommandsData &commandsData, const core::SettingsData &settings);

    // Chamado depois de QUALQUER saveCommands bem-sucedido — decide se
    // dispara sync KAI->arquivo automática pros projetos afetados.
    void notifyCommandsSaved(const core::CommandsData &commandsData);

    // Ações manuais (menu de contexto) — ignoram os flags automáticos,
    // mas passam pela MESMA checagem de conflito.
    void syncKaiToFileManually(const QString &folderId);
    void syncFileToKaiManually(const QString &folderId);

    bool isPaused(const QString &folderId) const;

    // Resposta do usuário ao SyncConflictDialog (ver §1.5) para o
    // conflito pendente daquele folderId.
    enum class ConflictResolution { KeepKai, UseFile, Dismissed };
    void resolveConflict(const QString &folderId, ConflictResolution resolution);

signals:
    void externalChangeApplied(const QString &folderId);
    // `hasFocusAwarePendingDialog`: false quando o conflito só entrou
    // no estado "pausado" sem abrir diálogo (Kai sem foco) — a UI usa
    // isto pra saber se precisa abrir o diálogo quando a janela ganhar
    // foco depois.
    void syncConflictDetected(const QString &folderId, const QString &externalPath,
                               bool dialogShownNow);
    void syncPausedChanged(const QString &folderId, bool paused); // -> badge "desconectado"
    void syncError(const QString &folderId, const QString &message);

private:
    void handleDirectoryChanged(const QString &path);
    void processPendingChange(const QString &folderId);
    QString targetFilePathFor(const QString &folderId) const; // kai.json existente OU kai.yml novo
    QString lastSyncedHashFor(const QString &folderId) const;
    void setLastSyncedHash(const QString &folderId, const QString &hash);

    ConfigManager &m_configManager;
    QFileSystemWatcher m_watcher;
    QMap<QString, QString> m_lastSyncedHashByFolder; // persistido (ver abaixo)
    QMap<QString, QTimer *> m_debounceByFolder;
    QSet<QString> m_pausedFolderIds;
    QSet<QString> m_pendingDialogFolderIds; // conflitos aguardando o Kai ganhar foco
};
```
- `m_lastSyncedHashByFolder` e `m_pausedFolderIds` precisam sobreviver
  a restart do app (senão todo boot vira "conflito" espúrio, ou perde o
  estado de "pausado"). Persistir num arquivo próprio,
  `project-sync-state.json` (mesmo padrão atômico de `ConfigManager`),
  **não** dentro de `settings.json` (é estado derivado/cache, não
  preferência do usuário — mesmo raciocínio já usado pra
  `dynamic-vars.json` vs `environments`).
- Instanciado uma vez em `MainWindow`, análogo a `m_configManager`/
  `m_notificationHistory`. `MainWindow` conecta
  `syncPausedChanged`/`syncConflictDetected` para atualizar o badge da
  árvore/aba e, quando `dialogShownNow == true`, abrir o
  `SyncConflictDialog`.

### 1.5 Diálogo e menu de conflito (escopo novo, pedido explícito do usuário)

> Esta subseção é a que o usuário sinalizou precisar de "análise
> adicional" — o desenho abaixo é um ponto de partida razoável, mas
> vale uma revisão de UX dedicada antes de codar, já que envolve uma
> tela nova de comparação de arquivos que não tem equivalente hoje no
> Kai.

**`SyncConflictDialog` (novo — `src/ui/features/settings/sync-conflict-dialog.h/.cpp`,
ou um local próprio `src/ui/features/sync/` se o número de arquivos
desta feature justificar uma pasta dedicada):**
- Mostra os dois lados: "Versão do Kai" vs. "Versão do arquivo"
  (`<caminho resolvido>`), cada lado renderizado como o JSON/YAML
  formatado (reaproveitar o mesmo visualizador usado no
  `CommandJsonEditorDialog`/`FoldableJsonView` para syntax highlight,
  em vez de um `QPlainTextEdit` cru).
- **Diff**: destaca linhas adicionadas/removidas/alteradas entre os
  dois lados — não há utilitário de diff no Kai hoje; usar um algoritmo
  simples de diff por linha (ex: LCS/Myers básico, sem dependência
  nova) sobre o texto formatado de cada lado.
- 3 ações: "Manter versão do Kai" (`KeepKai` → dispara sync KAI→arquivo
  imediatamente, sobrescrevendo o arquivo), "Usar versão do arquivo"
  (`UseFile` → aplica arquivo→Kai, sobrescrevendo o estado do Kai
  daquele projeto), "Fechar sem decidir" (`Dismissed` → mantém pausado,
  fecha o diálogo).
- **Não-modal em relação ao app** no sentido de não bloquear outras
  janelas do SO, mas modal em relação à `MainWindow` — e só é
  instanciado/mostrado enquanto a `MainWindow` está ativa (ver regra de
  foco em §1.3.5); nunca aparece por cima de outro app.
- Reaproveita `ConfigManager::exportFolder`/`importFromJson` para
  montar os dois lados a partir do estado real (não precisa de um
  parser de diff estrutural — é comparação textual do JSON
  pretty-printed de cada lado, suficiente pro caso de uso).

**Menu de contexto em pasta-projeto** (`command-tree-widget.cpp`, onde
o menu de contexto atual já é montado):
- Duas ações novas, só quando `folder.isProject == true`:
  - `"project.sync_to_file"` — "Sincronizar para o arquivo" (ícone de
    seta Kai→disco).
  - `"project.sync_from_file"` — "Sincronizar do arquivo" (ícone de
    seta disco→Kai).
- Quando `ProjectSyncManager::isPaused(folderId) == true`, o item do
  menu (ou um item extra acima dos dois) ganha destaque/hint — texto
  tipo "Sincronização pausada — conflito não resolvido" — reabrindo o
  `SyncConflictDialog` ao clicar, em vez de (ou além de) as duas ações
  de sync direta.
- Atalho de teclado: reaproveita `contextMenuShortcut` (Insert, já
  existente) — nenhum atalho novo dedicado necessário, é só mais um
  item no menu que ele já abre.

**Badge "desconectado" na árvore/aba:**
- Ícone (ex.: `link-off`/`plug-off` do pool `LucideIcons`) sobreposto
  ao ícone normal da pasta-projeto na árvore quando
  `isPaused(folderId) == true` — mesmo mecanismo visual já usado pro
  status Running/Error/Success de processos em background
  (`command-tree-widget`), só que fixado na pasta em vez de num
  comando.
- Quando a pasta-projeto é raiz de uma aba dinâmica, o mesmo ícone
  aparece também na aba (pequeno overlay no canto do ícone da aba,
  mesmo padrão de badge usado hoje pra indicar comando rodando/erro
  dentro daquela aba, se esse padrão já existir — validar em
  `main-window.cpp`/`command-tree-widget.cpp` antes de desenhar algo
  novo do zero).

### 1.6 Tela "Diretórios de busca" (nova aba em Settings)

**Arquivos novos:**
- `src/ui/features/settings/tabs/search-directories-tab.h/.cpp`

**Registro:** adicionar ao `QStackedWidget`/lista de navegação do
`SettingsDialog` (mesmo padrão de `GeneralTab`/`AppearanceTab`/etc. em
`settings-dialog.h/.cpp`).

**Conteúdo da aba**, seguindo explicitamente "mesmo padrão visual e de
interação da adição de parâmetros" — isto é, replicar o layout de
`parameter-editor-widget.h/.cpp` (tabela + botão adicionar + edição/
exclusão inline):
1. **Bloco de configuração de sincronização** (topo), 3 radio/checkbox
   simples (ver §0.3/§1.1):
   - "Sincronização automática" (`autoSyncEnabled`).
   - "Mudanças de arquivos de configuração" (`autoSyncFromFileEnabled`)
     — habilitado só com o campo acima ligado.
   - "Alterações via KAI" (`autoSyncFromKaiEnabled`) — idem.
   - Texto de apoio explicando que conflitos sempre perguntam,
     independente destes 3 campos.
2. **Tabela de diretórios de busca** (abaixo), colunas:
   - Nome (opcional, editável inline)
   - Pasta (path externo, com botão "..." → `QFileDialog::getExistingDirectory`)
   - Pasta KAI (usa o **Folder Picker V2**, ver §1.7)
   - coluna de ações (editar/excluir inline, ícones — mesmo padrão dos
     outros editores de tabela do Kai)
   - Botão "Adicionar diretório externo" acima/abaixo da tabela.
3. Persistência: ao fechar/aplicar o diálogo, grava em
   `SettingsData::searchDirectories` + os 3 campos, via
   `ConfigManager::saveSettings` (mesmo fluxo de commit do
   `SettingsDialog::buildSettings()` já existente).
4. i18n: ver checklist completo em §5.

### 1.7 Seletor de pastas KAI — V2 (visual/UX, sem mudança funcional)

**Estado atual** (`src/ui/shared/dialog-utils.h`): o "folder picker" é
um `QComboBox` populado em ordem de árvore (`foldersInTreeOrder`) com
texto gerado por `folderComboLabel()` — indentação por espaços + nome +
caminho completo entre parênteses como hint, largura travada por
`capComboBoxWidth`. Usado em `command-editor-dialog`,
`collection-editor-dialog`, `folder-editor-dialog`,
`project-import-options-dialog`, `export-dialog` (`m_folderPickerCombo`).

**V2 — o que muda:**
- Trocar o texto "indentação + (caminho entre parênteses)" por um
  **delegate customizado** (`QStyledItemDelegate`) que desenha o
  caminho como pills/chips coloridas, uma por segmento de pasta (ex:
  `API` `›` `Projeto A` `›` `Sub`), cada chip com:
  - `border-radius` via `utils::tokens::radiusSm()` (regra §9 do
    AGENTS.md — nunca hardcode);
  - cor de fundo = tom do `accent` variando por profundidade: quanto
    mais profundo o segmento, mais claro o tom (usar
    `QColor::lighter(100 + depth*N)` sobre `utils::tokens::accent()`,
    ou interpolar accent↔`surface2()` — decisão de implementação, mas a
    regra de negócio é "mais fundo = mais claro").
- Extrair a lógica de path/pills num widget novo reutilizável,
  `src/ui/shared/folder-picker-widget.h/.cpp` (`FolderPickerWidget`),
  em vez de repetir o delegate em cada tela que hoje usa `QComboBox`
  cru — os 5+ pontos de uso listados acima passam a instanciar este
  widget novo. Mantém a mesma API pública mínima
  (`selectedFolderId()`, `setSelectedFolderId()`, sinal
  `selectionChanged(QString)`), pra minimizar o diff nos callers.
- Espaçamento: aumentar padding interno do popup/lista (linhas do
  dropdown mais altas, gap maior entre pills).
- Busca: hoje é um `QCompleter` fixo sobre `folderComboLabel()` (ver
  comentário de `foldersInTreeOrder` — ordena por pré-ordem de árvore
  pra pasta aparecer antes dos próprios filhos no filtro). V2 deve
  melhorar comportamento: destacar o trecho batido dentro do nome
  (bold/accent no texto do chip que casou), e não só na pasta-folha mas
  em qualquer segmento do caminho.
- **Sem mudança de comportamento**: o id retornado, hierarquia,
  criação/edição continuam idênticos — é puramente visual/UX, conforme
  a spec explicita.
- Este widget novo é o mesmo usado no campo "Pasta KAI" da tela de
  Diretórios de busca (§1.6).

### 1.8 Reuso e pontos de atenção de implementação

- `FolderPathResolver` (`src/core/folder-path-resolver.h`) já resolve
  path "A/B/C" → id criando pastas intermediárias — reaproveitar direto
  na importação arquivo→Kai (§1.3) em vez de reescrever resolução de
  hierarquia.
- `ConfigManager::importFromJson`/`mergeImportResult` hoje são
  "sempre soma" (ids novos, nunca atualiza no lugar) quando `lean=true`.
  Para sync arquivo→Kai precisamos do modo **atualizar no lugar**
  (equivalente ao `lean=false`, ids estáveis) — usar
  `exportFolder(..., lean=false)` do lado da escrita (§1.2) para que
  reimportar o mesmo arquivo **não duplique** comandos/pastas. Isso é
  uma restrição forte: o par sync precisa sempre andar em `lean=false`
  (ids estáveis), diferente do Export/Import manual do usuário (que por
  padrão é `lean=true`). Documentar isso claramente no código para não
  ser "corrigido" por engano depois.
- `YamlBridge` (`jsonTextToYamlText`/`yamlTextToJsonText`) é exatamente
  o que resolve a conversão pro caso "arquivo novo nasce em YAML"
  (§0.2) — nenhum parser/serializador YAML novo é necessário, só
  encadear: `exportFolder(...)` (JSON) → `jsonTextToYamlText` (se for
  criar `.yml`) na escrita, e `yamlTextToJsonText` → `importFromJson`
  (se o arquivo-alvo for `.yml`) na leitura.
- `looksLikeJson` (já existe em `yaml-bridge.h`) resolve o "sniff" de
  qual arquivo é qual quando o `ProjectSyncManager` precisa decidir se
  um `<root>/kai.json` ou `<root>/kai.yml` existente deve ser tratado
  como JSON ou YAML — mas na prática a extensão do arquivo já deveria
  bastar (`.json` → JSON, `.yml`/`.yaml` → YAML); usar `looksLikeJson`
  só como fallback defensivo se o conteúdo não bater com a extensão.
- Nenhuma leitura/escrita de arquivo do `ProjectSyncManager` pode rodar
  na GUI thread — usar `QtConcurrent::run` ou um `QThread` dedicado
  para hashing/parsing, sinal de volta pra thread principal só para
  aplicar o resultado (regra não-negociável do AGENTS.md).

### 1.9 Testes (novo arquivo `tests/test_project_sync_manager.cpp`)

Cobrir, no mínimo:
- Descoberta de arquivos: `rescan()` encontra `kai.json` OU `kai.yml`
  sob `workingDir`/`projectPath` de pastas-projeto e monta os watchers
  certos; prioriza `kai.json` existente sobre criar `kai.yml` novo.
- KAI→arquivo automático: salvar um comando com
  `autoSyncEnabled && autoSyncFromKaiEnabled` grava o arquivo-alvo (nas
  duas variantes de formato); com qualquer um dos dois `false` não
  grava nada automaticamente.
- Ação manual (`syncKaiToFileManually`/`syncFileToKaiManually`)
  funciona mesmo com `autoSyncEnabled == false`.
- Arquivo→KAI automático: editar o arquivo-alvo externamente (sem
  tocar o Kai) aplica a mudança quando
  `autoSyncFromFileEnabled == true` e o Kai não tinha mudança
  pendente.
- **Conflito**: editar o arquivo externamente E mudar algo no Kai para
  aquele projeto sem sincronizar antes → `syncConflictDetected`
  emitido, `pausedFolderIds` contém o projeto, nada é sobrescrito
  automaticamente — mesmo com os 3 campos automáticos todos `true`.
- **Foco**: conflito detectado com a janela sem foco não abre o
  diálogo (`dialogShownNow == false`), mas marca pendente; simular
  ganho de foco e verificar que o diálogo pendente é sinalizado pra
  abrir.
- `resolveConflict(KeepKai/UseFile)` aplica a direção certa e remove de
  `pausedFolderIds`; `Dismissed` mantém pausado.
- Persistência de `m_lastSyncedHashByFolder`/`m_pausedFolderIds`
  sobrevive a reload do `ProjectSyncManager` (restart simulado).
- Hierarquia: `Folder::workingDir` herdado por subpasta sem valor
  próprio; `Command::workingDir` vazio herda do `Folder::workingDir`
  mais próximo (via `ExecutionPipeline::effectiveWorkingDir`).
- Feature flag: com `autoSyncEnabled == false`, nada dispara
  automaticamente em nenhuma direção, mesmo com diretórios de busca
  cadastrados (mas as ações manuais continuam funcionando).
- `test_dialog_utils.cpp`/novo `test_folder_picker_widget.cpp`: geração
  de pills, ordenação/pré-ordem preservada, seleção retorna o id certo.
- `test_i18n_sync.cpp` precisa passar depois de adicionar as chaves
  novas (rodar sempre após qualquer string nova).

---

## 2. MÓDULO CRON SCHEDULER

*(sem mudanças em relação à v1 do documento, exceto a confirmação do
master switch em §2.4 — mantido aqui na íntegra por completude.)*

### 2.1 Modelo de dados

**`src/core/models.h` — `struct Command`:**
```cpp
// AGENDAMENTO CRON (Módulo Cron Scheduler): quando preenchido, o
// scheduler interpreta esta expressão e dispara o comando pela pipeline
// normal no horário correspondente — mesmo espírito de autoRun, mas
// recorrente em vez de "uma vez no boot". Vazio = sem agendamento
// (comportamento atual, sem mudança). Só relevante para
// CommandType::Shell.
QString cronExpression;

// NOTIFICAR EXECUÇÃO CRON: independente do agendamento em si — permite
// ter uma expressão cron configurada sem gerar notificação a cada
// disparo (default false, mesmo espírito opt-in de
// notifyOnBackgroundProcessSuccess). Quando true, cada disparo do
// scheduler gera notificação (respeitando o master switch global
// notificationsEnabled para o TOAST — ver §2.4) com o resultado; o
// output do comando fica disponível por hover/expansão no histórico.
bool cronNotifyOnRun = false;
```
- `toJson`/`fromJson`: chaves `"cron_expression"` e
  `"cron_notify_on_run"`.
- **Não reutilizar `autoRun`/`autoRunDelaySec`** — são conceitos
  paralelos (dispara 1x no boot vs. recorrente por expressão); o spec
  pede pipeline compartilhada, não campo compartilhado.

### 2.2 Parser/avaliador CRON

Sem parser cron no projeto atualmente. Recomendo implementar um parser
mínimo próprio — `src/utils/cron-expression.h/.cpp`, cobrindo a
sintaxe padrão de 5 campos (`minuto hora dia-do-mês mês
dia-da-semana`), incluindo `*`, listas (`1,2,3`), ranges (`1-5`), steps
(`*/15`) e nomes de dia da semana/mês abreviados — mesmo espírito do
`YamlBridge` ("subconjunto tratável" em vez de biblioteca completa,
padrão já estabelecido no projeto), evitando dependência externa nova
(atrito de build multiplataforma, spec 08).

```cpp
namespace kai::utils {

struct CronExpression {
    bool valid = false;
    QString error; // preenchido quando valid == false

    static CronExpression parse(const QString &expression);

    // Próximo instante >= `from` que casa a expressão. std::nullopt se
    // a expressão for inválida.
    std::optional<QDateTime> nextOccurrence(const QDateTime &from) const;
};

// Tradução legível da expressão pro HINT do campo no editor. Ex:
// "0 9 * * 1-5" -> "Todo dia útil às 09:00". Best-effort: expressões
// incomuns caem num fallback genérico, nunca lança exceção.
QString describeCronExpression(const CronExpression &expr);

} // namespace kai::utils
```
- `describeCronExpression` monta a frase combinando fragmentos fixos
  traduzidos via `utils::tr()` + números literais (nunca uma frase
  inteira hardcoded num idioma só) — mesmo cuidado já aplicado em
  `date-param-format.cpp` para presets de data.

### 2.3 `CronScheduler` (novo — `src/engine/cron-scheduler.h/.cpp`)

Fica em `src/engine/` (dispara execução, mesmo escopo de
`ProcessManager`/`ExecutionPipeline`).

```cpp
class CronScheduler : public QObject {
    Q_OBJECT
public:
    explicit CronScheduler(QObject *parent = nullptr);

    // Recalcula os timers a partir do estado atual de comandos.
    void reschedule(const QVector<core::Command> &commands);

signals:
    // A UI conecta isto no MESMO handler usado por clique manual/
    // autorun — o scheduler NUNCA chama ExecutionPipeline diretamente.
    void commandDue(const QString &commandId);

private:
    void armTimerFor(const core::Command &command);

    // commandId -> próximo QTimer armado (singleShot, rearmado após
    // disparar, calculando a PRÓXIMA ocorrência — nunca um intervalo
    // fixo, cron não é uniforme).
    QMap<QString, QTimer *> m_timersByCommandId;
};
```

**Integração em `MainWindow`** (mesmo padrão de `scheduleAutoRunCommands`,
`src/ui/main-window.cpp:2234-2258`):
```cpp
connect(&m_cronScheduler, &engine::CronScheduler::commandDue, this,
        [this](const QString &commandId) {
    if (!m_commandsById.contains(commandId)) {
        return;
    }
    const core::Command command = m_commandsById.value(commandId);
    utils::Logger::info(kLogTag, QStringLiteral("Cron: disparando '%1'.").arg(commandId));
    runCommandWithParams(command, command.lastParamValues);
    // cronNotifyOnRun tratado no callback de conclusão do runner (§2.4).
});
```
- O scheduler só dispara; execução (hooks, condições, terminal target,
  captura de env) é 100% `ExecutionPipeline`/`runCommandWithParams`,
  igual ao autorun — nenhuma pipeline paralela.
- Comando cron com parâmetro obrigatório sem `lastParamValues`
  suficiente: pula e loga (mesma regra do autorun).

### 2.4 Notificações de execução CRON

- Reaproveita `NotificationHistory` — novo `eventKey =
  "cron_command_run"`.
- **Toast do SO**: só aparece com `notificationsEnabled == true` **e**
  `command.cronNotifyOnRun == true` (decisão confirmada, §0.5) — título
  "Comando CRON executado: <nome>" + resumo curto (status + 1ª linha
  do output).
- **Histórico** (`NotificationRecord::body`): sempre registrado quando
  `cronNotifyOnRun == true`, com o output completo (truncado ao mesmo
  limite de `runs.json`), independente do master switch — "hover/
  expansão do output" citado na spec vira expandir o item na tela de
  histórico de notificações (validar se o widget de histórico já
  suporta corpo multi-linha colapsável antes de assumir UI nova).

### 2.5 Editor de comando — campo CRON (UI)

**Arquivo:** `src/ui/features/command-editor/command-editor-dialog.cpp`
(seção específica de Shell, ao lado de `autoRun`/`autoRunDelaySec`).

- Campo de texto (`QLineEdit`) para a expressão, com hint ao vivo
  abaixo (`mutedFg()`) atualizado a cada `textChanged`, mostrando
  `utils::describeCronExpression(...)` ou erro i18n quando
  `CronExpression::parse` falhar — permite salvar mesmo com expressão
  inválida (o scheduler ignora + loga em runtime), consistente com o
  resto do Kai ("nunca travar o usuário").
- Checkbox "Notificar execução" (`cronNotifyOnRun`) ao lado, sempre
  visível, sem efeito prático se `cronExpression` vazio.
- Campo some/desabilita para `type == Http`.

### 2.6 Testes (`tests/test_cron_scheduler.cpp` + `tests/test_cron_expression.cpp`)

- Parser: `* * * * *`, listas, ranges, steps, dia da semana por nome,
  expressão malformada → `valid == false` sem crash.
- `nextOccurrence`: virada de mês/ano; usar UTC internamente
  (documentar a decisão sobre DST).
- `describeCronExpression`: padrões comuns (a cada minuto, diário,
  dias úteis, semanal, mensal).
- `CronScheduler::reschedule` rearma corretamente (comando removido
  cancela timer; expressão editada reagenda; sem `cronExpression`
  nunca ganha timer).
- `commandDue` dispara passando pela pipeline normal (mock/spy, mesmo
  padrão de `test_execution_pipeline.cpp`).
- Notificação: `cronNotifyOnRun == true` gera `NotificationRecord` com
  output no `body`, respeitando `notificationsEnabled` só pro toast
  (mock do `NotificationHistory`/tray).

---

## 3. Arquivos afetados / novos — resumo

### Novos
- `src/core/project-sync-manager.h/.cpp` — `ProjectSyncManager`
- `src/ui/features/settings/tabs/search-directories-tab.h/.cpp`
- `src/ui/features/settings/sync-conflict-dialog.h/.cpp` — `SyncConflictDialog`
- `src/ui/shared/folder-picker-widget.h/.cpp` — `FolderPickerWidget` (V2)
- `src/engine/cron-scheduler.h/.cpp` — `CronScheduler`
- `src/utils/cron-expression.h/.cpp` — `CronExpression` + `describeCronExpression`
- `tests/test_project_sync_manager.cpp`
- `tests/test_sync_conflict_dialog.cpp`
- `tests/test_folder_picker_widget.cpp`
- `tests/test_cron_scheduler.cpp`
- `tests/test_cron_expression.cpp`

### Modificados
- `src/core/models.h/.cpp` — `Folder::workingDir`, `SearchDirectory`,
  `Command::cronExpression`/`cronNotifyOnRun`
- `src/core/config-manager.h/.cpp` — `SettingsData::autoSyncEnabled`/
  `autoSyncFromFileEnabled`/`autoSyncFromKaiEnabled`, persistência de
  `SearchDirectory`
- `src/core/notification-history.h/.cpp` — novo `eventKey`
  `"cron_command_run"`/`"project_sync_conflict"`
- `src/engine/execution-pipeline.h/.cpp` —
  `effectiveWorkingDir(command, folder, allFolders)`
- `src/ui/features/settings/settings-dialog.h/.cpp` — registra a nova
  aba
- `src/ui/features/command-editor/command-editor-dialog.h/.cpp` —
  campos CRON
- `src/ui/features/command-editor/command-tree-widget.h/.cpp` — ações
  de menu de contexto + badge "desconectado"
- `src/ui/shared/dialog-utils.h` — pontos que hoje usam
  `folderComboLabel`/`QComboBox` cru migram para `FolderPickerWidget`
  (nos 5 callers listados em §1.7)
- `src/ui/main-window.h/.cpp` — instancia `ProjectSyncManager` e
  `CronScheduler`, conecta sinais (inclusive foco de janela), chama
  `reschedule()`/`rescan()` nos pontos certos
- `assets/i18n/en.json`, `assets/i18n/pt.json` — todas as chaves novas
  (ver §5)
- `tests/CMakeLists.txt` — registra os `.cpp` de teste novos

---

## 4. Regras não-negociáveis a re-verificar antes de dar como pronto

- **Nenhum nome de ícone entra no código sem confirmar que o arquivo
  existe de verdade.** Este documento cita nomes de ícone só como
  EXEMPLO/sugestão em texto (`"plug-off"`/`"link-off"`, `"sync"` se
  aparecer em algum rascunho, ícones de seta direcional etc.) — **nenhum
  desses é garantia de que o `.svg` existe no projeto.** Antes de usar
  qualquer nome de ícone novo em qualquer `LucideIcons::icon(...)`,
  `QListWidgetItem`, badge, etc., rodar
  `ls assets/icons/lucide/ | grep -i <termo>` (ou `find` equivalente) e
  usar exatamente o nome do arquivo encontrado, sem a extensão `.svg`.
  Se o ícone desejado não existir no pool, escolher o mais próximo que
  EXISTE — nunca inventar/assumir um nome "que faz sentido". Um ícone
  usado sem essa checagem renderiza **vazio** silenciosamente (sem erro
  de build, sem crash) — é o tipo de bug que só aparece testando
  visualmente, e já aconteceu uma vez nesta mesma feature.
- Nenhum `QFile`/`QProcess`/watcher síncrono bloqueando a GUI thread —
  hashing, parsing e conversão JSON↔YAML em sync devem rodar fora dela.
- `ProjectSyncManager` e `CronScheduler` em `core`/`engine` **não podem
  incluir nada de `src/ui/`** — comunicação só por signal/slot.
- Toda string nova visível ao usuário passa por `utils::tr()` com chave
  em `en.json` **e** `pt.json` — rodar `test_i18n_sync` antes de
  considerar qualquer parte concluída.
- Todo elemento com `border-radius` novo (chips do Folder Picker V2,
  badge "desconectado", cards da tela de Diretórios de busca, o diálogo
  de conflito) usa `utils::tokens::radiusSm/Md/Lg` — nunca pixel fixo.
- O diálogo de conflito nunca aparece sem a `MainWindow` estar em foco
  — checar isso explicitamente, não só "não usar `Qt::WindowStaysOnTop`
  sem querer".
- Build limpo com `-Wall -Wextra` (`./build.sh`) antes de fechar cada
  etapa; binário final em `build/bin/kai`; `./build.sh install` só
  quando a feature estiver validada manualmente.
- Trabalhar **feature por feature** (ordem sugerida em §6), com teste
  escrito junto de cada mudança — não um PR gigante cobrindo os dois
  módulos de uma vez.

---

## 5. Checklist de chaves i18n (pt/en) a criar

```
settings.search_dirs.title
settings.search_dirs.description
settings.search_dirs.auto_sync_enabled
settings.search_dirs.sync_from_file_enabled
settings.search_dirs.sync_from_kai_enabled
settings.search_dirs.conflicts_always_ask_hint
settings.search_dirs.column_name
settings.search_dirs.column_external_folder
settings.search_dirs.column_kai_folder
settings.search_dirs.add_button
settings.search_dirs.browse_button
settings.search_dirs.delete_confirm_title
settings.search_dirs.delete_confirm_body
folder.working_dir.label
folder.working_dir.placeholder
command_editor.cron.label
command_editor.cron.placeholder
command_editor.cron.hint_prefix
command_editor.cron.invalid_expression
command_editor.cron.notify_checkbox
cron.describe.every_minute
cron.describe.daily_at
cron.describe.weekdays_at
cron.describe.weekly_on_at
cron.describe.monthly_on_at
cron.describe.fallback
notification.cron_command_run.title
notification.cron_command_run.body_success
notification.cron_command_run.body_failure
notification.project_sync_conflict.title
notification.project_sync_conflict.body
sync.conflict_dialog.title
sync.conflict_dialog.body
sync.conflict_dialog.kai_side_label
sync.conflict_dialog.file_side_label
sync.conflict_dialog.keep_kai
sync.conflict_dialog.use_file
sync.conflict_dialog.dismiss
sync.context_menu.sync_to_file
sync.context_menu.sync_from_file
sync.context_menu.paused_hint
sync.badge.disconnected_tooltip
folder_picker.search_placeholder
```
(lista inicial — ao implementar, qualquer string nova que apareça na UI
precisa da própria chave, não reaproveitar uma destas fora de contexto,
regra §8 do AGENTS.md.)

---

## 6. Ordem de implementação sugerida (feature-by-feature)

1. `Folder::workingDir` + `Command::cronExpression`/`cronNotifyOnRun` +
   `SearchDirectory` + `SettingsData::autoSync*` no `models.h/.cpp`/
   `config-manager.h/.cpp` (só dados, com testes de `toJson`/`fromJson`
   round-trip). Build limpo.
2. `ExecutionPipeline::effectiveWorkingDir` (herança de `workingDir`),
   com teste de hierarquia isolado.
3. `utils::CronExpression`/`describeCronExpression`, com
   `test_cron_expression.cpp` cobrindo o parser antes de qualquer UI.
4. `engine::CronScheduler` + integração mínima em `MainWindow` (sem UI
   de edição ainda). Checklist manual: comando com cron hardcoded no
   `commands.json` dispara no horário certo.
5. UI do campo CRON no `command-editor-dialog` + notificação + i18n.
6. `ui::FolderPickerWidget` (V2) isolado, depois trocado nos callers
   existentes um a um.
7. `core::ProjectSyncManager` — primeiro só a direção KAI→arquivo
   automática (sem watcher), depois arquivo→KAI com watcher e detecção
   de conflito (sem UI de conflito ainda — só o sinal e o estado
   `pausedFolderIds`).
8. Ações manuais de menu de contexto + badge "desconectado" na
   árvore/aba (consome o `ProjectSyncManager` do passo 7).
9. `SyncConflictDialog` completo (ver/diferença/escolher) + regra de
   foco da `MainWindow`.
10. Tela "Diretórios de busca" (usa o `FolderPickerWidget` do passo 6).
11. Passada final de i18n (`test_i18n_sync`), build limpo, checklist de
    validação manual completo (entregar ao usuário conforme §7 do
    AGENTS.md).

---

## 7. Pontos ainda abertos (menores, não bloqueiam início da implementação)

1. Desenho visual exato do diálogo de diff (§1.5) — o documento propõe
   diff por linha sobre o JSON pretty-printed; se o usuário quiser algo
   mais rico (side-by-side com scroll sincronizado, por exemplo), é um
   refinamento de UI a decidir durante a implementação da etapa 9.
2. Confirmar se o overlay de badge "desconectado" na ABA já tem um
   mecanismo equivalente reaproveitável (algum badge de status por aba
   já existente) antes de desenhar um novo do zero — checar
   `main-window.cpp` na etapa 8.
3. Ícone exato do pool `LucideIcons` a usar para "desconectado" e para
   as duas ações de sync direcional no menu de contexto — detalhe de
   implementação, não decisão de produto.
