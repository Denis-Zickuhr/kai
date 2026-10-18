#pragma once

#include <QRect>
#include <QWidget>

#include "ui/layout/layout-model.h"

#include <functional>

namespace kai::ui::layout {

// Onde um painel arrastado cairia.
struct DropTarget {
    bool valid = false;
    Zone zone = Zone::Center;
    // Posição entre as abas da zona (-1 = no fim).
    int index = -1;
    // Região a destacar, nas coordenadas do host.
    QRect rect;
};

// Camada transparente por cima da janela, visível SÓ durante o arrasto de um painel: destaca a zona (ou a borda vazia) onde
// ele vai cair. Desenhada com os tokens de tema e de raio. Só destaca; quem decide e aplica a queda é o host.
class DropOverlay : public QWidget {
    Q_OBJECT

public:
    using TargetFn = std::function<DropTarget(const QPoint &)>;
    // As bordas vazias (esquerda/direita/baixo) onde um painel pode ser solto, com o rótulo de cada uma.
    using EdgesFn = std::function<QList<std::pair<QRect, QString>>()>;

    explicit DropOverlay(QWidget *host);

    void setTargetProvider(TargetFn target, EdgesFn edges);
    const DropTarget &current() const { return m_target; }

signals:
    // Soltou sobre um alvo válido (a aplicação é adiada para depois de o arrasto terminar).
    void dropped(const QString &viewId, const DropTarget &target);

protected:
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setTarget(const DropTarget &target);

    TargetFn m_targetFn;
    EdgesFn m_edgesFn;
    DropTarget m_target;
};

} // namespace kai::ui::layout
