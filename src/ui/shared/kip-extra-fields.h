#pragma once

#include "core/kip-protocol.h"

#include <QAbstractButton>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace kai::ui {

class DiffView;

namespace fields {

// ---- select com style "cards" / "segmented" (§27) --------------------------------------------------------------

// Um cartão de escolha: ícone, rótulo, descrição e o ✓ no canto quando escolhido.
class ChoiceCard : public QAbstractButton {
    Q_OBJECT

public:
    ChoiceCard(const core::KipOption &option, QWidget *parent = nullptr);
    const QString &value() const { return m_option.value; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    core::KipOption m_option;
    bool m_hover = false;
};

// Escolha única em cartões (2–4 opções com descrição) ou num controle segmentado (2–5 opções curtas). Setas mudam
// a escolha, como num grupo de rádios.
class ChoiceButtonsField : public QWidget {
    Q_OBJECT

public:
    ChoiceButtonsField(const QVector<core::KipOption> &options, bool segmented, const QString &initial,
                       QWidget *parent = nullptr);
    QString value() const;
    void setValue(const QString &value);
    QAbstractButton *button(const QString &value) const;
    QWidget *focusTarget() const;

signals:
    void valueChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QButtonGroup *m_group = nullptr;
    QVector<QAbstractButton *> m_buttons;
    QVector<QString> m_values;
};

// ---- tree (§27) --------------------------------------------------------------------------------------------------

// Árvore de escolha: com `multiple`, caixas tri-estado (marcar uma pasta marca os filhos); sem, seleção de um nó.
// Badge e ícone por nó; filtro acima quando a árvore é grande (ou `searchable`).
class TreeChoiceField : public QWidget {
    Q_OBJECT

public:
    TreeChoiceField(const core::KipField &field, const QJsonValue &initial, QWidget *parent = nullptr);
    QJsonValue value() const;
    void setValue(const QJsonValue &value);
    QTreeWidget *tree() const { return m_tree; }
    QLineEdit *filter() const { return m_filter; }

signals:
    void valueChanged();

private:
    void addNodes(QTreeWidgetItem *parent, const QVector<core::KipTreeNode> &nodes);
    void applyFilter(const QString &text);
    void updateCount();

    bool m_multiple = false;
    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_count = nullptr;
    bool m_applying = false;
};

// ---- diff como campo (§27) ------------------------------------------------------------------------------------

// O DiffView em modo de seleção + o resumo "2 de 4 trechos · +6 −2".
class DiffChoiceField : public QWidget {
    Q_OBJECT

public:
    DiffChoiceField(const core::KipField &field, const QJsonValue &initial, QWidget *parent = nullptr);
    QJsonValue value() const;
    void setValue(const QJsonValue &value);
    void setInteractive(bool interactive);
    DiffView *view() const { return m_view; }
    QLabel *summary() const { return m_summary; }

signals:
    void valueChanged();

private:
    void updateSummary();

    bool m_files = false;
    DiffView *m_view = nullptr;
    QLabel *m_summary = nullptr;
};

} // namespace fields
} // namespace kai::ui
