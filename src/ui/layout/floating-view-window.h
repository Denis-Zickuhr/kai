#pragma once

#include <QRect>
#include <QString>

#include "ui/shared/app-window-frame.h"

class QToolButton;
class QVBoxLayout;

namespace kai::ui::layout {

// A janela solta de um painel (flutuante): a moldura do app (igual à Saída destacada: a própria, ou a do sistema no
// Windows, onde dá o encaixe nas bordas e quadrantes da tela) com o painel dentro e dois botões — devolver ao quadro e
// fixar no topo. Não tem dono (uma janela com dono anda em grupo
// com ele no Windows e trazia o Kai todo para a frente a cada clique): quem esconde as flutuantes junto com o Kai é o
// DockHost, e elas só voltam com ele: nunca aparecem sozinhas.
class FloatingViewWindow : public kai::ui::AppWindowFrame {
    Q_OBJECT

public:
    // `styleSource`: a janela de onde o stylesheet (tema) é copiado; a flutuante não é filha dela.
    FloatingViewWindow(const QString &viewId, const QString &title, QWidget *styleSource);
    // Refaz a cópia do stylesheet (o tema ou a aparência mudaram).
    void syncStyleFrom(const QWidget *source);

    QString viewId() const { return m_viewId; }
    void setContent(QWidget *content);
    // Tira o painel da janela (para devolvê-lo ao quadro ou descartá-lo).
    QWidget *takeContent(QWidget *newParent);
    void setAlwaysOnTop(bool onTop);
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    void setViewTitle(const QString &title);
    QToolButton *dockButton() const { return m_dock; }
    QToolButton *pinButton() const { return m_pin; }

signals:
    void dockRequested(const QString &viewId);
    void closeRequested(const QString &viewId);
    void alwaysOnTopChanged(const QString &viewId, bool onTop);
    void geometryEdited(const QString &viewId, const QRect &geometry);

protected:
    void closeEvent(QCloseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updatePinIcon();

    QString m_viewId;
    QVBoxLayout *m_layout = nullptr;
    QWidget *m_content = nullptr;
    QToolButton *m_dock = nullptr;
    QToolButton *m_pin = nullptr;
    bool m_alwaysOnTop = false;
};

} // namespace kai::ui::layout
