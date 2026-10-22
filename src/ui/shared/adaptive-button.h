#pragma once

#include <QPushButton>

namespace kai::ui {

// Botão com ícone e texto que vira só ícone quando o layout não dá a largura
// do texto. O texto some do desenho (continua no botão) e vai pro tooltip.
class AdaptiveButton : public QPushButton {
    Q_OBJECT

public:
    explicit AdaptiveButton(const QString &text, QWidget *parent = nullptr);

    QSize minimumSizeHint() const override;
    bool isCollapsed() const { return m_collapsed; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateCollapsed();

    bool m_collapsed{false};
    QString m_ownTip;
};

} // namespace kai::ui
