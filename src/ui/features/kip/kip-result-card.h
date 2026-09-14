#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

class QLabel;
class QPushButton;

namespace kai::ui {

class FlowLayout;

// Cartão de resultado de uma sessão KIP: o `done` do programa (§12.1) e os
// estados terminais — Unsupported, ProtocolError, Failed, Cancelled, Finished
// (§13.2). Ícone num selo tingido, título, texto e uma fileira de botões.
class KipResultCard : public QWidget {
    Q_OBJECT

public:
    explicit KipResultCard(QWidget *parent = nullptr);

    void setContent(const QColor &color, const QString &iconName, const QString &title, const QString &text);
    // Botão na fileira de ações; `primary` usa o estilo de destaque do app.
    QPushButton *addButton(const QString &text, const QString &iconName = QString(), bool primary = false);
    void clearButtons();
    QList<QPushButton *> buttons() const { return m_buttons; }

    QString titleText() const;
    QString bodyText() const;

private:
    QWidget *m_badge = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_text = nullptr;
    QWidget *m_buttonHost = nullptr;
    FlowLayout *m_buttonLayout = nullptr;
    QList<QPushButton *> m_buttons;
};

} // namespace kai::ui
