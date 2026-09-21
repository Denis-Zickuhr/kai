#pragma once

#include <QLayout>
#include <QList>
#include <QStyle>

namespace kai::ui {

// Layout genérico que empilha os itens em LINHA, quebrando pra próxima
// linha quando não há mais espaço horizontal (padrão de tags/chips) — Qt
// não tem um equivalente pronto (QHBoxLayout nunca quebra linha). Usado
// pela primeira vez por CollectionChipPickerWidget (chips de entradas de
// coleção escolhidas + campo de busca), mas é genérico o bastante pra
// qualquer UI futura de tags/chips reaproveitar.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> m_items;
    int m_hSpace;
    int m_vSpace;
};

} // namespace kai::ui
