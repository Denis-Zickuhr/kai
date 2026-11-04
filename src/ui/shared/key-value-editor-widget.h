#pragma once

#include <QWidget>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

class QTableWidget;
class QToolButton;
class QStackedWidget;
class QLabel;
class QLineEdit;
class QPushButton;

namespace kai::ui {

// Edição de pares chave-valor (env_vars de Folder/Global/Comando, headers e query de HTTP). A tabela é
// somente-leitura; a edição é pelo KeyValueRowDialog (lápis ou duplo-clique), com valor multi-linha e, no modo
// secreto, toggle de máscara. A fonte de verdade é m_rows; a tabela é derivada.
class KeyValueEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit KeyValueEditorWidget(QWidget *parent = nullptr,
                                   const QString &keyHeader = QStringLiteral("Chave"),
                                   const QString &valueHeader = QStringLiteral("Valor"));

    void setValues(const QMap<QString, QString> &values);

    QMap<QString, QString> values() const;

    void enableSecretColumn(const QString &secretHeader = QStringLiteral("Secreta"));
    void setValuesWithSecrets(const QMap<QString, QString> &values, const QSet<QString> &secretKeys);
    QSet<QString> secretKeys() const;

    // Acrescenta pares (ou troca o valor de quem já existe, mantendo a marca de secreto). Devolve quantos entraram.
    int mergeValues(const QMap<QString, QString> &values);

    // Esconde o "+" próprio do widget (quando dentro de um CollapsibleSectionCard o "+Add" já está no cabeçalho do
    // card). Chamar antes de mostrar; o padrão (true) serve aos donos sem card (Folder/Environment).
    void setShowOwnAddButton(bool show);

    // Extensões opt-in, todas desligadas por padrão para não mudar os donos que não as usam (Folder/Command/Settings).

    // Mostra a coluna de valor (por padrão só a chave aparece).
    void setValueColumnVisible(bool visible);
    // Fonte monoespaçada nas colunas de chave/valor (tokens/URLs legíveis).
    void setMonospaceFont(bool mono);
    // Ícone de olho por linha secreta, revelando o valor mascarado sem abrir o formulário. Só com enableSecretColumn().
    void setSecretRevealEnabled(bool enabled);
    // Linha de adição rápida (borda tracejada, campos de chave/valor + Salvar) abaixo da tabela.
    void setInlineAddRowEnabled(bool enabled);

signals:
    // Como ParameterEditorWidget::changed(): sincroniza o badge de um CollapsibleSectionCard que envolva este widget.
    void changed();

public slots:
    // Público para um "+Add" externo (cabeçalho de card) disparar o mesmo fluxo; ver ParameterEditorWidget::handleAddRowClicked.
    void handleAddRowClicked();

private:
    struct Row {
        QString key;
        QString value;
        bool secret = false;
    };

    void setupUi(const QString &keyHeader, const QString &valueHeader);
    void rebuildTable();
    // Abre o formulário para a linha `row` do modelo; true se confirmado (m_rows[row] atualizado).
    bool editRow(int row);
    void removeRowAt(int row);
    // Colunas fixas (chave, valor, ações): o modo secreto não adiciona coluna, o estado vem do valor mascarado + olho.
    int actionsColumn() const { return 2; }
    void updateActionsColumnWidth();
    void handleQuickAddSave();

    QTableWidget *m_table = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_emptyLabel = nullptr;
    bool m_hasSecretColumn = false;
    QString m_keyHeader;
    QString m_valueHeader;
    QString m_secretHeader;
    QToolButton *m_addButton = nullptr;
    QWidget *m_buttonsRow = nullptr; // some inteiro via setShowOwnAddButton(false)
    QVector<Row> m_rows; // fonte de verdade

    bool m_valueColumnVisible = false;
    bool m_monospaceFont = false;
    bool m_secretRevealEnabled = false;
    QSet<QString> m_revealedKeys; // chaves secretas reveladas manualmente na sessão
    QWidget *m_quickAddRow = nullptr;
    QLineEdit *m_quickAddKey = nullptr;
    QLineEdit *m_quickAddValue = nullptr;
    QPushButton *m_quickAddSave = nullptr;
};

} // namespace kai::ui
