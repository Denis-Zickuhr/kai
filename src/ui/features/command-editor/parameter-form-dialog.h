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

// Preenche parâmetros dinâmicos antes de executar um Command: um campo por core::Parameter conforme
// o ParameterType (Text=QLineEdit, Select=QComboBox, Bool=QCheckBox, File=QLineEdit + QFileDialog).
class ParameterFormDialog : public QDialog {
    Q_OBJECT

public:
    // `lastValues` (opcional) pré-preenche com os últimos valores usados (prioridade sobre defaultValue).
    // `usageHistory` (opcional): por parâmetro, valores do mais recente ao mais antigo; em Select, os
    // mais usados vão ao topo e o combo ganha busca. updatedUsageHistory() devolve o histórico para o
    // chamador persistir.
    // `collectionFilters` (opcional): busca + favoritos por Collection::id, reaplicados na tela de
    // seleção de cada param ligado a coleção; updatedCollectionFilters() devolve o mapa
    // (persistir via ConfigManager::saveCollectionFilters).
    explicit ParameterFormDialog(const QVector<core::Parameter> &params, QWidget *parent = nullptr,
                                  const QMap<QString, QString> &lastValues = {},
                                  const QMap<QString, QStringList> &usageHistory = {},
                                  const QVector<core::Collection> &collections = {},
                                  const QString &description = {},
                                  const QMap<QString, core::CollectionFilterState> &collectionFilters = {});

    // nome_da_variável -> valor, aplicado como "Parâmetros do Formulário" (precedência mais alta, spec 03).
    QMap<QString, QString> values() const;

    // Histórico atualizado: o valor recém-escolhido sobe ao topo, sem duplicatas. O chamador persiste no Command.
    QMap<QString, QStringList> updatedUsageHistory() const;

    // Coleções possivelmente atualizadas (toggles de favorito feitos na
    // tela de seleção). O chamador persiste se collectionsChanged() for true.
    QVector<core::Collection> updatedCollections() const { return m_collections; }
    bool collectionsChanged() const { return m_collectionsChanged; }

    // Busca + favoritos-only de cada coleção aberta nesta sessão (as demais mantêm o valor do construtor).
    QMap<QString, core::CollectionFilterState> updatedCollectionFilters() const { return m_collectionFilters; }

    // true só quando todo parâmetro obrigatório tem valor não-vazio; habilita o OK e recalcula os
    // badges de grupo. Público para teste direto.
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
    // Recalcula o botão OK e os badges "pendente" de cada grupo; ligado ao sinal de mudança de todo campo.
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
    // Param com fonte de coleção: id(s) escolhido(s) na tela de seleção, por nome (suporta multi).
    QMap<QString, QStringList> m_collectionSelectionByParam;
    bool m_collectionsChanged = false;
    QMap<QString, core::CollectionFilterState> m_collectionFilters;
    // Checkbox "Informar <label>?" dos parâmetros opcionais, para lembrar a última escolha
    // (kOptionalEnabledKeyPrefix no .cpp).
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
    // Multi-select de opções fixas: filtro por parâmetro, focado ao expandir o GRUPO que o contém.
    QMap<QString, QLineEdit *> m_multiSelectFilterByParamName;
    // Parâmetros-script: onde o campo de verdade entra, a linha de erro e o botão de recarregar.
    QMap<QString, QWidget *> m_scriptSlotByParamName;
    QMap<QString, QLabel *> m_scriptStatusByParamName;
};

} // namespace kai::ui
