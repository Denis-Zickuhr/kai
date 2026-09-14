#include "ui/features/command-editor/command-tree-widget.h"

#include "utils/design-tokens.h"
#include "core/folder-actions.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QFontMetrics>
#include <QTimer>
#include <QAction>
#include <QToolButton>
#include <QLabel>
#include <QColor>
#include <QTabBar>
#include <QIcon>
#include <QFrame>
#include <QAbstractItemModel>
#include <QApplication>
#include <QHelpEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QPalette>
#include <QEvent>
#include <QMovie>
#include <functional>
#include <algorithm>

#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/draggable-tree-widget.h"
#include "ui/shared/overflow-indicator.h"
#include "ui/shared/tab-bar-style.h"
#include "ui/shared/tab-strip-background.h"
#include "utils/logger.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace {
constexpr const char *kLogTag = "CommandTree";
// Id da pasta PADRÃO sintética que hospeda comandos/coleções órfãos
// (folderId vazio ou apontando para pasta/comando inexistente). Não é
// persistida — existe só na árvore para nunca esconder órfãos.
constexpr const char *kDefaultFolderId = "__kai_default__";
constexpr int kCommandIdRole = Qt::UserRole + 1;
constexpr int kIsCommandRole = Qt::UserRole + 2;
// Marca um item de árvore como Coleção (não-executável). Distinto de
// comando (kIsCommandRole=true) e de pasta (kIsCommandRole=false sem este).
constexpr int kIsCollectionRole = Qt::UserRole + 3;
// Marca se o comando é do tipo HTTP (para exibir "Editar body" no menu).
constexpr int kIsHttpRole = Qt::UserRole + 4;
// Marca um item como NOTA. Notas também levam kIsCollectionRole (são "nem pasta nem comando", como a coleção: não
// executam, não aceitam filhos...), e este papel as distingue onde o comportamento muda (menu, abrir, arrastar).
constexpr int kIsNoteRole = Qt::UserRole + 5;
constexpr int kStatusColumn = 1;

// Selo âmbar no canto do ícone da pasta (pasta fora de sincronia com o kai.yml).
QIcon withDriftBadge(const QIcon &base)
{
    QIcon out;
    for (const int size : {16, 20, 24, 32}) {
        QPixmap pixmap = base.pixmap(QSize(size, size));
        if (pixmap.isNull()) { // pasta sem ícone: o selo vai sozinho
            pixmap = QPixmap(size, size);
            pixmap.fill(Qt::transparent);
        }
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal radius = std::max<qreal>(3.0, size * 0.22);
        const QPointF center(pixmap.width() / pixmap.devicePixelRatio() - radius - 0.5, radius + 0.5);
        painter.setPen(QPen(QColor(utils::tokens::bg()), 1.2));
        painter.setBrush(QColor(utils::tokens::warningFg()));
        painter.drawEllipse(center, radius, radius);
        painter.end();
        out.addPixmap(pixmap);
    }
    return out;
}
constexpr int kRunningIndicatorSize = 20;

// Aplica (ou remove) o indicador de "rodando" na COLUNA DE STATUS do item
// (coluna 1), separado do ícone do comando (coluna 0) — feedback do
// usuário: o ícone de rodar deve ser grande e não ficar colado no ícone
// do comando. Usa o ícone Lucide "circle-play" grande, na cor verde de
// execução (#50fa7b), consistente com o botão Play da sidebar. Quando não
// está rodando, limpa a coluna.
int statusColumnBaseWidth(const QTreeWidget *tree)
{
    const QFontMetrics fm(tree->font());
    const int textWidth = fm.horizontalAdvance(QStringLiteral("00h 00m 00s"));
    const int itemHorizontalPadding = 2 * utils::tokens::space(3);
    return kRunningIndicatorSize + textWidth + itemHorizontalPadding + utils::tokens::space(4);
}

void applyRunningIndicator(QTreeWidgetItem *item, bool isRunning)
{
    if (isRunning) {
        item->setIcon(kStatusColumn,
            LucideIcons::icon(QStringLiteral("circle-play"), QColor(utils::tokens::successFg()), kRunningIndicatorSize));
        item->setToolTip(kStatusColumn, utils::tr(QStringLiteral("tree.status.running_tooltip")));
    } else {
        item->setIcon(kStatusColumn, QIcon());
        item->setToolTip(kStatusColumn, QString());
        // Limpa o contador de tempo quando o comando não está mais rodando.
        item->setText(kStatusColumn, QString());
    }
}

// Fundo do item SEMPRE transparente, em TODO estado (normal, hover,
// seleção, foco) — aplicado como stylesheet LOCAL de cada árvore (não via
// app-stylesheet.cpp/theme-manager.cpp), porque um stylesheet local de
// WIDGET tem prioridade garantida sobre qualquer stylesheet herdado do
// QApplication/tema, sem depender de especificidade de seletor CSS (fonte
// de bugs anteriores: a regra "CommandTreeWidget QTreeWidget::item" em
// app-stylesheet.cpp nem sempre vencia a cascata do tema ativo). Com o
// fundo do item sempre "apagado" aqui, o DraggableTreeWidget é o único
// responsável por pintar normal/zebra/hover/seleção — sempre linha
// inteira, sem cantos (ver paintEvent/setRowColors).
const QString kTreeItemTransparentQss = QStringLiteral(
    "QTreeWidget::item, QTreeWidget::item:hover, QTreeWidget::item:selected,"
    " QTreeWidget::item:selected:active, QTreeWidget::item:selected:!active,"
    " QTreeWidget::item:focus {"
    " background-color: transparent; border: none; outline: none; border-radius: 0px; }");
}

CommandTreeWidget::CommandTreeWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void CommandTreeWidget::setTabBarHeight(int height)
{
    if (m_tabWidget) {
        m_tabWidget->tabBar()->setStyleSheet(flatTabBarQss(height));
    }
}

void CommandTreeWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget(this);
    // Scroll lateral quando há muitas abas (feedback do
    // usuário: a barra de abas deve rolar lateralmente quando ficar cheia,
    // em vez de espremer/cortar). setUsesScrollButtons(true) mostra as
    // setas de navegação; setElideMode reticencia nomes muito longos em
    // vez de esticar a aba indefinidamente.
    m_tabWidget->setUsesScrollButtons(true);
    m_tabWidget->setElideMode(Qt::ElideRight);
    m_tabWidget->tabBar()->setExpanding(false);
    // Barra de rolagem (arrastável) na cor de destaque logo ABAIXO da barra de
    // abas, quando as abas das pastas raiz não cabem: mostra quanto está visível
    // e onde, e não conflita com o sublinhado da aba selecionada.
    QTabBar *rootTabBar = m_tabWidget->tabBar();
    auto *tabOverflow = new OverflowIndicator(m_tabWidget, OverflowIndicator::forTabBar(rootTabBar),
                                              OverflowIndicator::scrollToForTabBar(rootTabBar));
    tabOverflow->setAnchor(rootTabBar);
    tabOverflow->setBottomInset(1); // 1px de respiro abaixo da barra

    // Mesmo estilo/altura/recuo das abas da Saída (padronização das abas).
    setTabBarHeight(standardTabBarHeight());

    // Faixa de abas com fundo de outra cor (contraste com a lista abaixo).
    new TabStripBackground(m_tabWidget, m_tabWidget->tabBar());
    // Navegação por seta como prioridade (feedback do usuário: up/down
    // devem focar/navegar os comandos): ao trocar de aba, move o foco
    // para a árvore da aba ativa e seleciona o primeiro item, para que
    // ↑/↓ naveguem os comandos imediatamente, sem exigir clique.
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int) {
        focusFirstVisibleItem();
        // Ao trocar de aba, a Saída deve acompanhar o item ATUAL da nova aba
        // (pedido do usuário: a saída sempre segue o comando selecionado).
        // O currentItemChanged da árvore NÃO dispara só por trocar de aba se
        // a aba já tinha um item selecionado — então emitimos a seleção
        // explicitamente aqui.
        auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
        if (tree && tree->currentItem()) {
            QTreeWidgetItem *item = tree->currentItem();
            emit selectionChanged(item->data(0, kCommandIdRole).toString(),
                                  !item->data(0, kIsCommandRole).toBool());
        } else {
            emit selectionChanged(QString(), false);
        }
    });
    // Reordenar abas via drag (feature — feedback do usuário): habilita o
    // movimento das abas e persiste a nova ordem das pastas-raiz ao mover.
    m_tabWidget->tabBar()->setMovable(true);
    // Botão direito na ABA (pasta-raiz) — pedido do usuário: "quero a
    // possibilidade de editar pastas com o botão direito (pastas raiz)".
    // Antes a tab bar não tinha NENHUM menu de contexto; a única forma de
    // editar uma pasta-raiz era o botão de editar da toolbar (que segue a
    // aba ativa).
    m_tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabWidget->tabBar(), &QTabBar::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const int index = m_tabWidget->tabBar()->tabAt(pos);
        if (index < 0) {
            return;
        }
        showTabContextMenu(index, m_tabWidget->tabBar()->mapToGlobal(pos));
    });
    connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, [this](int, int) {
        // Deferido para o próximo ciclo do event loop: tratar o reorder
        // (que emite structureChanged -> persist -> possível rebuild das
        // abas) DENTRO do próprio callback de tabMoved causava re-entrância
        // no QTabBar durante o movimento e crashava (bug reportado). Com
        // singleShot(0), o QTabBar termina o move antes de reagirmos.
        QTimer::singleShot(0, this, [this]() { handleTabsReordered(); });
    });
    layout->addWidget(m_tabWidget);
}

void CommandTreeWidget::setData(const QVector<core::Folder> &folders,
                                const QVector<core::Command> &commands)
{
    m_folders = folders;
    m_commands = commands;
    // Chamada sem coleções mantém as existentes intactas? Não — a versão
    // 2-arg é legada (testes); zera coleções para não exibir dados órfãos.
    // O MainWindow usa sempre a versão 3-arg.
    m_collections.clear();
    m_notes.clear();
    applyDataAndRebuild();
}

void CommandTreeWidget::setData(const QVector<core::Folder> &folders,
                                const QVector<core::Command> &commands,
                                const QVector<core::Collection> &collections)
{
    m_folders = folders;
    m_commands = commands;
    m_collections = collections;
    m_notes.clear();
    applyDataAndRebuild();
}

void CommandTreeWidget::setData(const QVector<core::Folder> &folders, const QVector<core::Command> &commands,
                                const QVector<core::Collection> &collections, const QVector<core::Note> &notes)
{
    m_folders = folders;
    m_commands = commands;
    m_collections = collections;
    m_notes = notes;
    applyDataAndRebuild();
}

QString CommandTreeWidget::defaultFolderId()
{
    return QString::fromLatin1(kDefaultFolderId);
}

void CommandTreeWidget::applyDataAndRebuild()
{
    m_commandIndex.clear();
    for (const core::Command &c : m_commands) {
        m_commandIndex.insert(c.id, c);
    }
    // Ordenação de exibição (ordem manual persistida via
    // drag-and-drop): itens com `order` definido (>= 0) vêm primeiro, na
    // ordem manual crescente; os demais (order == -1) preservam a ordem
    // de inserção/carregamento original (stable_sort + comparador que não
    // os reordena entre si). Assim, o padrão de uma lista nunca arrastada
    // é a ordem em que os itens foram criados/importados, e assim que o
    // usuário arrasta itens de um nível, aquele nível passa a respeitar a
    // ordem manual persistida. (Nota: a auto-ordenação alfabética default
    // não é forçada aqui para preservar o contrato de ordem de inserção
    // das pastas raiz/abas — o usuário obtém a ordem que quiser via drag.)
    auto lessThan = [](int orderA, int orderB) {
        const bool manualA = orderA >= 0;
        const bool manualB = orderB >= 0;
        if (manualA != manualB) {
            return manualA; // itens com ordem manual primeiro
        }
        if (manualA && manualB) {
            return orderA < orderB;
        }
        return false; // ambos sem ordem manual: preserva ordem original
    };

    std::stable_sort(m_folders.begin(), m_folders.end(),
        [&lessThan](const core::Folder &a, const core::Folder &b) {
            return lessThan(a.order, b.order);
        });
    std::stable_sort(m_commands.begin(), m_commands.end(),
        [&lessThan](const core::Command &a, const core::Command &b) {
            return lessThan(a.order, b.order);
        });
    std::stable_sort(m_collections.begin(), m_collections.end(),
        [&lessThan](const core::Collection &a, const core::Collection &b) {
            return lessThan(a.order, b.order);
        });
    std::stable_sort(m_notes.begin(), m_notes.end(),
        [&lessThan](const core::Note &a, const core::Note &b) {
            return lessThan(a.order, b.order);
        });

    rebuildTabs();
}

void CommandTreeWidget::setRunningCommandIds(const QSet<QString> &runningCommandIds)
{
    // Marca o INÍCIO dos que passaram a rodar e descarta os que terminaram,
    // para o contador de tempo ser por execução (e não reiniciar a cada
    // atualização de status).
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &id : runningCommandIds) {
        if (!m_runStartedAt.contains(id)) {
            m_runStartedAt.insert(id, now);
        }
    }
    for (auto it = m_runStartedAt.begin(); it != m_runStartedAt.end();) {
        it = runningCommandIds.contains(it.key()) ? std::next(it) : m_runStartedAt.erase(it);
    }

    const bool runningSetChanged = m_runningCommandIds != runningCommandIds;
    m_runningCommandIds = runningCommandIds;
    refreshCommandVisuals();
    refreshActionViews(); // a cor (verde/accent) dos ícones de ação acompanha
    // Com "só em execução" ligado, quem começa/termina de rodar entra/sai da
    // lista na hora.
    if (m_showRunningOnly && runningSetChanged) {
        setFilterQuery(m_currentFilter);
    }

    // Tique de 1s só existe enquanto HÁ algo rodando (sem timer ocioso).
    if (!m_runStartedAt.isEmpty()) {
        if (!m_elapsedTimer) {
            m_elapsedTimer = new QTimer(this);
            m_elapsedTimer->setInterval(1000);
            connect(m_elapsedTimer, &QTimer::timeout, this, &CommandTreeWidget::updateElapsedLabels);
        }
        if (!m_elapsedTimer->isActive()) {
            m_elapsedTimer->start();
        }
        updateElapsedLabels(); // mostra "0s" imediatamente, sem esperar 1s
    } else if (m_elapsedTimer) {
        m_elapsedTimer->stop();
    }
}

void CommandTreeWidget::resetRunTimer(const QString &commandId)
{
    // Sobrescreve INCONDICIONALMENTE (ao contrário do laço em
    // setRunningCommandIds, que só grava se o id estava ausente) — ver
    // comentário no .h. Chamado pelo MainWindow no instante síncrono em que
    // uma execução começa de verdade, então não depende do próximo poll
    // periódico perceber uma transição parado->rodando.
    m_runStartedAt.insert(commandId, QDateTime::currentDateTime());
    if (!m_elapsedTimer) {
        m_elapsedTimer = new QTimer(this);
        m_elapsedTimer->setInterval(1000);
        connect(m_elapsedTimer, &QTimer::timeout, this, &CommandTreeWidget::updateElapsedLabels);
    }
    if (!m_elapsedTimer->isActive()) {
        m_elapsedTimer->start();
    }
    updateElapsedLabels();
}

QString CommandTreeWidget::formatElapsed(qint64 seconds)
{
    // Formato progressivo pedido pelo usuário: cada token só aparece quando é
    // ALCANÇADO — "0s" até 59s, "0m 0s" a partir de 1 minuto, "0h 0m 0s" a
    // partir de 1 hora. Nunca mostra "0h" antes de existir uma hora.
    if (seconds < 0) {
        seconds = 0;
    }
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 sec = seconds % 60;

    if (h > 0) {
        return QStringLiteral("%1h %2m %3s").arg(h).arg(m).arg(sec);
    }
    if (m > 0) {
        return QStringLiteral("%1m %2s").arg(m).arg(sec);
    }
    return QStringLiteral("%1s").arg(sec);
}

void CommandTreeWidget::updateElapsedLabels()
{
    if (m_runStartedAt.isEmpty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();
        std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
            if (item->data(0, kIsCommandRole).toBool()) {
                const QString id = item->data(0, kCommandIdRole).toString();
                const auto startIt = m_runStartedAt.constFind(id);
                if (startIt != m_runStartedAt.constEnd()) {
                    const qint64 secs = startIt.value().secsTo(now);
                    item->setText(kStatusColumn, formatElapsed(secs));
                }
            }
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i));
            }
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
}

void CommandTreeWidget::setFailedCommandIds(const QSet<QString> &failedCommandIds)
{
    m_failedCommandIds = failedCommandIds;
    refreshCommandVisuals();
}

void CommandTreeWidget::rebuildTabs()
{
    struct RebuildGuard {
        bool &flag;
        explicit RebuildGuard(bool &f) : flag(f) { flag = true; }
        ~RebuildGuard() { flag = false; }
    } rebuildGuard(m_rebuilding);
    QString activeRootId;
    if (m_tabWidget->currentIndex() >= 0) {
        activeRootId = m_tabWidget->tabBar()->tabData(m_tabWidget->currentIndex()).toString();
    }

    // Preserva o estado de expansão das pastas e o item selecionado antes
    // de destruir a árvore (bug reportado: ao editar/persistir um comando,
    // as pastas autocolapsavam e a aba resetava, porque rebuildTabs recria
    // tudo do zero). Capturamos os ids expandidos e o id selecionado para
    // reaplicá-los após reconstruir.
    const QSet<QString> expandedIds = collectExpandedIds();
    const QString selectedId = currentSelectionId();

    m_tabWidget->clear();
    m_treesByRootId.clear();

    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &folder : m_folders) {
        foldersById.insert(folder.id, folder);
    }

    QVector<core::Folder> rootFolders;
    for (const core::Folder &folder : m_folders) {
        const bool hasMissingParent = folder.parentId.has_value()
            && !foldersById.contains(folder.parentId.value());
        if (!folder.parentId.has_value() || hasMissingParent) {
            rootFolders << folder;
        }
    }

    // Pasta PADRÃO para órfãos (feedback do usuário: permitir criar comandos
    // sem que haja pastas; comandos/coleções sem pasta válida não podem
    // sumir). Um comando/coleção é órfão quando seu folderId é vazio OU não
    // resolve para nenhuma pasta/comando existente. Só criamos a aba padrão
    // se de fato houver órfãos, para não poluir quem já organiza tudo em
    // pastas. Pastas NUNCA são órfãs aqui: pasta sem pai já vira raiz acima.
    const bool hasDefaultFolder = foldersById.contains(kDefaultFolderId);
    if (!hasDefaultFolder) {
        auto resolvesToExisting = [this, &foldersById](const QString &parentId) -> bool {
            QString cur = parentId;
            QSet<QString> visited;
            for (int guard = 0; guard < 128; ++guard) {
                if (cur.isEmpty() || visited.contains(cur)) return false;
                visited.insert(cur);
                if (foldersById.contains(cur)) return true;
                const auto it = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                    [&cur](const core::Command &c) { return c.id == cur; });
                if (it == m_commands.constEnd()) return false;
                cur = it->folderId;
            }
            return false;
        };
        bool hasOrphans = false;
        for (const core::Command &c : m_commands) {
            if (!resolvesToExisting(c.folderId)) { hasOrphans = true; break; }
        }
        if (!hasOrphans) {
            for (const core::Collection &col : m_collections) {
                if (!foldersById.contains(col.folderId)) { hasOrphans = true; break; }
            }
        }
        if (!hasOrphans) {
            for (const core::Note &note : m_notes) {
                if (!foldersById.contains(note.folderId)) { hasOrphans = true; break; }
            }
        }
        if (hasOrphans) {
            core::Folder defaultFolder;
            defaultFolder.id = kDefaultFolderId;
            defaultFolder.name = utils::tr(QStringLiteral("folder.default_name"));
            defaultFolder.icon = QStringLiteral("folder");
            defaultFolder.order = -1; // vai para o fim das abas manuais
            rootFolders << defaultFolder;
            foldersById.insert(defaultFolder.id, defaultFolder);
        }
    }

    for (const core::Folder &rootFolder : rootFolders) {
        // OCULTAR DA ÁRVORE: pasta raiz oculta some a ABA inteira por
        // padrão (mesma regra de pastas/comandos/coleções aninhados).
        if (rootFolder.hidden && !m_showHidden) {
            continue;
        }
        auto *tree = createTreeForRoot(rootFolder);
        const int tabIndex = m_tabWidget->addTab(tree, folderIconFor(rootFolder), rootFolder.name);
        const QString driftTip = syncTooltipFor(rootFolder.id);
        m_tabWidget->setTabToolTip(tabIndex, driftTip.isEmpty() ? rootFolder.name
                                                                  : rootFolder.name + QStringLiteral("\n") + driftTip);
        m_tabWidget->tabBar()->setTabData(tabIndex, rootFolder.id);
        m_treesByRootId.insert(rootFolder.id, tree);
    }

    if (m_tabWidget->count() == 0) {
        return;
    }

    int restoredIndex = -1;
    if (!activeRootId.isEmpty()) {
        restoredIndex = m_tabWidget->indexOf(m_treesByRootId.value(activeRootId, nullptr));
    }
    m_tabWidget->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);
    setFilterQuery(m_currentFilter);

    // Reaplica o estado de expansão preservado (bug reportado: pastas
    // autocolapsavam ao persistir). Só reexpande o que estava expandido;
    // pastas novas seguem o default (colapsado).
    if (!expandedIds.isEmpty()) {
        restoreExpandedIds(expandedIds);
    }

    // Restaura a seleção anterior, se o item ainda existe após o rebuild.
    if (!selectedId.isEmpty()) {
        selectItemById(selectedId);
    }

    // Seleção default do primeiro item da aba ativa (feedback do usuário:
    // ao entrar numa tab, o primeiro item já deve estar selecionado, para
    // que ↑/↓ e Enter funcionem de imediato sem exigir clique). Feito após
    // aplicar o filtro para nunca selecionar um item escondido. Só quando
    // não houve seleção restaurada acima.
    if (auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget())) {
        if (!tree->currentItem() || tree->currentItem()->isHidden()) {
            selectFirstVisible(tree);
        }
    }

    // Reaplica a transparência do fundo às árvores recém-criadas (o rebuild
    // recria os QTreeWidget, perdendo o estilo aplicado por setBackground).
    applyBackgroundStyle();
    refreshActionViews(); // largura da coluna de ações nas árvores novas
}

QIcon CommandTreeWidget::folderIconFor(const core::Folder &folder) const
{
    const QIcon base = IconPickerWidget::iconForName(folder.icon);
    const auto drift = m_syncDrift.constFind(folder.id);
    if (drift == m_syncDrift.constEnd() || drift.value() == core::SyncDrift::InSync) {
        return base;
    }
    return withDriftBadge(base);
}

QString CommandTreeWidget::syncTooltipFor(const QString &folderId) const
{
    switch (m_syncDrift.value(folderId, core::SyncDrift::InSync)) {
    case core::SyncDrift::KaiChanged: return utils::tr(QStringLiteral("tree.sync.kai_changed"));
    case core::SyncDrift::FileChanged: return utils::tr(QStringLiteral("tree.sync.file_changed"));
    case core::SyncDrift::BothChanged: return utils::tr(QStringLiteral("tree.sync.both_changed"));
    case core::SyncDrift::InSync: break;
    }
    return QString();
}

void CommandTreeWidget::setSyncDrift(const QMap<QString, core::SyncDrift> &driftByFolderId)
{
    if (m_syncDrift == driftByFolderId) {
        return;
    }
    m_syncDrift = driftByFolderId;
    refreshSyncMarks();
}

void CommandTreeWidget::refreshSyncMarks()
{
    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &f : std::as_const(m_folders)) {
        foldersById.insert(f.id, f);
    }
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
            if (!item->data(0, kIsCommandRole).toBool() && !item->data(0, kIsCollectionRole).toBool()) {
                const auto folder = foldersById.constFind(item->data(0, kCommandIdRole).toString());
                if (folder != foldersById.constEnd()) {
                    item->setIcon(0, folderIconFor(*folder));
                    item->setToolTip(0, syncTooltipFor(folder->id));
                }
            }
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i));
            }
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        const auto folder = foldersById.constFind(m_tabWidget->tabBar()->tabData(i).toString());
        if (folder == foldersById.constEnd()) {
            continue;
        }
        m_tabWidget->setTabIcon(i, folderIconFor(*folder));
        const QString tip = syncTooltipFor(folder->id);
        m_tabWidget->setTabToolTip(i, tip.isEmpty() ? folder->name : folder->name + QStringLiteral("\n") + tip);
    }
}

void CommandTreeWidget::refreshCommandVisuals()
{
    // Atualiza apenas o indicador de status (coluna 1) e os controles
    // inline (coluna 2, Play/Stop/Reset/Editar/Excluir) de cada item de
    // comando já existente, SEM recriar nenhum QTreeWidget/QTreeWidgetItem
    // (bug real corrigido: "ao rodar um comando o sistema tá
    // resetando a barra"). Causa raiz confirmada via teste real: antes,
    // setRunningCommandIds/setFailedCommandIds chamavam rebuildTabs(), que
    // destrói e recria toda a árvore — perdendo seleção e posição de
    // scroll do usuário a cada mudança de status de execução, mesmo em
    // abas sem nenhuma relação com o comando que mudou de estado.
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();

        std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
            const bool isCommand = item->data(0, kIsCommandRole).toBool();
            if (isCommand) {
                const QString commandId = item->data(0, kCommandIdRole).toString();
                const auto commandIt = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                    [&commandId](const core::Command &c) { return c.id == commandId; });
                if (commandIt != m_commands.constEnd()) {
                    item->setIcon(0, IconPickerWidget::iconForName(commandIt->icon));
                    applyRunningIndicator(item, m_runningCommandIds.contains(commandId));
                }
            }
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i));
            }
        };

        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
}

QString CommandTreeWidget::rootIdForFolder(const core::Folder &folder,
                                            const QMap<QString, core::Folder> &foldersById)
{
    QString currentId = folder.id;
    QSet<QString> visited;

    while (foldersById.contains(currentId)) {
        if (visited.contains(currentId)) {
            // Dados corrompidos com ciclo: mantém a pasta inicial visível
            // como raiz de fallback, sem loop infinito ou descarte.
            return folder.id;
        }
        visited.insert(currentId);

        const core::Folder current = foldersById.value(currentId);
        if (!current.parentId.has_value() || !foldersById.contains(current.parentId.value())) {
            return current.id;
        }
        currentId = current.parentId.value();
    }

    return folder.id;
}

QTreeWidget *CommandTreeWidget::createTreeForRoot(const core::Folder &rootFolder)
{
    auto *tree = new DraggableTreeWidget(m_tabWidget);
    tree->setHeaderHidden(true);
    // Duas colunas: [0] ícone + nome do item (com a estrutura de árvore /
    // indentação, que o Qt sempre desenha na coluna da árvore) e [1] um
    // indicador de status GRANDE e SEPARADO do ícone do comando (feedback
    // do usuário: o ícone de "rodando" deve ser grande e não ficar colado
    // no ícone do comando). A coluna de status tem largura fixa e só
    // mostra algo quando o comando está em execução.
    tree->setColumnCount(2); // nome e status/tempo (que também leva os ícones de ação das pastas)
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    // A coluna de status carrega o ÍCONE de execução E o CONTADOR DE TEMPO.
    // Tinha 30px cravados — cabia só o ícone, e o contador não tinha onde ser
    // desenhado (bug reportado: "não está tendo espaço para renderizar o
    // contador de tempo"). Agora a largura é calculada: ícone + o texto mais
    // largo possível ("00h 00m 00s") + respiro.
    //
    // Bug real reportado DEPOIS: "os segundos ficam cortados" — a conta
    // acima não incluía o padding HORIZONTAL que o QSS global aplica em
    // todo item de árvore/lista ("QTreeView::item { padding: %2px %3px }",
    // ver app-stylesheet.cpp) — 2x esse padding (esquerda+direita) comia
    // parte do espaço já calculado, cortando os últimos caracteres
    // ("00h 00m 00s" virava "00h 00m 0"). Soma explicitamente agora.
    tree->setColumnWidth(1, statusColumnBaseWidth(tree));
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    // SELEÇÃO POR LINHA INTEIRA, não por célula (pedido do usuário: "as
    // bordas são entre as células" — árvore tem 2 colunas: nome/ícone e o
    // indicador de status. O padrão do Qt é SelectItems, que marca só a
    // CÉLULA clicada como selecionada; a segunda coluna (status), mesmo
    // vazia na maior parte do tempo, ficava sem o fundo de seleção/hover
    // pintado — abrindo um vão sem cor entre as duas células da mesma
    // linha, que parecia uma "borda" entre comandos. SelectRows marca as
    // duas colunas do item como selecionadas juntas, unificando o fundo.
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Zebra sutil estilo CopyQ: a cor de alternância é pintada pelo
    // próprio DraggableTreeWidget (ver setRowColors abaixo), não mais via
    // QSS alternate-background-color — então NÃO chamamos
    // setAlternatingRowColors aqui (o fallback nativo do Qt para esse modo
    // é exatamente o que abria o vão entre as duas colunas da linha no
    // hover — ver comentário em setRowColors).
    tree->setFrameShape(QFrame::NoFrame);
    tree->setRootIsDecorated(true);
    tree->setConnectorStyle(static_cast<DraggableTreeWidget::ConnectorStyle>(m_treeConnectorStyle));
    if (m_treeConnectorLineColor.isValid()) {
        tree->setConnectorLineColor(m_treeConnectorLineColor);
    }
    // Fundo (normal/zebra, hover e seleção) pintado pelo próprio widget,
    // cobrindo a linha inteira (ver DraggableTreeWidget::setRowColors) — o
    // QSS correspondente (app-stylesheet.cpp/theme-manager.cpp) deixa o
    // item transparente em todo estado só para esta árvore, então as cores
    // precisam vir daqui.
    tree->setRowColors(QColor(utils::tokens::bg()), QColor(utils::tokens::treeStripeBg()),
                        QColor(utils::tokens::hoverBg()), QColor(utils::tokens::selBg()));

    // Ícones das AÇÕES na linha de cada subpasta (ver FolderActionsView).
    {
        FolderActionsView::Provider provider;
        provider.folderIdOf = [](const QTreeWidgetItem *item) {
            if (item->data(0, kIsCommandRole).toBool() || item->data(0, kIsCollectionRole).toBool()) {
                return QString();
            }
            return item->data(0, kCommandIdRole).toString();
        };
        provider.iconsFor = [this](const QString &folderId) { return folderActionIcons(folderId); };
        provider.isRunning = [this](const QString &virtualId) { return m_runningCommandIds.contains(virtualId); };
        provider.hasOutput = [this](const QString &virtualId) { return m_actionsWithOutput.contains(virtualId); };
        provider.focusedId = [this]() { return m_focusedActionId; };
        auto *actionsView = new FolderActionsView(tree, std::move(provider));
        connect(actionsView, &FolderActionsView::activated, this, &CommandTreeWidget::folderActionActivated);
        connect(actionsView, &FolderActionsView::contextRequested, this,
                &CommandTreeWidget::folderActionContextRequested);
        // Clicar na linha da pasta FORA dos ícones desfoca a ação dela: a pasta
        // volta a ser a seleção (a saída da ação sai de vista, como sempre
        // que se seleciona uma pasta).
        connect(actionsView, &FolderActionsView::rowClicked, this, [this](const QString &folderId) {
            const auto parsed = core::parseFolderActionCommandId(m_focusedActionId);
            if (parsed && parsed->folderId == folderId) {
                setFocusedAction(QString());
                emit selectionChanged(folderId, true);
            }
        });
    }

    // Drag & drop de itens FILHOS (comandos, coleções, subpastas dentro de
    // uma pasta): tinha sido DESLIGADO ("o comportamento de drag dos
    // comandos pra re-ordenar ainda não funciona propriamente,
    // desabilitar") por causa de um bug real, real mesmo — o D&D NATIVO
    // do Qt que o DraggableTreeWidget de então usava (QTreeWidget::
    // InternalMove + QDrag) tinha um payload de MIME DATA que QUALQUER
    // widget sob o cursor no momento do release podia engolir, exatamente
    // o mesmo bug estrutural rastreado e eliminado na tabela de
    // Parâmetros Dinâmicos ("é como se o drag colocasse o param dentro do
    // outro componente"). Religado agora que DraggableTreeWidget foi
    // reescrito sem QDrag/mimeData nenhum (rastreamento de mouse manual,
    // 100% contido no próprio widget — ver draggable-tree-widget.h) — o
    // "resquício" mencionado antes (handleItemsReordered já lendo a
    // árvore inteira depois do drop) continua funcionando sem mudança
    // nenhuma, já que só o MECANISMO de mover o item mudou, não o
    // resultado final. O campo "Ordem" manual continua existindo — os
    // dois escrevem o mesmo `order`, sem conflito; drag é o atalho
    // rápido, o campo é o ajuste fino.
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    // Bug real reportado ("eu to conseguindo aninhar cmds dentro de
    // outros cmds, não era pra dar"): só PASTAS podem virar pai de outro
    // item ao soltar "em cima" (zona do meio) — comandos e coleções não
    // aceitam filhos por drag, só reordenam como irmãos. "Pasta" aqui é
    // "nem comando nem coleção" (kIsCommandRole/kIsCollectionRole ambos
    // false), o mesmo critério já usado no resto deste arquivo.
    tree->setCanAcceptChildrenPredicate([](QTreeWidgetItem *item) {
        return !item->data(0, kIsCommandRole).toBool() && !item->data(0, kIsCollectionRole).toBool();
    });

    connect(tree, &QTreeWidget::itemActivated, this, &CommandTreeWidget::handleItemActivated);
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        if (item && !m_rebuilding && item->data(0, kIsCommandRole).toBool()) {
            emit commandClicked(item->data(0, kCommandIdRole).toString());
        }
    });
    connect(tree, &QTreeWidget::customContextMenuRequested,
            this, &CommandTreeWidget::handleTreeContextMenuRequested);
    connect(tree, &QTreeWidget::currentItemChanged,
            this, &CommandTreeWidget::handleTreeCurrentItemChanged);
    connect(tree, &DraggableTreeWidget::itemsDropped, this, &CommandTreeWidget::handleItemsReordered);

    // Coluna única agora (coluna 1 de controles inline foi removida —
    // spec: as ações play/stop/trash/reset viraram ROW ACTIONS na
    // ActionSidebar, habilitadas conforme a linha selecionada).
    QMap<QString, core::Folder> foldersById;
    for (const core::Folder &folder : m_folders) {
        foldersById.insert(folder.id, folder);
    }

    // OCULTAR EM CASCATA: uma pasta oculta deve esconder TODA a sua
    // subárvore, não só a si mesma. Antes, ao pular a pasta oculta, os
    // filhos ficavam sem pai no itemsById e caíam como itens de TOPO
    // (renderizavam "fora da pasta" — bug relatado). Este helper sobe a
    // cadeia de ancestrais (parentId de pasta; folderId de comando, que pode
    // apontar para pasta OU comando) e diz se ALGUM ancestral é uma pasta
    // oculta. Com "Mostrar ocultos" ligado (m_showHidden), nada é suprimido.
    auto isUnderHiddenFolder = [this, &foldersById](const QString &startParentId) -> bool {
        if (m_showHidden) {
            return false;
        }
        QString cur = startParentId;
        QSet<QString> visited;
        for (int guard = 0; guard < 128; ++guard) {
            if (cur.isEmpty() || visited.contains(cur)) {
                break;
            }
            visited.insert(cur);
            const auto fit = foldersById.constFind(cur);
            if (fit != foldersById.constEnd()) {
                if (fit->hidden) {
                    return true; // ancestral é pasta oculta
                }
                cur = fit->parentId.value_or(QString());
                continue;
            }
            const auto cit = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                [&cur](const core::Command &c) { return c.id == cur; });
            if (cit == m_commands.constEnd()) {
                break;
            }
            cur = cit->folderId;
        }
        return false;
    };

    // Aninhamento UNIFICADO: agrupar comando dentro de comando é
    // permitido): criamos itens para pastas E comandos num único mapa por
    // id, e depois aninhamos cada item sob seu pai — que pode ser uma
    // pasta OU um comando. Um comando pertence ao id em command.folderId
    // (que passou a poder ser o id de outro comando após um drag). Uma
    // subpasta pertence a folder.parentId.
    QMap<QString, QTreeWidgetItem *> itemsById;

    // Pastas deste root (exceto a própria raiz, que é a aba).
    for (const core::Folder &folder : m_folders) {
        if (folder.id == rootFolder.id) {
            continue;
        }
        if (rootIdForFolder(folder, foldersById) != rootFolder.id) {
            continue;
        }
        // OCULTAR DA ÁRVORE: mesma regra de comandos (ver mais abaixo).
        if (folder.hidden && !m_showHidden) {
            continue;
        }
        // Cascata: se um ANCESTRAL está oculto, esta subpasta também some
        // (senão renderizaria fora da pasta oculta — bug relatado).
        if (isUnderHiddenFolder(folder.parentId.value_or(QString()))) {
            continue;
        }
        auto *folderItem = new QTreeWidgetItem();
        folderItem->setText(0, folder.name);
        folderItem->setData(0, kCommandIdRole, folder.id);
        folderItem->setData(0, kIsCommandRole, false);
        folderItem->setIcon(0, folderIconFor(folder));
        folderItem->setToolTip(0, syncTooltipFor(folder.id));
        if (folder.hidden) {
            folderItem->setForeground(0, QColor(utils::tokens::mutedFg()));
        }
        itemsById.insert(folder.id, folderItem);
    }

    // Comandos: cria um item para cada comando cujo ancestral (seguindo a
    // cadeia de pais pasta/comando) chega nesta pasta raiz. Como um
    // comando pode ser pai de outro, resolvemos o root subindo por
    // folderId até chegar a uma pasta raiz.
    auto commandRootId = [this, &foldersById](const core::Command &command) -> QString {
        QString currentParent = command.folderId;
        QSet<QString> visited;
        for (int guard = 0; guard < 128; ++guard) {
            if (currentParent.isEmpty() || visited.contains(currentParent)) {
                break;
            }
            visited.insert(currentParent);
            // Se o pai é uma pasta, sobe pela hierarquia de pastas.
            if (foldersById.contains(currentParent)) {
                return rootIdForFolder(foldersById.value(currentParent), foldersById);
            }
            // Senão, o pai deve ser um comando: sobe pelo folderId dele.
            const auto it = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                [&currentParent](const core::Command &c) { return c.id == currentParent; });
            if (it == m_commands.constEnd()) {
                break;
            }
            currentParent = it->folderId;
        }
        return currentParent;
    };

    for (const core::Command &command : m_commands) {
        const bool belongsDirectlyToRoot = (command.folderId == rootFolder.id);
        // Sob a pasta PADRÃO ("Geral"), entram os ÓRFÃOS: comandos cujo
        // folderId não resolve para nenhuma pasta/comando existente.
        bool isOrphanHere = false;
        if (rootFolder.id == QString::fromLatin1(kDefaultFolderId)) {
            QString cur = command.folderId;
            QSet<QString> visited;
            bool resolves = false;
            for (int guard = 0; guard < 128; ++guard) {
                if (cur.isEmpty() || visited.contains(cur)) break;
                visited.insert(cur);
                if (foldersById.contains(cur)) { resolves = true; break; }
                const auto it = std::find_if(m_commands.constBegin(), m_commands.constEnd(),
                    [&cur](const core::Command &c) { return c.id == cur; });
                if (it == m_commands.constEnd()) break;
                cur = it->folderId;
            }
            isOrphanHere = !resolves;
        }
        if (!belongsDirectlyToRoot && !isOrphanHere && commandRootId(command) != rootFolder.id) {
            continue;
        }
        // OCULTAR DA ÁRVORE: comando marcado hidden some por padrão; só
        // aparece com "Mostrar ocultos" ativo (ver setShowHidden), meio
        // apagado para se distinguir dos comandos normais.
        if (command.hidden && !m_showHidden) {
            continue;
        }
        // Cascata: comando dentro de uma pasta (ou subpasta) oculta some junto.
        if (isUnderHiddenFolder(command.folderId)) {
            continue;
        }
        auto *commandItem = new QTreeWidgetItem();
        commandItem->setText(0, formatCommandLabel(command));
        commandItem->setData(0, kCommandIdRole, command.id);
        commandItem->setData(0, kIsCommandRole, true);
        commandItem->setData(0, kIsHttpRole, command.type == core::CommandType::Http);
        commandItem->setIcon(0, IconPickerWidget::iconForName(command.icon));
        if (command.hidden) {
            commandItem->setForeground(0, QColor(utils::tokens::mutedFg()));
        }
        // Indicador de status GRANDE e separado na coluna 1 (feedback do
        // usuário): fica ao lado, não sobreposto ao ícone do comando.
        applyRunningIndicator(commandItem, m_runningCommandIds.contains(command.id));
        itemsById.insert(command.id, commandItem);
    }

    // Coleções deste root: itens NÃO-executáveis com ícone de database.
    // Pertencem diretamente a uma pasta (collection.folderId aponta para
    // uma pasta; não há coleção dentro de comando). Marcadas com
    // kIsCollectionRole para o menu de contexto e as row actions tratarem
    // como não-executáveis (play desabilitado).
    for (const core::Collection &collection : m_collections) {
        const bool belongsDirectlyToRoot = (collection.folderId == rootFolder.id);
        const bool viaFolder = foldersById.contains(collection.folderId)
            && rootIdForFolder(foldersById.value(collection.folderId), foldersById) == rootFolder.id;
        // Sob a pasta PADRÃO ("Geral"), entram coleções órfãs: folderId
        // vazio ou apontando para uma pasta inexistente.
        const bool isOrphanHere = (rootFolder.id == QString::fromLatin1(kDefaultFolderId))
            && !foldersById.contains(collection.folderId);
        if (!belongsDirectlyToRoot && !viaFolder && !isOrphanHere) {
            continue;
        }
        // OCULTAR DA ÁRVORE: mesma regra de comandos/pastas.
        if (collection.hidden && !m_showHidden) {
            continue;
        }
        // Cascata: coleção dentro de uma pasta oculta some junto.
        if (isUnderHiddenFolder(collection.folderId)) {
            continue;
        }
        auto *collectionItem = new QTreeWidgetItem();
        collectionItem->setText(0, collection.name);
        collectionItem->setData(0, kCommandIdRole, collection.id);
        collectionItem->setData(0, kIsCommandRole, false);
        collectionItem->setData(0, kIsCollectionRole, true);
        const QString iconName = collection.icon.isEmpty()
            ? QStringLiteral("database") : collection.icon;
        collectionItem->setIcon(0, IconPickerWidget::iconForName(iconName));
        if (collection.hidden) {
            collectionItem->setForeground(0, QColor(utils::tokens::mutedFg()));
        }
        itemsById.insert(collection.id, collectionItem);
    }

    // Notas deste root: itens NÃO-executáveis com ícone de caderno, sob a pasta delas (mesmas regras de ocultação e de
    // órfãs das coleções).
    for (const core::Note &note : m_notes) {
        const bool belongsDirectlyToRoot = (note.folderId == rootFolder.id);
        const bool viaFolder = foldersById.contains(note.folderId)
            && rootIdForFolder(foldersById.value(note.folderId), foldersById) == rootFolder.id;
        const bool isOrphanHere = (rootFolder.id == QString::fromLatin1(kDefaultFolderId))
            && !foldersById.contains(note.folderId);
        if (!belongsDirectlyToRoot && !viaFolder && !isOrphanHere) {
            continue;
        }
        if (note.hidden && !m_showHidden) {
            continue;
        }
        if (isUnderHiddenFolder(note.folderId)) {
            continue;
        }
        auto *noteItem = new QTreeWidgetItem();
        noteItem->setText(0, note.name);
        noteItem->setData(0, kCommandIdRole, note.id);
        noteItem->setData(0, kIsCommandRole, false);
        noteItem->setData(0, kIsCollectionRole, true);
        noteItem->setData(0, kIsNoteRole, true);
        const QString iconName = note.icon.isEmpty() ? QStringLiteral("notebook") : note.icon;
        noteItem->setIcon(0, IconPickerWidget::iconForName(iconName));
        noteItem->setToolTip(0, note.local ? utils::tr(QStringLiteral("note.tooltip.local"))
                                           : utils::tr(QStringLiteral("note.tooltip.synced")));
        if (note.hidden) {
            noteItem->setForeground(0, QColor(utils::tokens::mutedFg()));
        }
        itemsById.insert(note.id, noteItem);
    }

    // Aninha cada item sob seu pai (pasta ou comando). Pai == raiz da aba
    // (ou pai inexistente) => item de topo.
    auto parentIdOf = [&rootFolder](const QString &id, bool isCommand,
                                     const QMap<QString, core::Folder> &fById,
                                     const QVector<core::Command> &cmds) -> QString {
        if (isCommand) {
            const auto it = std::find_if(cmds.constBegin(), cmds.constEnd(),
                [&id](const core::Command &c) { return c.id == id; });
            return it != cmds.constEnd() ? it->folderId : QString();
        }
        const auto fit = fById.constFind(id);
        if (fit != fById.constEnd() && fit->parentId.has_value()) {
            return fit->parentId.value();
        }
        Q_UNUSED(rootFolder);
        return QString();
    };

    // Aninha os itens sob seus pais RESPEITANDO A ORDEM MANUAL: iteramos
    // na ordem de m_folders -> m_commands -> m_collections (todos já
    // ordenados por `order` acima), em vez de iterar o QMap itemsById (que
    // itera por id, embaralhando a ordem manual — bug real: reordenar
    // pastas não persistia visualmente após reload).
    // Constrói a lista de ids na ORDEM GLOBAL compartilhada (order único
    // atribuído em pré-ordem pelo drag). Pastas, comandos e coleções são
    // intercalados por `order` — antes o código concatenava pastas, depois
    // comandos, depois coleções, o que forçava coleções sempre ao fim e
    // ignorava a posição real do drag (bug: coleção movida pra cima ficava
    // embaixo). Itens sem order manual (order < 0) mantêm-se estáveis ao
    // final, na ordem de inserção dos vetores (já stable_sorted acima).
    struct OrderedRef { QString id; int order; int seqTiebreak; };
    QVector<OrderedRef> refs;
    int tiebreak = 0;
    for (const core::Folder &f : m_folders) {
        if (itemsById.contains(f.id)) refs.push_back({f.id, f.order, tiebreak++});
    }
    for (const core::Command &c : m_commands) {
        if (itemsById.contains(c.id)) refs.push_back({c.id, c.order, tiebreak++});
    }
    for (const core::Collection &col : m_collections) {
        if (itemsById.contains(col.id)) refs.push_back({col.id, col.order, tiebreak++});
    }
    for (const core::Note &note : m_notes) {
        if (itemsById.contains(note.id)) refs.push_back({note.id, note.order, tiebreak++});
    }
    std::stable_sort(refs.begin(), refs.end(), [](const OrderedRef &a, const OrderedRef &b) {
        const bool aManual = a.order >= 0;
        const bool bManual = b.order >= 0;
        if (aManual != bManual) return aManual;         // manuais antes dos sem-ordem
        if (aManual && a.order != b.order) return a.order < b.order;
        return a.seqTiebreak < b.seqTiebreak;           // estável
    });
    QStringList orderedIds;
    for (const OrderedRef &r : refs) {
        orderedIds << r.id;
    }

    for (const QString &itemId : orderedIds) {
        QTreeWidgetItem *item = itemsById.value(itemId);
        const bool isCommand = item->data(0, kIsCommandRole).toBool();
        const bool isCollection = item->data(0, kIsCollectionRole).toBool();
        const bool isNote = item->data(0, kIsNoteRole).toBool();
        QString parentId;
        if (isNote) {
            const auto nit = std::find_if(m_notes.constBegin(), m_notes.constEnd(),
                [&itemId](const core::Note &n) { return n.id == itemId; });
            parentId = (nit != m_notes.constEnd()) ? nit->folderId : QString();
        } else if (isCollection) {
            // Coleção aninha sob sua folderId (uma pasta). Descobrimos via
            // a lista de coleções.
            const auto cit = std::find_if(m_collections.constBegin(), m_collections.constEnd(),
                [&itemId](const core::Collection &c) { return c.id == itemId; });
            parentId = (cit != m_collections.constEnd()) ? cit->folderId : QString();
        } else {
            parentId = parentIdOf(itemId, isCommand, foldersById, m_commands);
        }
        QTreeWidgetItem *parentItem = (!parentId.isEmpty() && parentId != rootFolder.id)
            ? itemsById.value(parentId, nullptr)
            : nullptr;
        if (parentItem) {
            parentItem->addChild(item);
        } else {
            tree->addTopLevelItem(item);
        }
    }

    for (QTreeWidgetItem *item : itemsById) {
        item->setExpanded(false);
    }

    return tree;
}

QString CommandTreeWidget::formatCommandLabel(const core::Command &command)
{
    // Exibe apenas o nome do comando (feedback do usuário: "o
    // nome do comando deve apenas exibir o NOME, não precisa exibir os
    // params"). Os parâmetros ainda ficam visíveis ao editar o comando.
    return command.name;
}

void CommandTreeWidget::snapshotExpansion()
{
    m_expansionBeforeFilter.clear();
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        const QString id = item->data(0, kCommandIdRole).toString();
        if (!id.isEmpty()) {
            m_expansionBeforeFilter.insert(id, item->isExpanded());
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
}

void CommandTreeWidget::restoreExpansion()
{
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        const auto it = m_expansionBeforeFilter.constFind(item->data(0, kCommandIdRole).toString());
        if (it != m_expansionBeforeFilter.constEnd()) {
            item->setExpanded(it.value());
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
    m_expansionBeforeFilter.clear();
}

void CommandTreeWidget::updateRootTabVisibility()
{
    if (!m_tabWidget) {
        return;
    }
    int firstVisible = -1;
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        const int index = m_tabWidget->indexOf(tree);
        if (index < 0) {
            continue;
        }
        bool hasVisible = false;
        for (int i = 0; i < tree->topLevelItemCount() && !hasVisible; ++i) {
            hasVisible = !tree->topLevelItem(i)->isHidden();
        }
        const bool show = !m_showRunningOnly || hasVisible;
        m_tabWidget->setTabVisible(index, show);
        if (show && firstVisible < 0) {
            firstVisible = index;
        }
    }
    // A aba atual sumiu: vai para a primeira que sobrou.
    const int current = m_tabWidget->currentIndex();
    if (firstVisible >= 0 && current >= 0 && !m_tabWidget->isTabVisible(current)) {
        m_tabWidget->setCurrentIndex(firstVisible);
    }
}

void CommandTreeWidget::setFilterQuery(const QString &query)
{
    const bool filtering = !query.isEmpty() || m_showRunningOnly;
    if (filtering && !m_filterSnapshotTaken) {
        snapshotExpansion();
        m_filterSnapshotTaken = true;
    }
    m_currentFilter = query;
    for (QTreeWidget *tree : m_treesByRootId) {
        applyFilterToTree(tree, query);
    }
    if (!filtering && m_filterSnapshotTaken) {
        restoreExpansion(); // parou de filtrar: as pastas voltam ao que estavam
        m_filterSnapshotTaken = false;
    }
    updateRootTabVisibility();
    updateRunningOnlyHint();
}

void CommandTreeWidget::setShowRunningOnly(bool runningOnly)
{
    if (m_showRunningOnly == runningOnly) {
        return;
    }
    m_showRunningOnly = runningOnly;
    setFilterQuery(m_currentFilter);
}

void CommandTreeWidget::updateRunningOnlyHint()
{
    if (!m_tabWidget) {
        return;
    }
    // Dica "nenhum em execução" quando o filtro está ligado e a aba atual
    // ficou sem nenhum item visível (senão é uma caixa vazia sem explicação).
    bool nothingVisible = false;
    if (m_showRunningOnly) {
        nothingVisible = true;
        if (auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget())) {
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                if (!tree->topLevelItem(i)->isHidden()) {
                    nothingVisible = false;
                    break;
                }
            }
        }
    }
    if (!nothingVisible) {
        if (m_runningOnlyHint) {
            m_runningOnlyHint->hide();
        }
        return;
    }
    if (!m_runningOnlyHint) {
        m_runningOnlyHint = new QLabel(utils::tr(QStringLiteral("tree.running_only_empty")), m_tabWidget);
        m_runningOnlyHint->setAlignment(Qt::AlignCenter);
        m_runningOnlyHint->setWordWrap(true);
        m_runningOnlyHint->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_runningOnlyHint->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                             .arg(utils::tokens::mutedFg()));
        m_tabWidget->installEventFilter(this);
        connect(m_tabWidget, &QTabWidget::currentChanged, this, [this]() { updateRunningOnlyHint(); });
    }
    const QTabBar *tabBar = m_tabWidget->tabBar();
    const int top = tabBar->geometry().bottom() + 1;
    m_runningOnlyHint->setGeometry(utils::tokens::space(4), top,
                                   qMax(0, m_tabWidget->width() - 2 * utils::tokens::space(4)),
                                   qMax(0, m_tabWidget->height() - top));
    m_runningOnlyHint->show();
    m_runningOnlyHint->raise();
}

void CommandTreeWidget::setShowHidden(bool show)
{
    if (m_showHidden == show) {
        return;
    }
    m_showHidden = show;
    // Refiltra quais comandos entram na árvore (ver createTreeForRoot) —
    // precisa reconstruir, diferente do filtro de busca (que só
    // esconde/mostra itens já criados via applyFilterToItem).
    applyDataAndRebuild();
}

void CommandTreeWidget::applyFilterToTree(QTreeWidget *tree, const QString &query)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        applyFilterToItem(tree->topLevelItem(i), query);
    }
}

bool CommandTreeWidget::applyFilterToItem(QTreeWidgetItem *item, const QString &query)
{
    const bool wasHidden = item->isHidden();
    bool childMatches = false;
    for (int i = 0; i < item->childCount(); ++i) {
        childMatches = applyFilterToItem(item->child(i), query) || childMatches;
    }

    // Busca fuzzy (vazia = tudo casa).
    const bool selfMatches = query.isEmpty() || FuzzyMatcher::score(query, item->text(0)) >= 0;

    bool visible;
    if (m_showRunningOnly) {
        // Só comandos rodando agora (que casam com a busca) e as pastas que os
        // contêm; coleções e pastas sem nenhum em execução somem.
        const bool selfRunning = item->data(0, kIsCommandRole).toBool()
            && m_runningCommandIds.contains(item->data(0, kCommandIdRole).toString());
        visible = (selfRunning && selfMatches) || childMatches;
    } else {
        visible = selfMatches || childMatches;
    }
    item->setHidden(!visible);

    // Expande o caminho até o que casou: sempre para a busca (como antes); para
    // "só em execução", só quando o item acabou de aparecer — senão refazer o
    // filtro a cada mudança de execução reabriria pastas que o usuário fechou.
    if (visible && childMatches && (!query.isEmpty() || (m_showRunningOnly && wasHidden))) {
        item->setExpanded(true);
    }
    return visible;
}

void CommandTreeWidget::handleItemActivated(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    // Coleção: duplo-clique abre o editor (grid), como um comando abre a
    // execução. Antes só expandia/colapsava, dando a impressão de que não
    // dava pra editar (feedback do usuário).
    if (item->data(0, kIsNoteRole).toBool()) {
        const QString noteId = item->data(0, kCommandIdRole).toString();
        if (!noteId.isEmpty()) {
            emit noteActivated(noteId); // abre a nota no editor do leitor
        }
        return;
    }
    if (item->data(0, kIsCollectionRole).toBool()) {
        const QString collectionId = item->data(0, kCommandIdRole).toString();
        if (!collectionId.isEmpty()) {
            emit collectionEditRequested(collectionId);
        }
        return;
    }
    if (!item->data(0, kIsCommandRole).toBool()) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    const QString commandId = item->data(0, kCommandIdRole).toString();
    if (!commandId.isEmpty()) {
        emit commandActivated(commandId);
    }
}

void CommandTreeWidget::handleTreeContextMenuRequested(const QPoint &pos)
{
    auto *tree = qobject_cast<QTreeWidget *>(sender());
    if (!tree) {
        return;
    }
    QTreeWidgetItem *item = tree->itemAt(pos);
    const QPoint globalPos = tree->viewport()->mapToGlobal(pos);
    showContextMenu(item, globalPos);
}

void CommandTreeWidget::openContextMenuForCurrent()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree) {
        return;
    }
    QTreeWidgetItem *item = tree->currentItem();
    // Ancora o menu no item corrente (se houver) ou no canto da árvore, e o
    // exec do QMenu já dá navegação por teclado (setas/Enter/Esc).
    QPoint globalPos;
    if (item) {
        const QRect r = tree->visualItemRect(item);
        globalPos = tree->viewport()->mapToGlobal(r.center());
    } else {
        globalPos = tree->mapToGlobal(QPoint(tree->width() / 2, 20));
    }
    showContextMenu(item, globalPos);
}

void CommandTreeWidget::showContextMenu(QTreeWidgetItem *item, const QPoint &globalPos)
{
    const QColor accent(utils::tokens::accent());
    const QColor playColor(80, 250, 123);
    const QColor stopColor(241, 250, 140);
    const QColor killColor(255, 85, 85);

    // Helper: cria uma ação já com ícone Lucide colorido (todos os itens do
    // menu exibem ícone representativo — pedido de UX).
    auto addIconAction = [&](QMenu *m, const QString &icon, const QColor &color,
                             const QString &text) -> QAction * {
        QAction *a = m->addAction(LucideIcons::icon(icon, color, 16), text);
        return a;
    };

    // Habilita a NAVEGAÇÃO POR TECLADO (↑/↓/Enter): agenda, para o início do
    // event loop do exec(), o foco no menu e a ativação da primeira ação
    // habilitada. Sem isso, em alguns compositores (Wayland) o popup abre sem
    // foco de teclado e as setas não navegavam (bug reportado). Chamar ANTES de
    // cada menu.exec().
    auto armKeyboardNav = [](QMenu *m) {
        QTimer::singleShot(0, m, [m]() {
            m->activateWindow();
            m->raise();
            m->setFocus(Qt::OtherFocusReason);
            const auto acts = m->actions();
            for (QAction *a : acts) {
                if (a->isEnabled() && !a->isSeparator()) {
                    // setActiveAction destaca o item e, combinado com o foco no
                    // menu, habilita ↑/↓/Enter. Em alguns compositores (Wayland)
                    // o popup abre sem foco de teclado — daí o reforço acima.
                    m->setActiveAction(a);
                    break;
                }
            }
        });
    };

    // Menu ANCORADO na aba atual (widget da árvore). Sem um parent que faça
    // parte da hierarquia da janela, o QMenu não herdava o stylesheet global
    // (QMenu/QMenu::item/selected/separator do app-stylesheet) e aparecia com o
    // estilo nativo do sistema, fora do tema (bug reportado).
    QWidget *anchor = m_tabWidget->currentWidget();
    if (!anchor) {
        anchor = this;
    }
    QMenu menu(anchor);

    // --- Ações do item selecionado (se houver) ---
    if (item) {
        const bool isCommand = item->data(0, kIsCommandRole).toBool();
        const bool isCollection = item->data(0, kIsCollectionRole).toBool();
        const QString itemId = item->data(0, kCommandIdRole).toString();

        if (item->data(0, kIsNoteRole).toBool() && !itemId.isEmpty()) {
            QAction *openNote = addIconAction(&menu, QStringLiteral("pencil"), accent,
                                              utils::tr(QStringLiteral("ctx.note.open")));
            QAction *editNote = addIconAction(&menu, QStringLiteral("settings"), accent,
                                              utils::tr(QStringLiteral("ctx.note.edit")));
            QAction *dupNote = addIconAction(&menu, QStringLiteral("copy"), accent,
                                             utils::tr(QStringLiteral("ctx.note.duplicate")));
            menu.addSeparator();
            QAction *delNote = addIconAction(&menu, QStringLiteral("trash-2"), killColor,
                                             utils::tr(QStringLiteral("ctx.note.delete")));
            menu.addSeparator();
            addCreationActions(&menu, addIconAction, accent);
            armKeyboardNav(&menu);
            QAction *chosen = menu.exec(globalPos);
            if (chosen == openNote) emit noteActivated(itemId);
            else if (chosen == editNote) emit noteEditRequested(itemId);
            else if (chosen == dupNote) emit noteDuplicateRequested(itemId);
            else if (chosen == delNote) emit noteDeleteRequested(itemId);
            else handleCreationChoice(chosen);
            return;
        }

        if (isCollection && !itemId.isEmpty()) {
            QAction *editCol = addIconAction(&menu, QStringLiteral("pencil"), accent,
                                             utils::tr(QStringLiteral("ctx.collection.edit")));
            QAction *dupCol = addIconAction(&menu, QStringLiteral("copy"), accent,
                                            utils::tr(QStringLiteral("ctx.collection.duplicate")));
            menu.addSeparator();
            QAction *delCol = addIconAction(&menu, QStringLiteral("trash-2"), killColor,
                                            utils::tr(QStringLiteral("ctx.collection.delete")));
            menu.addSeparator();
            addCreationActions(&menu, addIconAction, accent);
            armKeyboardNav(&menu);
            QAction *chosen = menu.exec(globalPos);
            if (chosen == editCol) emit collectionEditRequested(itemId);
            else if (chosen == dupCol) emit collectionDuplicateRequested(itemId);
            else if (chosen == delCol) emit collectionDeleteRequested(itemId);
            else handleCreationChoice(chosen); // roteia Nova Pasta/Comando/Coleção
            return;
        }

        if (!itemId.isEmpty()) {
            // ACTION ROWS de comando (Rodar/Parar/Forçar/Resetar) com ícones,
            // no topo do menu (pedido de UX). Só para comandos.
            QAction *runAction = nullptr;
            QAction *stopAction = nullptr;
            QAction *forceAction = nullptr;
            QAction *resetAction = nullptr;
            if (isCommand) {
                const bool running = m_runningCommandIds.contains(itemId);
                runAction = addIconAction(&menu, QStringLiteral("play"), playColor,
                                          utils::tr(QStringLiteral("tree.run")));
                runAction->setEnabled(!running);
                stopAction = addIconAction(&menu, QStringLiteral("square"), stopColor,
                                           utils::tr(QStringLiteral("tree.stop")));
                stopAction->setEnabled(running);
                forceAction = addIconAction(&menu, QStringLiteral("octagon-x"), killColor,
                                            utils::tr(QStringLiteral("sidebar.force_stop")));
                forceAction->setEnabled(running);
                resetAction = addIconAction(&menu, QStringLiteral("rotate-ccw"), accent,
                                            utils::tr(QStringLiteral("tree.reset")));
                resetAction->setEnabled(running || m_failedCommandIds.contains(itemId));
                menu.addSeparator();
            }

            QAction *editAction = addIconAction(&menu, QStringLiteral("pencil"), accent,
                isCommand ? utils::tr(QStringLiteral("ctx.command.edit"))
                          : utils::tr(QStringLiteral("ctx.folder.edit")));
            QAction *duplicateAction = addIconAction(&menu, QStringLiteral("copy"), accent,
                isCommand ? utils::tr(QStringLiteral("ctx.command.duplicate"))
                          : utils::tr(QStringLiteral("ctx.folder.duplicate")));
            QAction *syncToFileAction = nullptr;
            QAction *syncFromFileAction = nullptr;
            if (!isCommand && isProjectFolder(itemId)) {
                menu.addSeparator();
                syncToFileAction = addIconAction(&menu, QStringLiteral("upload"), accent,
                                                 utils::tr(QStringLiteral("ctx.folder.sync_to_file")));
                syncFromFileAction = addIconAction(&menu, QStringLiteral("download"), accent,
                                                   utils::tr(QStringLiteral("ctx.folder.sync_from_file")));
            }
            QAction *exportAction = addIconAction(&menu, QStringLiteral("share-2"), accent,
                isCommand ? utils::tr(QStringLiteral("ctx.command.export"))
                          : utils::tr(QStringLiteral("ctx.folder.export")));
            menu.addSeparator();
            QAction *deleteAction = addIconAction(&menu, QStringLiteral("trash-2"), killColor,
                isCommand ? utils::tr(QStringLiteral("ctx.command.delete"))
                          : utils::tr(QStringLiteral("ctx.folder.delete")));
            menu.addSeparator();
            addCreationActions(&menu, addIconAction, accent);

            armKeyboardNav(&menu);

            QAction *chosen = menu.exec(globalPos);
            if (!chosen) return;
            if (chosen == runAction) emit playRequested(itemId);
            else if (chosen == stopAction) emit killRequested(itemId);
            else if (chosen == forceAction) emit forceStopRequested(itemId);
            else if (chosen == resetAction) emit resetRequested(itemId);
            else if (chosen == editAction) emit editRequested(itemId, !isCommand);
            else if (chosen == duplicateAction) emit duplicateRequested(itemId, !isCommand);
            else if (chosen == exportAction) emit exportRequested(itemId, !isCommand);
            else if (chosen == deleteAction) emit deleteRequested(itemId, !isCommand);
            else if (syncToFileAction && chosen == syncToFileAction) emit projectSyncRequested(itemId, true);
            else if (syncFromFileAction && chosen == syncFromFileAction) emit projectSyncRequested(itemId, false);
            else handleCreationChoice(chosen); // roteia Nova Pasta/Comando/Coleção
            return;
        }
    }

    // --- Área vazia (ou item sem id): só as ações de criação ---
    addCreationActions(&menu, addIconAction, accent);
    armKeyboardNav(&menu);
    QAction *chosen = menu.exec(globalPos);
    handleCreationChoice(chosen);
}

void CommandTreeWidget::showTabContextMenu(int tabIndex, const QPoint &globalPos)
{
    const QString rootId = m_tabWidget->tabBar()->tabData(tabIndex).toString();
    if (rootId.isEmpty()) {
        return;
    }

    // Estado atual de "oculta" (pedido do usuário: "adicione a
    // possibilidade de ocultar pastas de raiz") — precisa saber pra rotular
    // a ação certa ("Ocultar" vs "Mostrar") e pra permitir REVERTER uma
    // pasta que já ficou oculta antes de virar raiz (ex: era subpasta
    // oculta, o pai foi excluído, ela virou órfã/raiz e ficou presa sem
    // NENHUM outro jeito de desmarcar — o form de edição de pasta não tem
    // campo "oculta", e o atalho normal só enxerga o item selecionado
    // DENTRO da árvore, nunca a aba/raiz em si).
    bool isHidden = false;
    for (const core::Folder &f : m_folders) {
        if (f.id == rootId) { isHidden = f.hidden; break; }
    }

    QMenu menu(this);
    const QColor accent(utils::tokens::accent());
    const QColor killColor(255, 85, 85);
    QAction *editAction = menu.addAction(LucideIcons::icon(QStringLiteral("pencil"), accent, 16),
        utils::tr(QStringLiteral("ctx.folder.edit")));
    QAction *toggleHiddenAction = menu.addAction(
        LucideIcons::icon(isHidden ? QStringLiteral("eye") : QStringLiteral("eye-off"), accent, 16),
        utils::tr(isHidden ? QStringLiteral("ctx.folder.show") : QStringLiteral("ctx.folder.hide")));
    QAction *syncToFileAction = nullptr;
    QAction *syncFromFileAction = nullptr;
    if (isProjectFolder(rootId)) {
        menu.addSeparator();
        syncToFileAction = menu.addAction(LucideIcons::icon(QStringLiteral("upload"), accent, 16),
            utils::tr(QStringLiteral("ctx.folder.sync_to_file")));
        syncFromFileAction = menu.addAction(LucideIcons::icon(QStringLiteral("download"), accent, 16),
            utils::tr(QStringLiteral("ctx.folder.sync_from_file")));
    }
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(LucideIcons::icon(QStringLiteral("trash-2"), killColor, 16),
        utils::tr(QStringLiteral("ctx.folder.delete")));

    QTimer::singleShot(0, &menu, [&menu]() {
        menu.activateWindow();
        menu.raise();
        menu.setFocus(Qt::OtherFocusReason);
        if (!menu.actions().isEmpty()) {
            menu.setActiveAction(menu.actions().first());
        }
    });

    QAction *chosen = menu.exec(globalPos);
    if (chosen == editAction) {
        emit editRequested(rootId, /*isFolder=*/true);
    } else if (chosen == toggleHiddenAction) {
        emit toggleFolderHiddenRequested(rootId);
    } else if (chosen == deleteAction) {
        emit deleteRequested(rootId, /*isFolder=*/true);
    } else if (syncToFileAction && chosen == syncToFileAction) {
        emit projectSyncRequested(rootId, true);
    } else if (syncFromFileAction && chosen == syncFromFileAction) {
        emit projectSyncRequested(rootId, false);
    }
}

bool CommandTreeWidget::isProjectFolder(const QString &folderId) const
{
    for (const core::Folder &f : m_folders) {
        if (f.id == folderId) {
            return f.isProject;
        }
    }
    return false;
}

void CommandTreeWidget::addCreationActions(
    QMenu *menu,
    const std::function<QAction *(QMenu *, const QString &, const QColor &, const QString &)> &addIconAction,
    const QColor &accent)
{
    // Ações de INCLUSÃO diretas de Pasta/Comando/Coleção (pedido de UX). O
    // MainWindow resolve a pasta destino pela seleção/aba atual.
    QAction *newFolder = addIconAction(menu, QStringLiteral("folder-plus"), accent,
                                       utils::tr(QStringLiteral("ctx.new.folder")));
    QAction *newCommand = addIconAction(menu, QStringLiteral("square-terminal"), accent,
                                        utils::tr(QStringLiteral("ctx.new.command")));
    QAction *newCollection = addIconAction(menu, QStringLiteral("database"), accent,
                                           utils::tr(QStringLiteral("ctx.new.collection")));
    QAction *newNote = addIconAction(menu, QStringLiteral("notebook"), accent,
                                     utils::tr(QStringLiteral("ctx.new.note")));
    newNote->setData(QStringLiteral("new-note"));
    newNote->setProperty("kaiCreation", true);
    newFolder->setData(QStringLiteral("new-folder"));
    newCommand->setData(QStringLiteral("new-command"));
    newCollection->setData(QStringLiteral("new-collection"));
    // Marca para o roteador de escolha de criação distinguir estas ações.
    newFolder->setProperty("kaiCreation", true);
    newCommand->setProperty("kaiCreation", true);
    newCollection->setProperty("kaiCreation", true);
}

void CommandTreeWidget::handleCreationChoice(QAction *chosen)
{
    if (!chosen) {
        return;
    }
    const QString kind = chosen->data().toString();
    if (kind == QStringLiteral("new-folder")) emit newFolderRequested();
    else if (kind == QStringLiteral("new-command")) emit newCommandRequested();
    else if (kind == QStringLiteral("new-collection")) emit newCollectionRequested();
    else if (kind == QStringLiteral("new-note")) emit newNoteRequested();
}

void CommandTreeWidget::handleTreeCurrentItemChanged(QTreeWidgetItem *current,
                                                       QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    // Qualquer outra seleção tira o foco do ícone de ação (restaurar a
    // seleção depois de reconstruir a árvore não conta).
    if (!m_rebuilding && !m_focusedActionId.isEmpty()) {
        setFocusedAction(QString());
    }
    if (!current) {
        emit selectionChanged(QString(), false);
        return;
    }

    const QString itemId = current->data(0, kCommandIdRole).toString();
    const bool isCommand = current->data(0, kIsCommandRole).toBool();
    emit selectionChanged(itemId, !isCommand);
}

QString CommandTreeWidget::currentSelectionId() const
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return QString();
    }
    return tree->currentItem()->data(0, kCommandIdRole).toString();
}

void CommandTreeWidget::expandCurrentItem()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return;
    }
    tree->currentItem()->setExpanded(true);
}

void CommandTreeWidget::collapseCurrentItem()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return;
    }
    tree->currentItem()->setExpanded(false);
}

void CommandTreeWidget::expandAll()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree) {
        return;
    }
    tree->expandAll();
}

void CommandTreeWidget::collapseAll()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree) {
        return;
    }
    tree->collapseAll();
}

bool CommandTreeWidget::currentSelectionIsFolder() const
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return false;
    }
    // Coleção NÃO é pasta (embora não seja comando): distingue para o
    // botão de editar da sidebar rotear ao editor certo.
    if (tree->currentItem()->data(0, kIsCollectionRole).toBool()) {
        return false;
    }
    return !tree->currentItem()->data(0, kIsCommandRole).toBool();
}

bool CommandTreeWidget::currentSelectionIsCollection() const
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return false;
    }
    // Nota é um item não-executável como a coleção, mas tem fluxo próprio (ver currentSelectionIsNote).
    return tree->currentItem()->data(0, kIsCollectionRole).toBool() && !tree->currentItem()->data(0, kIsNoteRole).toBool();
}

bool CommandTreeWidget::currentSelectionIsNote() const
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree || !tree->currentItem()) {
        return false;
    }
    return tree->currentItem()->data(0, kIsNoteRole).toBool();
}

QString CommandTreeWidget::currentRootFolderId() const
{
    const int index = m_tabWidget->currentIndex();
    if (index < 0) {
        return QString();
    }
    return m_tabWidget->tabBar()->tabData(index).toString();
}

void CommandTreeWidget::focusFirstVisibleItem()
{
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (tree) {
        focusFirstVisibleItem(tree);
    }
}

void CommandTreeWidget::focusFirstVisibleItem(QTreeWidget *tree)
{
    tree->setFocus();
    // Se já há um item corrente visível, garante que ele esteja também
    // SELECIONADO (com highlight), não apenas corrente — o usuário
    // reportou o item sem destaque mesmo navegando por seta.
    if (QTreeWidgetItem *current = tree->currentItem(); current && !current->isHidden()) {
        if (!current->isSelected()) {
            tree->clearSelection();
            current->setSelected(true);
        }
        return;
    }
    selectFirstVisible(tree);
}

void CommandTreeWidget::selectFirstVisible(QTreeWidget *tree)
{
    if (!tree) {
        return;
    }
    // Aplica seleção VISUAL, não apenas o item corrente. Bug reportado
    // pelo usuário: o primeiro item ficava "current" (↑/↓ navegavam) mas
    // sem highlight, porque setCurrentItem() define o item corrente sem
    // garantir o estado :selected que o QSS usa para pintar o fundo —
    // sobretudo quando chamado antes de a árvore ter foco/estar visível
    // (no rebuild das abas). setCurrentItem + clearSelection +
    // setSelected(true) garante que o item nasça destacado.
    auto applySelection = [tree](QTreeWidgetItem *item) {
        tree->setCurrentItem(item);
        tree->clearSelection();
        item->setSelected(true);
        tree->scrollToItem(item);
    };

    // Seleciona o primeiro item VISÍVEL na ordem de exibição, sem forçar a
    // expansão de pastas colapsadas (outro requisito: pastas nascem
    // colapsadas). Só desce para dentro de um item se ele já estiver
    // expandido; caso contrário, seleciona o próprio item (pasta), que
    // fica destacado sem revelar/expandir seu conteúdo.
    std::function<bool(QTreeWidgetItem *)> selectFirst = [&](QTreeWidgetItem *item) {
        if (item->isHidden()) {
            return false;
        }
        if (item->childCount() > 0 && item->isExpanded()) {
            for (int i = 0; i < item->childCount(); ++i) {
                if (selectFirst(item->child(i))) {
                    return true;
                }
            }
        }
        applySelection(item);
        return true;
    };

    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (selectFirst(tree->topLevelItem(i))) {
            return;
        }
    }
}

QSet<QString> CommandTreeWidget::collectExpandedIds() const
{
    QSet<QString> expanded;
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        if (item->childCount() > 0 && item->isExpanded()) {
            const QString id = item->data(0, kCommandIdRole).toString();
            if (!id.isEmpty()) {
                expanded.insert(id);
            }
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
    return expanded;
}

void CommandTreeWidget::restoreExpandedIds(const QSet<QString> &expandedIds)
{
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        const QString id = item->data(0, kCommandIdRole).toString();
        if (!id.isEmpty() && expandedIds.contains(id)) {
            item->setExpanded(true);
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            visit(tree->topLevelItem(i));
        }
    }
}

bool CommandTreeWidget::selectItemById(const QString &id)
{
    if (id.isEmpty()) {
        return false;
    }
    for (auto it = m_treesByRootId.constBegin(); it != m_treesByRootId.constEnd(); ++it) {
        QTreeWidget *tree = it.value();
        std::function<QTreeWidgetItem *(QTreeWidgetItem *)> find =
            [&](QTreeWidgetItem *item) -> QTreeWidgetItem * {
                if (item->data(0, kCommandIdRole).toString() == id) {
                    return item;
                }
                for (int i = 0; i < item->childCount(); ++i) {
                    if (QTreeWidgetItem *found = find(item->child(i))) {
                        return found;
                    }
                }
                return nullptr;
            };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (QTreeWidgetItem *found = find(tree->topLevelItem(i)); found && !found->isHidden()) {
                // Ativa a aba que contém o item e o seleciona com destaque.
                m_tabWidget->setCurrentWidget(tree);
                tree->setCurrentItem(found);
                tree->clearSelection();
                found->setSelected(true);
                tree->scrollToItem(found);
                return true;
            }
        }
    }
    return false;
}

void CommandTreeWidget::selectNextTab()
{
    const int count = m_tabWidget->count();
    for (int step = 1; step < count; ++step) { // pula as abas escondidas pelo filtro
        const int index = (m_tabWidget->currentIndex() + step) % count;
        if (m_tabWidget->isTabVisible(index)) {
            m_tabWidget->setCurrentIndex(index);
            return;
        }
    }
}

void CommandTreeWidget::selectPreviousTab()
{
    const int count = m_tabWidget->count();
    for (int step = 1; step < count; ++step) {
        const int index = (m_tabWidget->currentIndex() - step + count * 2) % count;
        if (m_tabWidget->isTabVisible(index)) {
            m_tabWidget->setCurrentIndex(index);
            return;
        }
    }
}

void CommandTreeWidget::handleItemsReordered()
{
    // Após um drop, percorre a árvore ativa em pré-ordem e captura, para
    // cada item, sua nova POSIÇÃO (order sequencial por tipo) E seu novo
    // PAI (reparenting via drag — só entre pastas agora: DraggableTreeWidget
    // restringe a zona "On" a itens que não são comando/coleção, bug real
    // reportado: "eu to conseguindo aninhar cmds dentro de outros cmds,
    // não era pra dar". O aninhamento comando-dentro-de-comando continua
    // existindo como CAPACIDADE do modelo/renderização — só não é mais
    // alcançável por ESTE gesto de drag). O pai é:
    //  - o id do item-pai na árvore, se o item foi solto sob outro item;
    //  - a pasta raiz da aba ativa, se o item ficou no topo da árvore.
    // Emite structureChanged para o MainWindow persistir folderId/parentId
    // e order de uma vez, mantendo a hierarquia visual = hierarquia de
    // dados (corrige o bug: comando aninhado sob comando "sumia"/"bugava"
    // no próximo rebuild porque o folderId não acompanhava o drop).
    auto *tree = qobject_cast<QTreeWidget *>(m_tabWidget->currentWidget());
    if (!tree) {
        utils::Logger::warning(kLogTag,
            QStringLiteral("Drop recebido mas a árvore ativa não pôde ser resolvida — ignorado."));
        return;
    }

    const QString rootFolderId = currentRootFolderId();
    utils::Logger::info(kLogTag,
        QStringLiteral("Drag&drop: drop aceito na aba raiz '%1' (itens no topo: %2).")
            .arg(rootFolderId).arg(tree->topLevelItemCount()));

    QVector<TreeNodePlacement> placements;
    int seq = 0;

    std::function<void(QTreeWidgetItem *, const QString &)> visit =
        [&](QTreeWidgetItem *item, const QString &parentId) {
            const QString id = item->data(0, kCommandIdRole).toString();
            const bool isCommand = item->data(0, kIsCommandRole).toBool();
            const bool isNote = item->data(0, kIsNoteRole).toBool();
            const bool isCollection = item->data(0, kIsCollectionRole).toBool() && !isNote;
            if (!id.isEmpty()) {
                TreeNodePlacement placement;
                placement.id = id;
                placement.isCommand = isCommand;
                placement.isCollection = isCollection;
                placement.isNote = isNote;
                placement.parentId = parentId;
                // Sequência ÚNICA e global (posição em pré-ordem). Assim
                // pastas, coleções e comandos compartilham a mesma escala
                // de `order` e o rebuild consegue intercalá-los na ordem
                // EXATA em que ficaram após o drag (bug: coleção movida pra
                // cima continuava embaixo porque cada tipo tinha sua própria
                // sequência e o rebuild jogava coleções sempre por último).
                placement.order = seq++;
                placements.append(placement);
            }
            const QString childParentId = id.isEmpty() ? parentId : id;
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i), childParentId);
            }
        };

    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        visit(tree->topLevelItem(i), rootFolderId);
    }

    // Atualiza o estado local para consistência imediata antes do
    // round-trip de persistência.
    for (const TreeNodePlacement &p : placements) {
        if (p.isCommand) {
            for (core::Command &command : m_commands) {
                if (command.id == p.id) {
                    command.order = p.order;
                    command.folderId = p.parentId;
                    break;
                }
            }
        } else if (p.isNote) {
            for (core::Note &note : m_notes) {
                if (note.id == p.id) {
                    note.order = p.order;
                    note.folderId = p.parentId;
                    break;
                }
            }
        } else if (p.isCollection) {
            for (core::Collection &collection : m_collections) {
                if (collection.id == p.id) {
                    collection.order = p.order;
                    collection.folderId = p.parentId;
                    break;
                }
            }
        } else {
            for (core::Folder &folder : m_folders) {
                if (folder.id == p.id) {
                    folder.order = p.order;
                    folder.parentId = p.parentId.isEmpty() ? std::nullopt : std::make_optional(p.parentId);
                    break;
                }
            }
        }
    }

    // Log detalhado da nova ordenação/hierarquia resultante do drag
    // (diagnóstico do bug de order reportado no Windows).
    utils::Logger::info(kLogTag,
        QStringLiteral("Drag&drop: %1 item(ns) reordenado(s). Emitindo structureChanged.")
            .arg(placements.size()));
    for (const TreeNodePlacement &p : placements) {
        utils::Logger::debug(kLogTag,
            QStringLiteral("  -> %1 id='%2' order=%3 parent='%4'")
                .arg(p.isCommand ? QStringLiteral("cmd")
                     : p.isCollection ? QStringLiteral("colecao")
                     : p.isNote ? QStringLiteral("nota")
                     : QStringLiteral("pasta"))
                .arg(p.id).arg(p.order).arg(p.parentId));
    }

    emit structureChanged(placements);
}

void CommandTreeWidget::handleTabsReordered()
{
    // Lê a ordem visual atual das abas (cada aba carrega o id da pasta-raiz
    // em tabData) e atribui `order` sequencial às pastas-raiz. Emite
    // structureChanged para o MainWindow persistir. As pastas não-raiz não
    // são tocadas (mantêm seu próprio order relativo aos irmãos).
    QVector<TreeNodePlacement> placements;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        const QString rootId = m_tabWidget->tabBar()->tabData(i).toString();
        if (rootId.isEmpty()) {
            continue;
        }
        TreeNodePlacement placement;
        placement.id = rootId;
        placement.isCommand = false;
        placement.parentId = QString(); // pasta-raiz: sem pai
        placement.order = i;
        placements.append(placement);

        // Atualiza o estado local imediatamente.
        for (core::Folder &folder : m_folders) {
            if (folder.id == rootId) {
                folder.order = i;
                break;
            }
        }
    }
    if (!placements.isEmpty()) {
        utils::Logger::info(kLogTag,
            QStringLiteral("Drag&drop de abas: %1 aba(s) reordenada(s). Emitindo tabsReordered.")
                .arg(placements.size()));
        // Sinal DEDICADO de reordenação de abas: o MainWindow persiste a
        // ordem SEM reconstruir a árvore/abas. Usar o structureChanged
        // genérico disparava reloadCommandTree, que recriava as abas no
        // meio do reorder e travava o segundo arraste (bug reportado:
        // "só reordena uma vez / uma posição por vez").
        emit tabsReordered(placements);
    }
}

void CommandTreeWidget::setBackground(const QString &imagePath, int opacity)
{
    m_backgroundOpacity = qBound(0, opacity, 100);

    // Descarta animação anterior, se houver.
    if (m_backgroundMovie) {
        m_backgroundMovie->stop();
        m_backgroundMovie->deleteLater();
        m_backgroundMovie = nullptr;
    }
    m_backgroundImage = QPixmap();

    if (!imagePath.isEmpty()) {
        // GIF/animação: usa QMovie e repinta os viewports a cada frame.
        // Imagem estática: carrega direto no pixmap.
        auto *probe = new QMovie(imagePath);
        if (probe->isValid() && probe->frameCount() != 1) {
            m_backgroundMovie = probe;
            m_backgroundMovie->setParent(this);
            connect(m_backgroundMovie, &QMovie::frameChanged, this, [this](int) {
                for (QTreeWidget *tree : m_treesByRootId) {
                    if (tree) {
                        tree->viewport()->update();
                    }
                }
            });
            m_backgroundMovie->start();
        } else {
            probe->deleteLater();
            m_backgroundImage.load(imagePath);
        }
    }
    applyBackgroundStyle();
    update();
}

void CommandTreeWidget::setTreeConnectorStyle(int style, const QColor &lineColor)
{
    m_treeConnectorStyle = style;
    if (lineColor.isValid()) {
        m_treeConnectorLineColor = lineColor;
    }
    const auto connectorStyle = static_cast<DraggableTreeWidget::ConnectorStyle>(m_treeConnectorStyle);
    for (QTreeWidget *tree : m_treesByRootId) {
        auto *draggable = qobject_cast<DraggableTreeWidget *>(tree);
        if (!draggable) {
            continue;
        }
        draggable->setConnectorStyle(connectorStyle);
        if (m_treeConnectorLineColor.isValid()) {
            draggable->setConnectorLineColor(m_treeConnectorLineColor);
        }
    }
}

void CommandTreeWidget::refreshTreeConnectorColor(const QColor &lineColor)
{
    setTreeConnectorStyle(m_treeConnectorStyle, lineColor);
}

void CommandTreeWidget::applyBackgroundStyle()
{
    const bool hasBg = !m_backgroundImage.isNull() || m_backgroundMovie != nullptr;
    for (QTreeWidget *tree : m_treesByRootId) {
        if (!tree) {
            continue;
        }
        QWidget *vp = tree->viewport();
        if (hasBg) {
            // Viewport REALMENTE transparente (palette Base transparente),
            // para o fundo pintado no eventFilter aparecer atrás dos itens.
            // Zebra desligada (empastelaria a imagem).
            vp->setAutoFillBackground(false);
            QPalette pal = vp->palette();
            pal.setColor(QPalette::Base, Qt::transparent);
            vp->setPalette(pal);
            tree->setAlternatingRowColors(false);
            tree->setStyleSheet(QStringLiteral("QTreeWidget { background: transparent; }\n")
                + kTreeItemTransparentQss);
            vp->removeEventFilter(this);
            vp->installEventFilter(this);
            vp->update();
        } else {
            // Restaura o comportamento padrão do tema. NUNCA liga
            // setAlternatingRowColors aqui: essa função é chamada de novo a
            // cada rebuildTabs, e religar a zebra NATIVA do Qt reabre
            // exatamente o bug que setRowColors resolve — o fallback nativo
            // pinta a listra por CÉLULA e não alcança a segunda coluna
            // (status), mostrando uma bordinha só nas linhas claras/zebradas
            // (relatado pelo usuário). A listra é 100% responsabilidade do
            // DraggableTreeWidget (ver paintEvent/setRowColors).
            vp->removeEventFilter(this);
            vp->setPalette(QPalette());
            // NUNCA limpa pra QString() aqui: precisa manter
            // kTreeItemTransparentQss (stylesheet LOCAL, prioridade
            // garantida sobre o tema) — limpar reabria a bordinha
            // arredondada de hover/foco, já que sem ela a árvore volta a
            // depender só da cascata do tema (relatado como ainda
            // presente mesmo com as regras do tema já zeradas).
            tree->setStyleSheet(kTreeItemTransparentQss);
            vp->update();
        }
    }
    if (m_tabWidget) {
        m_tabWidget->setStyleSheet(hasBg
            ? QStringLiteral("QTabWidget::pane { background: transparent; }")
            : QString());
    }
}

bool CommandTreeWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Redimensionou a aba: reposiciona a dica do filtro "só em execução".
    if (watched == m_tabWidget && event->type() == QEvent::Resize) {
        updateRunningOnlyHint();
        return false;
    }
    const QPixmap frame = m_backgroundMovie ? m_backgroundMovie->currentPixmap()
                                            : m_backgroundImage;
    if (event->type() == QEvent::Paint && !frame.isNull()) {
        // Pinta a imagem no viewport da árvore observada, em cover, com um
        // véu da cor de fundo do tema cuja opacidade é o COMPLEMENTO da
        // opacidade dos itens: opacidade 100 => véu opaco (fundo some);
        // opacidade 0 => sem véu (imagem plena).
        if (auto *vp = qobject_cast<QWidget *>(watched)) {
            QPainter painter(vp);

            // ÂNCORA FIXA À JANELA (feedback do usuário): a imagem preenche
            // uma área fixa que vai do TOPO da caixa de comandos até o FUNDO
            // da janela, dimensionada pela janela — não pelo viewport. Assim,
            // mexer no splitter/terminal NÃO redimensiona a imagem; só mudar
            // a largura/altura da janela. Cada viewport desenha apenas o
            // recorte dessa área que cai sobre ele (estilo background-fixed).
            QWidget *win = window();
            const QPoint anchorTopLeftGlobal = mapToGlobal(QPoint(0, 0));
            const QPoint winBottomRightGlobal =
                win->mapToGlobal(QPoint(win->width(), win->height()));
            const QSize anchorSize(
                win->width(),
                qMax(1, winBottomRightGlobal.y() - anchorTopLeftGlobal.y()));

            QPixmap scaled = frame.scaled(
                anchorSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            // Centraliza o excesso do "cover" dentro da área âncora.
            const int coverX = (scaled.width() - anchorSize.width()) / 2;
            const int coverY = (scaled.height() - anchorSize.height()) / 2;

            // Offset do viewport dentro da área âncora (em coords da âncora).
            const QPoint vpTopLeftGlobal = vp->mapToGlobal(QPoint(0, 0));
            const int offX = vpTopLeftGlobal.x() - anchorTopLeftGlobal.x();
            const int offY = vpTopLeftGlobal.y() - anchorTopLeftGlobal.y();

            // Desenha, para a área visível do viewport, o pedaço da imagem
            // correspondente à sua posição na âncora.
            painter.drawPixmap(0, 0, scaled,
                                coverX + offX, coverY + offY,
                                vp->width(), vp->height());

            QColor veil(utils::tokens::bg());
            // Véu limitado a ~90% de alpha: mesmo em opacidade 100 a imagem
            // ainda transparece um pouco (senão o fundo some por completo e
            // parece que "não funciona").
            veil.setAlpha(qMin(230, (m_backgroundOpacity * 230) / 100));
            painter.fillRect(vp->rect(), veil);
        }
        // Deixa o Qt seguir pintando os itens POR CIMA (não consome o evento).
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void CommandTreeWidget::setGlobalActions(const QVector<core::GlobalAction> &actions)
{
    m_globalActions = actions;
    refreshActionViews();
}

void CommandTreeWidget::setFocusedAction(const QString &virtualId)
{
    if (m_focusedActionId == virtualId) {
        return;
    }
    m_focusedActionId = virtualId;
    refreshActionViews();
}

void CommandTreeWidget::setActionsWithOutput(const QSet<QString> &virtualIds)
{
    m_actionsWithOutput = virtualIds;
    refreshActionViews();
}

void CommandTreeWidget::refreshActionViews()
{
    // Os ícones de ação dividem a coluna de status/tempo com o play dos comandos (linha de
    // comando OU de pasta): a coluna cresce só se a pasta com MAIS ações precisar de mais.
    int most = 0;
    for (const core::Folder &folder : std::as_const(m_folders)) {
        if (folder.parentId.has_value()) {
            most = std::max(most, FolderActionsView::slotCount(folderActionIcons(folder.id)));
        }
    }
    const int width = FolderActionsView::columnWidthFor(most);
    for (QTreeWidget *tree : std::as_const(m_treesByRootId)) {
        const int target = std::max(width, statusColumnBaseWidth(tree));
        if (tree->columnWidth(FolderActionsView::kActionsColumn) != target) {
            tree->setColumnWidth(FolderActionsView::kActionsColumn, target);
        }
        tree->viewport()->update();
    }
}

QVector<FolderActionIcon> CommandTreeWidget::folderActionIcons(const QString &folderId) const
{
    QVector<FolderActionIcon> icons;
    const auto folderIt = std::find_if(m_folders.cbegin(), m_folders.cend(),
                                       [&folderId](const core::Folder &f) { return f.id == folderId; });
    // Pasta de primeiro nível é uma ABA, sem linha: só as subpastas têm ações.
    if (folderIt == m_folders.cend() || !folderIt->parentId.has_value()) {
        return icons;
    }
    for (const core::FolderAction &action : core::actionsForFolder(*folderIt, m_globalActions, m_commandIndex)) {
        const core::Command &command = m_commandIndex.value(action.commandId);
        icons.append({action.commandId, core::folderActionCommandId(action.commandId, folderId), folderId,
                      command.name, command.icon, action.expansion, action.group, action.groupIcon});
    }
    return icons;
}

} // namespace kai::ui
