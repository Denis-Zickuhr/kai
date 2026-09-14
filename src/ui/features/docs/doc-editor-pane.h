#pragma once

#include "ui/features/docs/doc-text-tools.h"

#include <QWidget>

class QLabel;

namespace kai::ui {

class DocCodeEditor;

// O editor com a barra de estado embaixo: "Ln 3, Col 5", a linguagem e o veredito da validação (clicar no erro leva até a
// linha). Também mostra mensagens rápidas das ferramentas ("JSON válido", "Não foi possível formatar: ...").
class DocEditorPane : public QWidget {
    Q_OBJECT

public:
    explicit DocEditorPane(QWidget *parent = nullptr);

    DocCodeEditor *editor() const { return m_editor; }
    // Põe o texto no editor (sem contar como alteração e sem histórico de desfazer) na linguagem dada.
    void setContent(const QString &text, texttools::Language language);
    QString text() const;
    bool isModified() const;
    // O texto atual passou a ser o "salvo".
    void markSaved();
    // Mensagem rápida na barra de estado (erro em vermelho); some ao próximo movimento do cursor.
    void showMessage(const QString &text, bool error);
    void refreshStyle();

    QString statusText() const;

signals:
    void modifiedChanged(bool modified);

private:
    void updateStatus();

    DocCodeEditor *m_editor = nullptr;
    QLabel *m_position = nullptr;
    QLabel *m_language = nullptr;
    QLabel *m_issue = nullptr;
    QString m_message;
    bool m_messageIsError = false;
};

} // namespace kai::ui
