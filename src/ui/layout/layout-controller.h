#pragma once

#include <QMap>
#include <QObject>
#include <QStringList>

#include "ui/layout/layout-model.h"

class QMenu;
class QWidget;

namespace kai::ui::layout {

class DockHost;
class LayoutStore;

// A cola entre o quadro de painéis e o resto do app: grava o layout (com debounce), guarda as perspectivas, monta o menu
// Exibir e cuida do foco por teclado (Alt+N, Ctrl+Tab). Mantém tudo isto fora do MainWindow, que só registra os painéis.
class LayoutController : public QObject {
    Q_OBJECT

public:
    LayoutController(DockHost *dock, LayoutStore *store, const QMap<QString, LayoutModel> &perspectives,
                     QWidget *dialogParent, QObject *parent = nullptr);

    DockHost *dock() const { return m_dock; }

    // As perspectivas lidas do layout.json (o arquivo é lido depois de o controlador existir).
    void setPerspectives(const QMap<QString, LayoutModel> &perspectives) { m_perspectives = perspectives; }

    // --- persistência
    void scheduleSave();
    void flush();
    void restoreDefault();

    // --- perspectivas: as embutidas (LayoutModel::Preset) e as que o usuário salvou
    QStringList savedPerspectiveNames() const;
    bool saveCurrentAs(const QString &name);
    bool deleteSaved(const QString &name);
    void applyPreset(LayoutModel::Preset preset);
    bool applySaved(const QString &name);
    const QMap<QString, LayoutModel> &savedPerspectives() const { return m_perspectives; }

    // --- menu Exibir (é repovoado a cada abertura)
    void populateViewMenu(QMenu *menu);

    // --- teclado
    // O painel que tem o foco agora (ou o da frente da primeira zona, se o foco está fora de todos).
    QString focusedViewId() const;
    // Os painéis visíveis, na ordem em que Alt+1..9 os numera: esquerda, centro, direita, embaixo, flutuantes.
    QStringList visibleViewOrder() const;
    void focusView(const QString &id);
    void focusPanelNumber(int number);
    void cycleFocus(bool forward);
    void toggleMaximizeFocused();

signals:
    void perspectivesChanged();

private:
    DockHost *m_dock;
    LayoutStore *m_store;
    QMap<QString, LayoutModel> m_perspectives;
    QWidget *m_dialogParent;
};

} // namespace kai::ui::layout
