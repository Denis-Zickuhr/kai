#pragma once

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QVector>

#include "core/models.h"

class QCheckBox;
class QLineEdit;
class QPushButton;

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
    explicit ParameterFormDialog(const QVector<core::Parameter> &params, QWidget *parent = nullptr,
                                  const QMap<QString, QString> &lastValues = {},
                                  const QMap<QString, QStringList> &usageHistory = {},
                                  const QVector<core::Collection> &collections = {},
                                  const QString &description = {});

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

    // Diretrizes da tela de Params (pedido do usuário): true só quando todo
    // parâmetro OBRIGATÓRIO (!optional) tem um valor não-vazio no momento —
    // usado pra habilitar/desabilitar o botão OK e recalcular os badges de
    // grupo. Público (não só usado internamente) para dar cobertura de
    // teste direta, sem depender de introspecção de widget.
    bool allRequiredFieldsFilled() const;

protected:
    // Diretriz da tela de Params: "Espaço" marca/desmarca o item focado
    // numa lista de múltipla seleção sem precisar do mouse. QListWidget não
    // faz isso sozinho — instalado como filtro em cada QListWidget de
    // multi-select criado em setupUi().
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi(const QVector<core::Parameter> &params);
    // Recalcula o estado do botão OK e os badges de contagem "pendente" de
    // cada grupo — conectado ao sinal de mudança de TODO campo do form (ver
    // fim de setupUi) e chamado uma vez no fim da montagem pro estado
    // inicial já vir correto.
    void refreshValidationState();
    // true se `p` conta como "preenchido" pro botão OK / badge de grupo.
    // Regra geral: valor não-vazio. EXCEÇÃO (achado real, com print): um
    // Select de opção única sempre chega com ALGUM valor pré-selecionado
    // (a 1ª opção, se não houver default/lastValue) — contar isso como
    // "preenchido" fazia o badge de um grupo com 2 obrigatórios mostrar "1"
    // pendente, mesmo que o usuário nunca tivesse tocado no select. Só
    // conta preenchido depois de um toggle/seleção de verdade nesta sessão
    // (ver m_touchedParamNames).
    bool isParamFilled(const core::Parameter &p, const QMap<QString, QString> &vals) const;
    void handleBrowseFileClicked(QLineEdit *targetField, const QString &initialDir = QString(),
                                 const QString &pathFormat = QStringLiteral("native"),
                                 bool pickFolder = false);

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
    // Parâmetros OPCIONAIS (Parameter::optional): a checkbox "Informar
    // <label>?" de cada um, pra lembrar se estava marcada da última vez
    // (pedido do usuário: "o sistema deve lembrar da opção selecionada se
    // foi sim ou não pro param opcional") — ver setupUi/values() e
    // kOptionalEnabledKeyPrefix no .cpp.
    QMap<QString, QCheckBox *> m_optionalCheckboxByParamName;
    QPushButton *m_okButton = nullptr;
    // Multi-select de opções fixas: campo de filtro por nome de parâmetro —
    // usado pra dar foco automático nele quando o GRUPO que o contém é
    // expandido (diretriz do usuário: "ao abrir... o foco deve ir
    // automaticamente para o campo de texto 'Filtrar opções...'").
    QMap<QString, QLineEdit *> m_multiSelectFilterByParamName;
    // Nomes de parâmetro Select (opção única) que o usuário já
    // efetivamente escolheu algo nesta sessão — ver isParamFilled().
    QSet<QString> m_touchedParamNames;
};

} // namespace kai::ui
