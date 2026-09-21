#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace kai::ui {

// Seção "Manifestos para IA" da Ajuda: os guias que o usuário entrega a um
// assistente de IA para ele escrever um kai.json ou um programa KIP. Cada
// manifesto tem um botão que copia o texto inteiro para a área de transferência.
//
// Os arquivos são lidos em SEGUNDO PLANO (nunca na thread da interface) a partir
// de assets/manifesto/.
class AiManifestosPage : public QWidget {
    Q_OBJECT

public:
    struct Source {
        QString id;          // "kai_json" | "kip"
        QStringList files;   // arquivos de assets/manifesto/, concatenados na ordem
    };
    static const QVector<Source> &sources();

    explicit AiManifestosPage(QWidget *parent = nullptr);

    QPushButton *copyButton(const QString &id) const { return m_buttons.value(id); }
    QLabel *statusLabel(const QString &id) const { return m_status.value(id); }

    // Copia o manifesto `id` (lendo em segundo plano). Normalmente é o clique do botão.
    void copyManifesto(const QString &id);

signals:
    // Terminou: `ok` = o texto foi para a área de transferência.
    void copyFinished(const QString &id, bool ok);

private:
    QHash<QString, QPushButton *> m_buttons;
    QHash<QString, QLabel *> m_status;
    QHash<QString, QTimer *> m_resetTimers;
};

} // namespace kai::ui
