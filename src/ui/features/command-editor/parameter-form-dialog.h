#pragma once

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QVector>

#include "core/models.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;

namespace kai::ui {

class CollapsibleSectionCard;

// Diálogo de preenchimento de parâmetros dinâmicos antes da execução de um
// Command parametrizado. Gera um campo de UI
// apropriado para cada core::Parameter conforme seu ParameterType:
//   - Text   -> QLineEdit
//   - Select -> QComboBox (populado com Parameter::options)
//   - Bool   -> QCheckBox
//   - File   -> QLineEdit + QToolButton (abre QFileDialog::getOpenFileName)
class ParameterFormDialog : public QDialog {
    Q_OBJECT

public:
    // `lastValues` (opcional) pré-preenche os campos com os últimos
    // valores informados numa execução anterior (feedback do
    // usuário: salvar os últimos parâmetros de texto/flag). Tem prioridade
    // sobre o defaultValue do parâmetro.
    //
    // `usageHistory` (opcional) traz, por parâmetro, os valores usados em
    // ordem do mais recente para o mais antigo. Para parâmetros Select, as
    // opções são reordenadas com os valores mais usados no topo e o combo
    // ganha busca (editable + completer). Após aceitar, updatedUsageHistory()
    // devolve o histórico atualizado para o chamador persistir.
    // `collectionFilters` (opcional): busca + favoritos salvos por
    // Collection::id (ver core::CollectionFilterState), reaplicados ao
    // abrir a tela de seleção dedicada de cada param ligado a uma coleção.
    // Após aceitar, updatedCollectionFilters() devolve o mapa atualizado
    // para o chamador persistir via ConfigManager::saveCollectionFilters.
    explicit ParameterFormDialog(const QVector<core::Parameter> &params, QWidget *parent = nullptr,
                                  const QMap<QString, QString> &lastValues = {},
                                  const QMap<QString, QStringList> &usageHistory = {},
                                  const QVector<core::Collection> &collections = {},
                                  const QString &description = {},
                                  const QMap<QString, core::CollectionFilterState> &collectionFilters = {});

    // Retorna o mapa nome_da_variável -> valor preenchido, pronto para ser
    // aplicado como "Parâmetros do Formulário" no EnvironmentManager
    // (precedência mais alta, spec 03).
    QMap<QString, QString> values() const;

    // Histórico de uso atualizado após o preenchimento: para cada
    // parâmetro, promove o valor recém-escolhido para o topo da lista
    // (mais recente), sem duplicatas. O chamador persiste isto no Command.
    QMap<QString, QStringList> updatedUsageHistory() const;

    // Coleções possivelmente atualizadas (toggles de favorito feitos na
    // tela de seleção). O chamador persiste se collectionsChanged() for true.
    QVector<core::Collection> updatedCollections() const { return m_collections; }
    bool collectionsChanged() const { return m_collectionsChanged; }

    // Busca + favoritos-only atualizados de cada coleção cujo seletor foi
    // aberto nesta sessão (coleções nunca abertas mantêm o valor recebido
    // no construtor) — o chamador persiste via
    // ConfigManager::saveCollectionFilters.
    QMap<QString, core::CollectionFilterState> updatedCollectionFilters() const { return m_collectionFilters; }

    // Diretrizes da tela de Params (pedido do usuário): true só quando todo
    // parâmetro OBRIGATÓRIO (!optional) tem um valor não-vazio no momento —
    // usado pra habilitar/desabilitar o botão OK e recalcular os badges de
    // grupo. Público (não só usado internamente) para dar cobertura de
    // teste direta, sem depender de introspecção de widget.
    bool allRequiredFieldsFilled() const;

    // Por que o OK está bloqueado: um item por parâmetro obrigatório ainda sem valor (nome do campo + motivo).
    struct Problem {
        QString paramName;
        QString label;
        QString reason;
        int step = 0;
    };
    QVector<Problem> validationProblems() const;

    // Etapas (Parameter::step): quantas páginas o formulário tem (1 = sem etapas) e qual está aberta (0-based).
    int stepCount() const { return qMax(1, int(m_stepNames.size())); }
    int currentStep() const { return m_currentStep; }

    // Parâmetros Select com `optionsScript`: o dono do diálogo escuta optionsScriptRequested (sem isso o
    // campo fica em "carregando"), roda o script no contexto do comando e devolve as linhas de stdout (ou a
    // mensagem de erro) aqui. Pode ser chamado de novo — o botão de recarregar do campo refaz o pedido.
    void setScriptOptions(const QString &paramName, const QStringList &lines, const QString &errorMessage);

signals:
    void optionsScriptRequested(const QString &paramName, const QString &script);

private:
    void requestScriptOptions(const QString &paramName);
    // Troca o conteúdo do campo de um parâmetro-script por um combo desabilitado com `text`.
    void showScriptPlaceholder(const QString &paramName, const QString &text);
    void setupUi(const QVector<core::Parameter> &params);
    // Recalcula o estado do botão OK e os badges de contagem "pendente" de
    // cada grupo — conectado ao sinal de mudança de TODO campo do form (ver
    // fim de setupUi) e chamado uma vez no fim da montagem pro estado
    // inicial já vir correto.
    void refreshValidationState();
    void focusParam(const QString &paramName);
    void goToStep(int index);
    // true se `p` conta como "preenchido" pro botão OK. Um select obrigatório sem default/último valor
    // válido abre num item vazio ("Escolha uma opção"), então o valor vazio já diz que falta escolher.
    bool isParamFilled(const core::Parameter &p, const QMap<QString, QString> &vals) const;

    QVector<core::Parameter> m_params;
    QMap<QString, QString> m_lastValues;
    QMap<QString, QStringList> m_usageHistory;
    QVector<core::Collection> m_collections;
    QString m_description; // texto de Detalhamento do comando (hint do form)
    QMap<QString, QWidget *> m_fieldByParamName;
    // Para params com fonte de coleção: id(s) da(s) entrada(s) escolhida(s)
    // na tela de seleção dedicada, por nome de parâmetro (suporta multi).
    QMap<QString, QStringList> m_collectionSelectionByParam;
    bool m_collectionsChanged = false;
    QMap<QString, core::CollectionFilterState> m_collectionFilters;
    // Parâmetros OPCIONAIS (Parameter::optional): a checkbox "Informar
    // <label>?" de cada um, pra lembrar se estava marcada da última vez
    // (pedido do usuário: "o sistema deve lembrar da opção selecionada se
    // foi sim ou não pro param opcional") — ver setupUi/values() e
    // kOptionalEnabledKeyPrefix no .cpp.
    QMap<QString, QCheckBox *> m_optionalCheckboxByParamName;
    QPushButton *m_okButton = nullptr;
    QLabel *m_problemsLabel = nullptr;
    // Etapas: nomes na ordem da 1ª aparição, página (índice) de cada parâmetro, e os widgets da navegação.
    QStringList m_stepNames;
    QMap<QString, int> m_stepByParamName;
    int m_currentStep = 0;
    QStackedWidget *m_stepStack = nullptr;
    QVector<QScrollArea *> m_pages;
    QVector<QPushButton *> m_stepChips;
    QPushButton *m_backButton = nullptr;
    QString m_okText;
    // Multi-select de opções fixas: campo de filtro por nome de parâmetro —
    // usado pra dar foco automático nele quando o GRUPO que o contém é
    // expandido (diretriz do usuário: "ao abrir... o foco deve ir
    // automaticamente para o campo de texto 'Filtrar opções...'").
    QMap<QString, QLineEdit *> m_multiSelectFilterByParamName;
    // Parâmetros-script: onde o campo de verdade entra, a linha de erro e o botão de recarregar.
    QMap<QString, QWidget *> m_scriptSlotByParamName;
    QMap<QString, QLabel *> m_scriptStatusByParamName;
};

} // namespace kai::ui
