#pragma once

#include <QPointer>
#include <QRect>
#include <QString>
#include <QStyledItemDelegate>
#include <QVector>
#include <functional>

class QTreeWidget;
class QTreeWidgetItem;

namespace kai::ui {

// Desenha, na linha do item, os botões de ação na borda direita da coluna do nome (Settings → Ações inline), sempre na
// mesma posição: executar/parar é o do canto e os outros se somam à esquerda dele. Os botões acompanham o estado do item
// (executar ou parar, reiniciar, body, saída, documento) e só aparecem na linha sob o mouse ou na selecionada. É o delegate da coluna do nome e um filtro de eventos do viewport: o clique num botão é consumido aqui,
// então nem seleciona a linha nem inicia um arrasto.
class InlineRowActions : public QStyledItemDelegate {
    Q_OBJECT
public:
    enum class Kind { Play, Stop, Reset, EditBody, Output, Document };

    // Os botões da linha, na ordem em que aparecem (vazio: linha sem ação).
    using Provider = std::function<QVector<Kind>(const QTreeWidgetItem *)>;

    InlineRowActions(QTreeWidget *tree, Provider provider);

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    static constexpr int kBox = 20;
    static constexpr int kIcon = 14;
    static constexpr int kGap = 2;
    static constexpr int kLeadGap = 6;       // entre o nome e o botão mais à esquerda
    static constexpr int kEdgeGap = 4;       // entre o primeiro botão e a borda da coluna
    static constexpr int kMinNameWidth = 48; // o nome sempre guarda este espaço, os botões que não cabem somem

    static QString iconName(Kind kind);
    static QString tooltipKey(Kind kind);

    struct Hit {
        bool valid = false;
        Kind kind = Kind::Play;
        const QTreeWidgetItem *item = nullptr;
        QRect rect;
    };
    Hit hitTest(const QPoint &viewportPos) const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

signals:
    void activated(const QTreeWidgetItem *item, Kind kind);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool rowShowsButtons(const QTreeWidgetItem *item) const;
    // Retângulos dos botões de `item` (o primeiro na borda direita da coluna); só os que cabem.
    QVector<QRect> buttonRects(const QTreeWidgetItem *item, int count) const;

    QTreeWidget *m_tree;
    Provider m_provider;
    bool m_enabled = false;
    const QTreeWidgetItem *m_hoverItem = nullptr;
    Hit m_hover;
    Hit m_pressed;
};

} // namespace kai::ui
