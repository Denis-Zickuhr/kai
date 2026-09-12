#pragma once

#include <QWidget>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

class QTableWidget;
class QToolButton;
class QLineEdit;
class QPushButton;

namespace kai::ui {

// Widget reutilizável de edição de pares chave-valor (env_vars de Folder/Global/
// Comando, headers e query de HTTP).
//
// PADRÃO ÚNICO DE TABELA (reformulado por feedback do usuário): tabela
// SOMENTE-LEITURA que apenas EXIBE os pares; a edição de uma linha é por
// FORMULÁRIO CONTEXTUAL caprichado (KeyValueRowDialog), acionado pelo lápis na
// barra de ações inferior (junto de adicionar/remover) ou por duplo-clique. O
// formulário traz um campo de VALOR EXPANSÍVEL (multi-linha) — valores de env
// costumam ser longos (URLs, tokens, JSON) — e, no modo secreto, um toggle de
// máscara. A fonte de verdade é m_rows; a tabela é derivada.
class KeyValueEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit KeyValueEditorWidget(QWidget *parent = nullptr,
                                   const QString &keyHeader = QStringLiteral("Chave"),
                                   const QString &valueHeader = QStringLiteral("Valor"));

    void setValues(const QMap<QString, QString> &values);

    // Retorna o mapa atual, ignorando linhas com chave vazia.
    QMap<QString, QString> values() const;

    // --- Modo "secreto" (opt-in, feature Variáveis Secretas) ---
    void enableSecretColumn(const QString &secretHeader = QStringLiteral("Secreta"));
    void setValuesWithSecrets(const QMap<QString, QString> &values, const QSet<QString> &secretKeys);
    QSet<QString> secretKeys() const;

    // Esconde o "+" próprio do widget (pedido do usuário: "deve ter o
    // botão de fora pra adicionar" — quando envolvido num
    // CollapsibleSectionCard, o "+Add" já vive no cabeçalho do card, e
    // ter os dois ao mesmo tempo é redundante). Chamar ANTES de mostrar o
    // widget; o padrão (true) preserva o comportamento de sempre pros
    // donos que NÃO usam card (Folder/Environment).
    void setShowOwnAddButton(bool show);

    // --- Extensões opt-in (revamp do EnvironmentManagerDialog) ---
    // Todas nascem DESLIGADAS (falso) para preservar o visual/comportamento
    // atual dos donos que não chamam nada disto (Folder/Command/Settings) —
    // ver nota em ui/environment-manager-dialog.cpp sobre por que a extensão
    // ficou aqui em vez de uma tabela local àquele diálogo.

    // Mostra a coluna de VALOR (por padrão escondida — só a chave aparece,
    // edição pelo formulário contextual). O mockup do Environment Manager
    // quer chave+valor lado a lado, estilo Postman/Insomnia.
    void setValueColumnVisible(bool visible);
    // Fonte monoespaçada nas colunas de chave/valor (tokens/URLs legíveis).
    void setMonospaceFont(bool mono);
    // Ícone de olho/olho-cortado por linha SECRETA, revelando o valor
    // mascarado sem abrir o formulário. Só tem efeito com
    // enableSecretColumn() ativo.
    void setSecretRevealEnabled(bool enabled);
    // Linha de "adição rápida" (borda tracejada, campos de chave/valor +
    // botão Salvar) fixada abaixo da tabela — alternativa rápida ao "+"
    // que abre o formulário contextual.
    void setInlineAddRowEnabled(bool enabled);

signals:
    // Ver ParameterEditorWidget::changed() — mesmo propósito (sincronizar
    // o badge de um CollapsibleSectionCard que envolva este widget).
    void changed();

public slots:
    // Público (era privado) — ver ParameterEditorWidget::handleAddRowClicked:
    // permite um "+Add" externo (cabeçalho de card) disparar o mesmo fluxo,
    // além do botão "+" próprio que este widget continua tendo (usado por
    // donos que NÃO o envolvem num card, ex: Folder/Environment).
    void handleAddRowClicked();

private:
    struct Row {
        QString key;
        QString value;
        bool secret = false;
    };

    void setupUi(const QString &keyHeader, const QString &valueHeader);
    void rebuildTable();
    // Abre o formulário contextual para a linha `row` do modelo. Retorna true
    // se confirmado (e m_rows[row] atualizado).
    bool editRow(int row);
    // Remove a linha `row` (ícone de lixeira inline).
    void removeRowAt(int row);
    // Índice da coluna de ações. A tabela tem sempre 3 colunas fixas
    // (chave, valor, ações) — o "modo secreto" (m_hasSecretColumn) não
    // adiciona mais uma coluna visível: o estado secreto de cada linha é
    // comunicado pelo valor mascarado + botão de olho, não por uma coluna
    // "Sim/—" própria (removida a pedido do usuário).
    int actionsColumn() const { return 2; }
    void updateActionsColumnWidth();
    void handleQuickAddSave();

    QTableWidget *m_table = nullptr;
    bool m_hasSecretColumn = false;
    QString m_keyHeader;
    QString m_valueHeader;
    QString m_secretHeader;
    QToolButton *m_addButton = nullptr;
    QWidget *m_buttonsRow = nullptr; // some inteiro via setShowOwnAddButton(false)
    QVector<Row> m_rows; // fonte de verdade

    // --- Extensões opt-in (ver setters acima) ---
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
