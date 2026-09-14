#pragma once

#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QVector>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;

namespace kai::ui {

// Ajuda → Manifesto de criação: janela própria com um guia por aba (o do kai.yml e o do KIP), os
// textos que o usuário entrega a um assistente de IA para ele escrever um arquivo do Kai ou um
// programa KIP. Cada aba mostra o texto EXATO que vai para a área de transferência e tem um botão
// que o copia.
//
// Os arquivos de assets/manifesto/ são lidos em SEGUNDO PLANO (nunca na thread da interface).
class ManifestoDialog : public QDialog {
    Q_OBJECT

public:
    struct Source {
        QString id;          // "kai" | "kip"
        QStringList files;   // arquivos de assets/manifesto/, concatenados na ordem
    };
    static const QVector<Source> &sources();

    explicit ManifestoDialog(QWidget *parent = nullptr);

    QPushButton *copyButton(const QString &id) const { return m_buttons.value(id); }
    QLabel *statusLabel(const QString &id) const { return m_status.value(id); }
    QPlainTextEdit *preview(const QString &id) const { return m_previews.value(id); }
    // O texto já carregado do manifesto `id` (vazio até terminar a leitura).
    QString text(const QString &id) const { return m_texts.value(id); }
    QTabWidget *tabs() const { return m_tabs; }

    // Copia o manifesto `id` (já carregado) para a área de transferência. Normalmente é o clique do botão.
    void copyManifesto(const QString &id);

signals:
    // A leitura em segundo plano terminou: `ok` = o texto está disponível.
    void loaded(const QString &id, bool ok);
    // `ok` = o texto foi para a área de transferência.
    void copyFinished(const QString &id, bool ok);

private:
    void load(const QString &id);
    void handleLoaded(const QString &id, const QString &text);

    QTabWidget *m_tabs = nullptr;
    QHash<QString, QPushButton *> m_buttons;
    QHash<QString, QLabel *> m_status;
    QHash<QString, QPlainTextEdit *> m_previews;
    QHash<QString, QTimer *> m_resetTimers;
    QHash<QString, QString> m_texts;
};

} // namespace kai::ui
