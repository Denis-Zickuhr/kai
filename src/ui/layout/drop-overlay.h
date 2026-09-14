#pragma once

#include <QRect>
#include <QWidget>

#include "ui/layout/layout-model.h"

#include <functional>

namespace kai::ui::layout {

// Onde um painel arrastado cairia.
struct DropTarget {
    bool valid = false;
    Zone zone;
    // Posição entre as abas da zona (-1 = no fim).
    int index = -1;
    // O destino virtual "Flutuar": soltar aqui tira o painel do quadro para uma janela própria (`zone` não vale).
    bool floating = false;
    // Região a destacar, nas coordenadas do host.
    QRect rect;
};

// Camada transparente por cima da janela, visível SÓ durante o arrasto de um painel: destaca a célula onde
// ele vai cair. Desenhada com os tokens de tema e de raio. Só destaca; quem decide e aplica a queda é o host.
class DropOverlay : public QWidget {
    Q_OBJECT

public:
    using TargetFn = std::function<DropTarget(const QPoint &)>;
    // As células da grade (vazias inclusive) onde um painel pode ser solto, com o rótulo de cada uma.
    using EdgesFn = std::function<QList<std::pair<QRect, QString>>()>;

    explicit DropOverlay(QWidget *host);

    void setTargetProvider(TargetFn target, EdgesFn edges);
    // O alvo virtual de flutuar (uma pílula na janela): retângulo nas coordenadas do host e o rótulo. Vazio = sem alvo.
    void setFloatPill(const QRect &rect, const QString &label);
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
    QRect m_floatRect;
    QString m_floatLabel;
};

} // namespace kai::ui::layout
