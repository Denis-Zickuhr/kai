#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <QVector>
#include <QPair>
#include <QBoxLayout>

class QToolButton;

namespace kai::ui {

// Formas de ícone desenhadas vetorialmente (nítidas em qualquer DPI e recoloríveis pelo tema).
enum class ActionIconShape {
    Play, Stop, ForceStop, Reset, EditBody, ShowOutput, ShowDocument
};

// Grupo de ações "Execução" do item selecionado na árvore: Executar, Parar, Forçar Parada, Resetar e Editar body
// (rápido, só HTTP). Os botões ficam sempre visíveis e só são habilitados quando a linha permite; o chamador informa
// a seleção via setRowContext() e recebe as ações por sinais. Usável nos dois containers (horizontal ou vertical)
// via setOrientation().
class ActionSidebar : public QWidget {
    Q_OBJECT

public:
    explicit ActionSidebar(QWidget *parent = nullptr);

    // Contexto da linha selecionada, habilitando as row actions conforme tipo e estado; `hasSelection` false =
    // tudo desabilitado.
    void setRowContext(bool hasSelection, bool isCommand, bool isRunning, bool hasFailed, bool isHttp = false);
    // Sem o foco inteligente a seleção não traz a Saída nem o Documento: estes dois botões trazem (`visible` false os
    // esconde, com o foco inteligente ligado).
    void setFocusActions(bool visible, bool canShowOutput, bool canShowDocument);
    QToolButton *showOutputButton() const { return m_showOutputButton; }
    QToolButton *showDocumentButton() const { return m_showDocumentButton; }

    // Recolore os ícones neutros (reset) com o accent do tema; play/stop/kill mantêm cores semânticas fixas.
    void applyAccentColor(const QColor &accent);

    // Alterna entre coluna (side bar) e linha (upper bar).
    void setOrientation(Qt::Orientation orientation);

signals:
    // Row actions do item selecionado (o MainWindow resolve o id via CommandTreeWidget::currentSelectionId).
    void playSelectedRequested();
    void stopSelectedRequested();
    void forceStopSelectedRequested();
    void resetSelectedRequested();
    // Editar SÓ o body (rápido) do comando HTTP selecionado.
    void editBodySelectedRequested();
    void showOutputSelectedRequested();
    void showDocumentSelectedRequested();

private:
    void setupUi();

    QBoxLayout *m_layout{nullptr};
    QColor m_accent{189, 147, 249}; // accent padrão (Dracula) até o tema carregar
    QVector<QPair<QToolButton *, ActionIconShape>> m_neutralButtons;

    QToolButton *m_playButton = nullptr;
    QToolButton *m_stopButton = nullptr;
    QToolButton *m_forceStopButton = nullptr;
    QToolButton *m_resetButton = nullptr;
    QToolButton *m_editBodyButton = nullptr;
    QToolButton *m_showOutputButton = nullptr;
    QToolButton *m_showDocumentButton = nullptr;
};

} // namespace kai::ui
