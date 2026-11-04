#pragma once

#include <QDialog>
#include <QVector>

#include "core/models.h"
#include "core/config-manager.h"
#include "core/http-resolve.h"

class QLineEdit;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;
namespace kai::ui { class InlineCodeField; }
class QStackedWidget;
class QPushButton;
class QLabel;
class QSpinBox;
class QToolButton;
class QListWidget;
class QTableWidget;
class QVBoxLayout;

namespace kai::ui {

class JsonSyntaxHighlighter;
class FolderPickerWidget;

class HttpBodyEditorWidget;
class HttpEntriesEditorWidget;
class HttpAuthEditorWidget;
class HttpOptionsEditorWidget;
class KeyValueEditorWidget;
class ParameterEditorWidget;
class OutputRespondersEditorWidget;
class HooksEditorWidget;
class ExecutionConditionsEditorWidget;
class EnvExtractorsEditorWidget;
class DeclaredEnvVarsEditorWidget;
class IconPickerWidget;
class WorkingDirField;
class CollapsibleSectionCard;

// Criação/edição de Command (Shell ou HTTP); o tipo é fixado pelo modo ativo no OK.
class CommandEditorDialog : public QDialog {
    Q_OBJECT

public:
    // `folderId`: pasta inicial do combo de destino. `commandsInSameFolder` alimenta o
    // HooksEditorWidget (sem o próprio comando). `allCommands` (opcional): universo completo, usado
    // só ao pesquisar hooks; omitido, a busca fica restrita a `commandsInSameFolder`.
    explicit CommandEditorDialog(const QString &folderId,
                                  const QVector<core::Command> &commandsInSameFolder,
                                  const QVector<core::Folder> &allFolders,
                                  QWidget *parent = nullptr,
                                  const core::Command *existingCommand = nullptr,
                                  const QVector<core::TerminalProfile> &terminalProfiles = {},
                                  const QVector<core::Command> &allCommands = {});

    core::Command buildCommand() const;

    // Coleções para parâmetros Select; chamar antes de exec().
    void setAvailableCollections(const QVector<core::Collection> &collections);

    // Variáveis dinâmicas capturadas (qualquer escopo), sugeridas no autocomplete de {{var}};
    // chamar antes de exec().
    void setAvailableDynamicVarNames(const QStringList &names) { m_availableDynamicVarNames = names; }

    // true se o usuário pediu o modo avançado: após Accepted, o chamador reabre o comando no
    // editor de JSON (via buildCommand()).
    bool switchToAdvancedRequested() const { return m_switchToAdvanced; }

    // Assistente de IA: o diálogo fecha com Accepted levando o comando proposto; quem abriu o editor o
    // reabre com ele como rascunho (nada é salvo antes do usuário confirmar).
    bool assistApplied() const { return m_assistApplied; }
    core::Command assistedCommand() const { return m_assisted; }

    static QString generateCommandId(const QString &folderId, const QString &name);

private slots:
    void handleAcceptRequested();
    void handleImportCurl();
    // Copia a requisição como curl ou código (formato: core::HttpExportFormat).
    void handleCopyRequestAs(int format);

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
    // Troca Shell/HTTP e sincroniza o segmented control (cobre trocas programáticas: comando
    // HTTP existente, cURL).
    void setExecutionMode(bool isHttp);
    void populateFolderCombo(const QVector<core::Folder> &allFolders, const QString &selectedFolderId);

    // Uma aba do sidebar por método (tabs/*.cpp), cada um chama addNavPage(); setupUi() só
    // orquestra a ordem e o que toca várias abas.
    void buildGeneralTab();
    QWidget *buildCommandTab();
    QWidget *buildHttpTab();
    // Aba "Configuração": CLI Path (sempre visível) + seções Shell-only em m_commandConfigContainer.
    void buildConfigurationTab();
    void buildKipTab();
    void refreshKipRemembered();
    void buildHeadersTab();
    void buildConnectionTab();
    void buildExtractorsTab();
    void buildDeclaredEnvVarsTab();
    void buildParamsTab();
    void buildRespondersTab();
    void buildConditionsTab();
    void buildHooksTab();
    void buildCliTab();
    // Aplica as exclusões de §15 aos controles conforme "Interface KIP".
    void updateKipExclusions();
    // Linguagem do comando (Nativa/Python/Node): mostra/esconde o interpretador,
    // troca placeholder e dica do campo de código e desabilita o que não vale.
    void updateLanguageUi();
    core::CommandLanguage currentLanguage() const;
    void populateEnvExtractorsEditor(const QVector<core::EnvExtractor> &extractors);
    // Formulário HTTP <-> HttpConfig (usado ao abrir, ao salvar e ao importar cURL).
    void applyHttpConfig(const core::HttpConfig &config);
    core::HttpConfig readHttpConfig() const;
    // Headers/autenticação que as pastas acima entregam ao comando; recalculado
    // quando a pasta destino muda.
    void refreshInheritedHttp();
    // Pasta escolhida no seletor (durante a construção, antes de ele ser preenchido, a pasta recebida).
    QString currentFolderId() const;
    QVector<core::EnvExtractor> readEnvExtractors() const;

    // Página nova no sidebar de abas (wrapper sobre addSidebarTabPage), chamável pelos tabs/*.cpp.
    QVBoxLayout *addNavPage(const QString &title, const QString &iconName);
    // Acrescenta "(N)" ao rótulo do nav quando a aba tem N>0 itens.
    void bindNavItemCount(int navRow, const QString &baseTitle, int count);

    QString m_folderId;
    QVector<core::Folder> m_allFolders;
    bool m_folderComboReady = false;
    QString m_existingId;
    QVector<core::TerminalProfile> m_terminalProfiles;
    // Não editáveis aqui; preservados ao salvar.
    QMap<QString, QString> m_lastParamValues;
    QJsonObject m_kipLastValues; // respostas KIP lembradas (idem lastParamValues)
    // Ordem manual de exibição; preservada ao editar.
    int m_existingOrder = -1;
    // Campos que este formulário não edita (menu "Ocultar", menu da saída, uso dos parâmetros):
    // preservados ao salvar.
    bool m_existingHidden = false;
    bool m_existingCompactOutput = false;
    QMap<QString, QStringList> m_existingParamUsageHistory;

    // Setado pelo botão do topo antes de accept().
    bool m_switchToAdvanced = false;
    bool m_assistApplied = false;
    core::Command m_assisted;
    QVector<core::Command> m_commandsForAi;

    // Corpo Shell/HTTP trocado pelo segmented control (m_commandModeButton/m_httpModeButton).
    QStackedWidget *m_tabWidget = nullptr;
    QPushButton *m_commandModeButton = nullptr;
    QPushButton *m_httpModeButton = nullptr;

    QLineEdit *m_nameField = nullptr;
    QLineEdit *m_cliPathField = nullptr;
    QCheckBox *m_cliInvocationDirCheck = nullptr;
    FolderPickerWidget *m_folderField = nullptr;
    IconPickerWidget *m_iconPicker = nullptr;
    QSpinBox *m_orderField = nullptr;
    HooksEditorWidget *m_hooksEditor = nullptr;
    ExecutionConditionsEditorWidget *m_conditionsEditor = nullptr;

    InlineCodeField *m_commandField = nullptr;
    QComboBox *m_languageField = nullptr;
    QLineEdit *m_interpreterField = nullptr;
    QWidget *m_interpreterRow = nullptr;
    QLabel *m_languageHint = nullptr;
    QLabel *m_kipModuleHint = nullptr;
    // Interpretadores globais (placeholder do campo por comando).
    core::InterpreterSettings m_globalInterpreters;
    InlineCodeField *m_descriptionField = nullptr;
    WorkingDirField *m_workingDirField = nullptr;
    // Working dir/detalhamento/flags Shell-only (CLI Path fica de fora: vale para os dois tipos).
    QWidget *m_commandConfigContainer = nullptr;
    QCheckBox *m_backgroundField = nullptr;
    QCheckBox *m_captureEnvField = nullptr;
    QCheckBox *m_openLastLinkField = nullptr;
    QCheckBox *m_hideOnRunField = nullptr;
    QCheckBox *m_ignoreExitCodeField = nullptr;
    QCheckBox *m_interactiveTerminalField = nullptr;
    QCheckBox *m_formattedOutputField = nullptr;
    // KIP (spec 11 §2/§15): liga a sessão KIP e, quando ligada, desabilita tudo
    // que compete com o stdin/stdout do protocolo.
    QCheckBox *m_kipField = nullptr;
    QCheckBox *m_kipWindowField = nullptr;
    QCheckBox *m_kipAutoCloseField = nullptr;
    QSpinBox *m_kipAutoCloseDelayField = nullptr;
    QLabel *m_kipRememberedLabel = nullptr;
    QPushButton *m_kipForgetButton = nullptr;
    QComboBox *m_terminalTargetField = nullptr;
    QWidget *m_terminalProfileFieldWrapper = nullptr;
    ParameterEditorWidget *m_paramsEditor = nullptr;
    QStringList m_availableDynamicVarNames; // ver setAvailableDynamicVarNames
    OutputRespondersEditorWidget *m_respondersEditor = nullptr;
    QWidget *m_respondersCard = nullptr; // aba inteira de auto-respostas (desabilitada com KIP)
    QCheckBox *m_autoRunField = nullptr;
    QSpinBox *m_autoRunDelayField = nullptr;
    QLineEdit *m_cronExpressionField = nullptr;
    QLabel *m_cronExpressionHintLabel = nullptr;
    QCheckBox *m_cronNotifyOnRunField = nullptr;
    // Params originais, reaplicados em setAvailableCollections sem perder o collectionId
    // (setParameters roda antes das coleções chegarem).
    QVector<core::Parameter> m_originalParams;

    QComboBox *m_methodField = nullptr;
    QLineEdit *m_urlField = nullptr;
    HttpBodyEditorWidget *m_bodyEditor = nullptr;
    CollapsibleSectionCard *m_headersCard = nullptr;
    HttpEntriesEditorWidget *m_headersEditor = nullptr;
    HttpEntriesEditorWidget *m_queryEditor = nullptr;
    HttpAuthEditorWidget *m_authEditor = nullptr;
    HttpOptionsEditorWidget *m_optionsEditor = nullptr;
    CollapsibleSectionCard *m_inheritedHeadersCard = nullptr;
    QTableWidget *m_inheritedHeadersTable = nullptr;
    QVector<core::InheritedHttpHeader> m_inheritedHeaders;
    QSet<QString> m_headersSwitchedOff; // nomes (minúsculos) de herdados que o comando já desligou
    // Resposta: asserções e arquivo de destino.
    ExecutionConditionsEditorWidget *m_assertionsEditor = nullptr;
    QLineEdit *m_responseFileField = nullptr;
    CollapsibleSectionCard *m_extractorsCard = nullptr;
    EnvExtractorsEditorWidget *m_envExtractorsEditor = nullptr;
    CollapsibleSectionCard *m_declaredEnvVarsCard = nullptr;
    DeclaredEnvVarsEditorWidget *m_declaredEnvVarsEditor = nullptr;

    // Sidebar de abas (mesmo padrão e objectName de QSS do SettingsDialog). m_navRow*: -1 se o
    // item de nav não existe.
    QListWidget *m_sideNav = nullptr;
    QStackedWidget *m_sidePages = nullptr;
    int m_navRowHeaders = -1;
    int m_navRowExtractors = -1;
    int m_navRowConnection = -1;
    int m_navRowDeclaredEnvVars = -1;
    int m_navRowConfiguration = -1;
    int m_navRowKip = -1;
};

} // namespace kai::ui
