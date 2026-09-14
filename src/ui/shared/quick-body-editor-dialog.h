#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;
class QLabel;

namespace kai::ui {

class JsonSyntaxHighlighter;

// Editor de Body de comandos HTTP — editor de JSON moderno (feedback do
// usuário): realce de sintaxe, validação ao vivo (✓/✗ com a mensagem do
// erro), fonte monoespaçada e ações Formatar/Minificar. Não abre o
// CommandEditorDialog completo.
class QuickBodyEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickBodyEditorDialog(const QString &commandName, const QString &initialBody, QWidget *parent = nullptr);

    QString body() const;

private slots:
    void handleFormatJsonRequested();
    void handleMinifyRequested();
    void handleValidateLive();

private:
    void setupUi(const QString &commandName, const QString &initialBody);

    QPlainTextEdit *m_bodyField = nullptr;
    QLabel *m_statusLabel = nullptr;
    JsonSyntaxHighlighter *m_highlighter = nullptr;
};

} // namespace kai::ui
