#pragma once

#include <QDialog>
#include <QVector>

#include "core/models.h"
#include "core/config-manager.h"

class QLineEdit;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;
namespace kai::ui { class InlineCodeField; }
class QStackedWidget;
class QPushButton;
class QLabel;
class QSpinBox;
class QListWidget;
class QVBoxLayout;

namespace kai::ui {

class JsonSyntaxHighlighter;

class KeyValueEditorWidget;
class ParameterEditorWidget;
class OutputRespondersEditorWidget;
class HooksEditorWidget;
class ExecutionConditionsEditorWidget;
class EnvExtractorsEditorWidget;
class DeclaredEnvVarsEditorWidget;
class IconPickerWidget;
class CollapsibleSectionCard;

// Diálogo completo de criação/edição de Command, cobrindo os dois tipos
// descritos no resumo da spec:
//   - Aba "Shell": nome, comando, working_dir, is_background, parâmetros
//     dinâmicos (text/select/bool/file) e hooks.
//   - Aba "HTTP": método, URL, headers (key-value), body, env extractors
//     (json_path -> env_var) e hooks.
//
// O tipo do comando (Shell/HTTP) é fixado pela aba ativa no momento do OK.
class CommandEditorDialog : public QDialog {
    Q_OBJECT

public:
    // `folderId` é a pasta onde o comando será criado/já existe (usada
    // como seleção inicial do combo de pasta destino, que permite
    // escolher/trocar livremente entre `allFolders` — spec 05, feedback
    // do usuário). `commandsInSameFolder` alimenta o HooksEditorWidget
    // (exclui o próprio comando, se estiver editando).
    // `allCommands` (opcional): universo COMPLETO de comandos do app, usado
    // pelo HooksEditorWidget só quando o usuário PESQUISA no seletor de
    // hooks (feedback do usuário: por padrão só lista hooks da pasta, mas
    // ao pesquisar deve mostrar todos). Se omitido, a busca de hooks fica
    // restrita a `commandsInSameFolder`, como antes.
    explicit CommandEditorDialog(const QString &folderId,
                                  const QVector<core::Command> &commandsInSameFolder,
                                  const QVector<core::Folder> &allFolders,
                                  QWidget *parent = nullptr,
                                  const core::Command *existingCommand = nullptr,
                                  const QVector<core::TerminalProfile> &terminalProfiles = {},
                                  const QVector<core::Command> &allCommands = {});

    core::Command buildCommand() const;

    // Coleções disponíveis para ligar a parâmetros Select (fonte de dados).
    // Deve ser chamada logo após construir o diálogo (antes de exec()).
    void setAvailableCollections(const QVector<core::Collection> &collections);

    // Nomes das variáveis DINÂMICAS atualmente capturadas (qualquer escopo —
    // feedback do usuário: "adicione ENVS temporárias na interpolação do
    // autocomplete") — entram na mesma listinha de {{var}} dos campos que
    // suportam interpolação neste diálogo. Chamar logo após construir
    // (antes de exec()), mesmo padrão de setAvailableCollections.
    void setAvailableDynamicVarNames(const QStringList &names) { m_availableDynamicVarNames = names; }

    // Item 8 (toggle simples<->avançado): true quando o usuário clicou no
    // botão "Modo avançado" no topo. O chamador (MainWindow), ao ver isto
    // após exec()==Accepted, reabre o comando no editor de JSON cru
    // preservando o conteúdo (via buildCommand()).
    bool switchToAdvancedRequested() const { return m_switchToAdvanced; }

    static QString generateCommandId(const QString &folderId, const QString &name);

private slots:
    void handleAcceptRequested();
    void handleFormatJsonRequested();
    void handleImportCurl();

protected:
    void showEvent(QShowEvent *event) override;

private:
    // Ajusta a altura do diálogo ao conteúdo real (evita vão vazio quando a aba
    // é curta), com piso utilizável e teto na área da tela.
    void fitToContent();
    // Faz o QTabWidget dimensionar pela aba VISÍVEL (o padrão é pela maior).
    void ignoreInactivePagesForSizeHint();
    bool m_autoFitPending = false;

    void setupUi(const core::Command *existingCommand);
    // Troca a página Shell/HTTP (m_tabWidget) E sincroniza o segmented
    // control (m_shellModeButton/m_httpModeButton) — os cliques nos
    // próprios botões já fazem isso inline; este helper cobre as trocas
    // PROGRAMÁTICAS (carregar um comando HTTP existente, importar cURL).
    void setExecutionMode(bool isHttp);
    void populateFolderCombo(const QVector<core::Folder> &allFolders, const QString &selectedFolderId);
    QWidget *buildShellTab();
    QWidget *buildHttpTab();
    // Aba 2 "Configuração" (pedido do usuário: era um popup "Configurações
    // Avançadas", virou aba do sidebar) — constrói CLI Path (sempre
    // visível) + working dir/detalhamento/flags (Shell-only, agrupados em
    // m_shellConfigContainer, escondido em HTTP por setExecutionMode).
    void buildConfigurationTab(QVBoxLayout *pageLayout);
    void populateEnvExtractorsEditor(const QVector<core::EnvExtractor> &extractors);
    QVector<core::EnvExtractor> readEnvExtractors() const;

    QString m_folderId;
    QString m_existingId;
    QVector<core::TerminalProfile> m_terminalProfiles;
    // Preservados na edição para não se perderem ao salvar (não editáveis
    // neste diálogo, mas parte do comando).
    QMap<QString, QString> m_lastParamValues;
    // Ordem manual de exibição do comando existente (drag-and-drop). Deve
    // ser preservada ao editar — senão o comando volta com order == -1 e
    // "pula" para o fim da lista / sai do lugar (bug real reportado).
    int m_existingOrder = -1;

    // Item 8: sinaliza que o usuário pediu para alternar para o modo
    // avançado (JSON). Setado pelo botão do topo antes de accept().
    bool m_switchToAdvanced = false;

    // Corpo Shell/HTTP: QStackedWidget (não mais QTabWidget) trocado pelo
    // segmented control "Shell | HTTP" no cabeçalho do card "Execution
    // Config" (mockup enviado pelo usuário) — ver m_shellModeButton/
    // m_httpModeButton.
    QStackedWidget *m_tabWidget = nullptr;
    QPushButton *m_shellModeButton = nullptr;
    QPushButton *m_httpModeButton = nullptr;

    // Campos comuns.
    QLineEdit *m_nameField = nullptr;
    QLineEdit *m_cliPathField = nullptr;
    QComboBox *m_folderField = nullptr;
    IconPickerWidget *m_iconPicker = nullptr;
    QSpinBox *m_orderField = nullptr;
    HooksEditorWidget *m_hooksEditor = nullptr;
    ExecutionConditionsEditorWidget *m_conditionsEditor = nullptr;

    // Aba Shell.
    InlineCodeField *m_commandField = nullptr;
    InlineCodeField *m_descriptionField = nullptr;
    QLineEdit *m_workingDirField = nullptr;
    // Agrupa working dir/detalhamento/flags — tudo Shell-only na aba
    // "Configuração" — pra esconder/mostrar de uma vez via
    // setExecutionMode() (CLI Path, na mesma aba, fica de fora: vale pros
    // dois tipos de comando).
    QWidget *m_shellConfigContainer = nullptr;
    QCheckBox *m_backgroundField = nullptr;
    QCheckBox *m_captureEnvField = nullptr;
    QCheckBox *m_openLastLinkField = nullptr;
    QCheckBox *m_hideOnRunField = nullptr;
    QCheckBox *m_ignoreExitCodeField = nullptr;
    QCheckBox *m_interactiveTerminalField = nullptr;
    QCheckBox *m_formattedOutputField = nullptr;
    QCheckBox *m_renderMarkdownField = nullptr;
    QComboBox *m_terminalTargetField = nullptr;
    // Wrapper (rótulo "PERFIL" + combo) — só faz sentido em modo Shell,
    // escondido em modo HTTP (ver setExecutionMode).
    QWidget *m_terminalProfileFieldWrapper = nullptr;
    ParameterEditorWidget *m_paramsEditor = nullptr;
    QStringList m_availableDynamicVarNames; // ver setAvailableDynamicVarNames
    OutputRespondersEditorWidget *m_respondersEditor = nullptr;
    QCheckBox *m_autoRunField = nullptr;
    QSpinBox *m_autoRunDelayField = nullptr;
    // Params originais do comando (preservados para reaplicar em
    // setAvailableCollections sem perder o collectionId escolhido, já que
    // setParameters roda no construtor antes das coleções chegarem).
    QVector<core::Parameter> m_originalParams;

    // Aba HTTP.
    QComboBox *m_methodField = nullptr;
    QLineEdit *m_urlField = nullptr;
    InlineCodeField *m_bodyField = nullptr;
    JsonSyntaxHighlighter *m_bodyHighlighter = nullptr;
    QLabel *m_bodyStatusLabel = nullptr;
    // Headers/Extractors: cards de PRIMEIRO NÍVEL (irmãos de Parâmetros/
    // Auto-responsores/Hooks — ver setupUi), não mais aninhados dentro do
    // card "Execution Config". Só visíveis em modo HTTP (ver
    // setExecutionMode).
    CollapsibleSectionCard *m_headersCard = nullptr;
    KeyValueEditorWidget *m_headersEditor = nullptr;
    CollapsibleSectionCard *m_extractorsCard = nullptr;
    EnvExtractorsEditorWidget *m_envExtractorsEditor = nullptr;
    CollapsibleSectionCard *m_declaredEnvVarsCard = nullptr;
    DeclaredEnvVarsEditorWidget *m_declaredEnvVarsEditor = nullptr;

    // Sidebar de abas (pedido do usuário: "quero os COMANDOS, seja um FORM
    // de aba na lateral esquerda, semelhante ao FORM de configuração") —
    // mesmo padrão de SettingsDialog (m_navList/m_pages lá), reaproveitando
    // até o MESMO objectName de QSS ("settingsNav") pra ficar visualmente
    // idêntico. -1 quando o item de nav correspondente não existe (aba
    // condicional ainda não criada nesta sessão — não deveria acontecer,
    // mas guarda contra acesso indevido).
    QListWidget *m_sideNav = nullptr;
    QStackedWidget *m_sidePages = nullptr;
    int m_navRowHeaders = -1;
    int m_navRowExtractors = -1;
    int m_navRowDeclaredEnvVars = -1;
};

} // namespace kai::ui
