#include "ui/action-group-container.h"
#include "utils/design-tokens.h"

#include <QFrame>
#include <QScrollArea>
#include <QVBoxLayout>

namespace kai::ui {

ActionGroupContainer::ActionGroupContainer(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setObjectName(QStringLiteral("actionGroupContainer"));
    // Sem isso, um QWidget puro não pinta background/border do próprio QSS
    // (só QFrame/QPushButton/etc. fazem isso sozinhos) — mesmo bug já
    // contornado em LoadingOverlay/ExpandCollapseBar.
    setAttribute(Qt::WA_StyledBackground, true);

    const int pad = utils::tokens::space(1);
    if (orientation == Qt::Vertical) {
        // VERTICAL (sidebar): conteúdo dentro de um QScrollArea, para a coluna
        // rolar quando os ícones não couberem — em vez de impor altura mínima
        // que limita o crescimento do terminal (pedido do usuário).
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        m_scroll = new QScrollArea(this);
        m_scroll->setObjectName(QStringLiteral("actionSidebarScroll"));
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // Scrollbar bem fina (a coluna de ações é estreita — pedido do
        // usuário): 4px, sem setas, alça discreta.
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#actionSidebarScroll { background: transparent; border: none; }"
            "QScrollArea#actionSidebarScroll QScrollBar:vertical { background: transparent; width: 4px; margin: 0; }"
            "QScrollArea#actionSidebarScroll QScrollBar::handle:vertical { background: %1; border-radius: 2px; min-height: 16px; }"
            "QScrollArea#actionSidebarScroll QScrollBar::add-line, QScrollArea#actionSidebarScroll QScrollBar::sub-line { height: 0; width: 0; }"
            "QScrollArea#actionSidebarScroll QScrollBar::add-page, QScrollArea#actionSidebarScroll QScrollBar::sub-page { background: transparent; }")
            .arg(utils::tokens::borderColor()));
        m_scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

        m_content = new QWidget(m_scroll);
        m_content->setAttribute(Qt::WA_StyledBackground, true);
        m_content->setStyleSheet(QStringLiteral("background: transparent;"));
        m_layout = new QBoxLayout(QBoxLayout::TopToBottom, m_content);
        m_layout->setContentsMargins(pad, pad, pad, pad);
        m_layout->setSpacing(4);
        m_scroll->setWidget(m_content);
        outer->addWidget(m_scroll);
        // Não impõe altura mínima: pode encolher e rolar.
        setMinimumHeight(0);
    } else {
        m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
        m_layout->setContentsMargins(pad, pad, pad, pad);
        m_layout->setSpacing(4);
    }

    refreshStyle();
    setVisible(false); // sem grupos ainda — setGroups() decide
}

void ActionGroupContainer::setGroups(const QVector<QWidget *> &groups)
{
    // Limpa o layout (grupos são reparentados para fora, não destruídos —
    // pertencem ao MainWindow; separadores antigos são descartados).
    QLayoutItem *item = nullptr;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget()) {
            // ESCONDE antes de desanexar. setParent(nullptr) sozinho torna o
            // widget TOP-LEVEL de verdade — e como ele ficou com o "visível"
            // interno em true (setVisible(true) no addWidget original), o
            // Qt o exibe como uma JANELA FLUTUANTE solta na tela, na posição
            // onde estava antes (bug relatado, com screenshot: um grupo
            // marcado como "Oculto" nas Configurações aparecia bugado perto
            // do ícone do Kai — era literalmente essa janela órfã). Quando o
            // grupo está só MUDANDO de container (não sendo escondido de
            // vez), o setGroups() do container de destino chama
            // setVisible(true) de novo ao readicioná-lo, então esconder
            // aqui não quebra esse caso.
            w->hide();
            w->setParent(nullptr);
        }
        delete item;
    }
    qDeleteAll(m_separators);
    m_separators.clear();

    // Parent dos grupos/separadores: o widget de conteúdo do scroll quando
    // vertical, ou o próprio container quando horizontal.
    QWidget *host = m_content ? m_content : this;

    bool first = true;
    for (QWidget *group : groups) {
        if (!group) {
            continue;
        }
        if (!first) {
            QWidget *sep = createSeparator();
            m_separators.append(sep);
            m_layout->addWidget(sep);
        }
        group->setParent(host);
        group->setVisible(true);
        m_layout->addWidget(group);
        first = false;
    }
    if (m_orientation == Qt::Horizontal) {
        m_layout->addStretch(1);
    } else {
        m_layout->addStretch(1); // empurra os ícones para o topo; sobra rola
    }

    setVisible(!groups.isEmpty());
}

QWidget *ActionGroupContainer::createSeparator()
{
    auto *line = new QFrame(m_content ? m_content : this);
    line->setObjectName(QStringLiteral("actionGroupSeparator"));
    line->setFrameShape(m_orientation == Qt::Horizontal ? QFrame::VLine : QFrame::HLine);
    if (m_orientation == Qt::Horizontal) {
        line->setFixedWidth(1);
    } else {
        line->setFixedHeight(1);
    }
    line->setAttribute(Qt::WA_StyledBackground, true);
    return line;
}

void ActionGroupContainer::refreshStyle()
{
    const QString border = utils::tokens::borderColor();
    // Já com a cor substituída ANTES de entrar no template abaixo — não
    // pode carregar um "%1" cru para dentro do .arg() externo (ficaria sem
    // substituir de novo: QString::arg não faz múltiplas passadas).
    const QString edgeRule = m_orientation == Qt::Horizontal
        ? QStringLiteral("border-bottom: 1px solid %1;").arg(border)
        : QString();
    setStyleSheet(QStringLiteral(
        "QWidget#actionGroupContainer {"
        "  background: transparent;"
        "  border: none;"
        "  %2"
        "}"
        "QFrame#actionGroupSeparator {"
        "  background-color: %1;"
        "  border: none;"
        "  margin: %3px %3px;"
        "}"
    ).arg(border, edgeRule).arg(utils::tokens::space(1)));
}

} // namespace kai::ui
