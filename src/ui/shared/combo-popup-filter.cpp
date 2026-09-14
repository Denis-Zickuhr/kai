#include "ui/shared/combo-popup-filter.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QMenu>

namespace kai::ui {

void ComboPopupFilter::style(QComboBox *combo)
{
    if (!combo || combo->property("kaiPopupStyled").toBool()) {
        return;
    }
    QAbstractItemView *view = combo->view();
    QWidget *container = view ? view->window() : nullptr;
    if (!container || container == combo->window()) {
        return;
    }
    container->setWindowFlags(container->windowFlags() | Qt::FramelessWindowHint
                              | Qt::NoDropShadowWindowHint);
    container->setAttribute(Qt::WA_TranslucentBackground, true);
    combo->setProperty("kaiPopupStyled", true);
}

bool ComboPopupFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Polish) {
        if (auto *menu = qobject_cast<QMenu *>(watched)) {
            // Menu popup (a janela nativa é quadrada): sem moldura/sombra do sistema e com fundo
            // transparente, só o retângulo arredondado do QSS é desenhado.
            if (!menu->property("kaiPopupStyled").toBool()) {
                menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
                menu->setAttribute(Qt::WA_TranslucentBackground, true);
                menu->setProperty("kaiPopupStyled", true);
            }
        } else if (auto *combo = qobject_cast<QComboBox *>(watched)) {
            style(combo);
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace kai::ui
