#include "ui/features/command-editor/hooks-editor-widget.h"
#include "ui/shared/fuzzy-search.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QSignalBlocker>

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
constexpr int kCommandIdRole = Qt::UserRole + 1;
}

HooksEditorWidget::HooksEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void HooksEditorWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(tk::space(2));

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(tk::space(2));

    auto *segmented = new QWidget(this);
    segmented->setAttribute(Qt::WA_StyledBackground, true);
    segmented->setStyleSheet(QStringLiteral(
        "background-color: %1; border-radius: %2px;").arg(tk::bg()).arg(tk::radiusMd()));
    auto *segmentedLayout = new QHBoxLayout(segmented);
    segmentedLayout->setContentsMargins(2, 2, 2, 2);
    segmentedLayout->setSpacing(2);
    const QString segmentCss = QStringLiteral(
        "QPushButton { border: none; border-radius: %1px; padding: %2px %3px; font-weight: 600;"
        " background: transparent; color: %4; }"
        "QPushButton:checked { background-color: %5; color: %6; }")
        .arg(tk::radiusSm()).arg(tk::space(1)).arg(tk::space(3))
        .arg(tk::mutedFg()).arg(tk::accent()).arg(tk::bg());
    m_preTabButton = new QPushButton(segmented);
    m_postTabButton = new QPushButton(segmented);
    m_cleanupTabButton = new QPushButton(segmented);
    int phaseIndex = 0;
    for (QPushButton *btn : {m_preTabButton, m_postTabButton, m_cleanupTabButton}) {
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(segmentCss);
        segmentedLayout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, phaseIndex]() { switchPhase(phaseIndex); });
        ++phaseIndex;
    }
    m_preTabButton->setChecked(true);
    headerLayout->addWidget(segmented, 0, Qt::AlignVCenter);
    headerLayout->addStretch(1);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(utils::tr(QStringLiteral("hooks_editor.search_placeholder")));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(220);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &q) {
        applyFilter(m_listStack->currentWidget() == m_preList ? m_preList
                  : m_listStack->currentWidget() == m_postList ? m_postList : m_cleanupList, q);
    });
    headerLayout->addWidget(m_search, 0, Qt::AlignVCenter);

    mainLayout->addLayout(headerLayout);

    m_listStack = new QStackedWidget(this);
    // Transparente: o fundo escuro global pintava um retângulo quadrado atrás da lista arredondada.
    // Qualificado pelo nome para não cascatear nos filhos.
    m_listStack->setObjectName(QStringLiteral("hooksListStack"));
    m_listStack->setStyleSheet(QStringLiteral("QStackedWidget#hooksListStack { background: transparent; }"));
    m_listStack->setMinimumHeight(220);

    auto makeList = [this]() {
        auto *list = new QListWidget(m_listStack);
        list->setFrameShape(QFrame::NoFrame);
        list->setProperty("kaiRole", QStringLiteral("insetList")); // caixa escura com o raio da preferência de cantos
        // Altura mínima fica no QStackedWidget: dentro dele o layout não a respeitava e o pai cortava a base.
        connect(list, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
            updatePhaseButtonLabels();
            refreshCrossPhaseAnnotations();
            emit changed();
        });
        return list;
    };
    m_preList = makeList();
    m_postList = makeList();
    // Cleanup roda sempre ao terminar (sucesso, falha, crash, Stop, Reset): cobre o que encerrar o processo não desmonta.
    m_cleanupList = makeList();
    m_cleanupList->setToolTip(utils::tr(QStringLiteral("hooks_editor.cleanup.tip")));

    m_listStack->addWidget(m_preList);
    m_listStack->addWidget(m_postList);
    m_listStack->addWidget(m_cleanupList);
    mainLayout->addWidget(m_listStack, 1);

    updatePhaseButtonLabels();
}

void HooksEditorWidget::switchPhase(int phaseIndex)
{
    m_activePhase = phaseIndex;
    m_preTabButton->setChecked(phaseIndex == 0);
    m_postTabButton->setChecked(phaseIndex == 1);
    m_cleanupTabButton->setChecked(phaseIndex == 2);
    m_listStack->setCurrentIndex(phaseIndex);
    // Reaplica a busca na lista recém-exibida (mantém o texto).
    applyFilter(m_listStack->currentWidget() == m_preList ? m_preList
              : m_listStack->currentWidget() == m_postList ? m_postList : m_cleanupList,
              m_search->text());
}

void HooksEditorWidget::updatePhaseButtonLabels()
{
    auto label = [](const QString &key, int count) {
        return QStringLiteral("%1 (%2)").arg(utils::tr(key)).arg(count);
    };
    m_preTabButton->setText(label(QStringLiteral("hooks_editor.pre"), checkedIds(m_preList).size()));
    m_postTabButton->setText(label(QStringLiteral("hooks_editor.post"), checkedIds(m_postList).size()));
    m_cleanupTabButton->setText(label(QStringLiteral("hooks_editor.cleanup"), checkedIds(m_cleanupList).size()));
}

void HooksEditorWidget::refreshCrossPhaseAnnotations()
{
    // Anota (tag como "[PRE-HOOK]") os itens já marcados em outra fase.
    const QStringList preIds = checkedIds(m_preList);
    const QStringList postIds = checkedIds(m_postList);
    const QStringList cleanupIds = checkedIds(m_cleanupList);

    auto annotate = [this](QListWidget *list, const QStringList &otherPhaseIds1, const QString &tag1,
                            const QStringList &otherPhaseIds2, const QString &tag2) {
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            const QString id = item->data(kCommandIdRole).toString();
            const core::Command *cmd = nullptr;
            for (const core::Command &c : m_availableCommands) {
                if (c.id == id) { cmd = &c; break; }
            }
            if (!cmd) {
                // Pode ser resultado de busca de fora da pasta (universo completo).
                for (const core::Command &c : m_allCommands) {
                    if (c.id == id) { cmd = &c; break; }
                }
            }
            if (!cmd) {
                continue;
            }
            QStringList tags;
            if (otherPhaseIds1.contains(id)) tags << tag1;
            if (otherPhaseIds2.contains(id)) tags << tag2;
            item->setText(tags.isEmpty() ? cmd->name
                : QStringLiteral("%1   [%2]").arg(cmd->name, tags.join(QStringLiteral(" · "))));
        }
    };
    const QString preTag = utils::tr(QStringLiteral("hooks_editor.tag.pre"));
    const QString postTag = utils::tr(QStringLiteral("hooks_editor.tag.post"));
    const QString cleanupTag = utils::tr(QStringLiteral("hooks_editor.tag.cleanup"));
    annotate(m_preList, postIds, postTag, cleanupIds, cleanupTag);
    annotate(m_postList, preIds, preTag, cleanupIds, cleanupTag);
    annotate(m_cleanupList, preIds, preTag, postIds, postTag);
}

void HooksEditorWidget::applyFilter(QListWidget *list, const QString &query)
{
    if (!list) {
        return;
    }
    // Preserva o marcado: trocar a fonte (pasta <-> app inteiro) não pode desmarcar hooks.
    const QStringList selected = checkedIds(list);

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        populateList(list, m_availableCommands, selected);
        return;
    }

    // Pesquisando: universo completo com fuzzy match no nome; sem setAllAvailableCommands, o conjunto da pasta.
    const QVector<core::Command> &pool = m_allCommands.isEmpty() ? m_availableCommands : m_allCommands;
    QStringList names;
    names.reserve(pool.size());
    for (const core::Command &cmd : pool) {
        names << cmd.name;
    }
    const QVector<FuzzyMatchResult> matches = FuzzyMatcher::search(trimmed, names);
    QVector<core::Command> filtered;
    filtered.reserve(matches.size());
    for (const FuzzyMatchResult &m : matches) {
        if (m.originalIndex >= 0 && m.originalIndex < pool.size()) {
            filtered << pool.at(m.originalIndex);
        }
    }
    populateList(list, filtered, selected);
}

namespace {
// Comando KIP não pode ser hook (spec 11 §15): o picker nem o oferece.
QVector<core::Command> withoutKipCommands(const QVector<core::Command> &commands)
{
    QVector<core::Command> out;
    for (const core::Command &c : commands) {
        if (!(c.kip && c.type == core::CommandType::Command)) {
            out.append(c);
        }
    }
    return out;
}
} // namespace

void HooksEditorWidget::setAvailableCommands(const QVector<core::Command> &availableCommands)
{
    m_availableCommands = withoutKipCommands(availableCommands);
    populateList(m_preList, m_availableCommands, {});
    populateList(m_postList, m_availableCommands, {});
    populateList(m_cleanupList, m_availableCommands, {});
    updatePhaseButtonLabels();
}

void HooksEditorWidget::setAllAvailableCommands(const QVector<core::Command> &allCommands)
{
    m_allCommands = withoutKipCommands(allCommands);
}

void HooksEditorWidget::populateList(QListWidget *list, const QVector<core::Command> &source, const QStringList &selectedIds)
{
    const QSignalBlocker blocker(list); // evita disparar itemChanged N vezes ao popular
    list->clear();
    for (const core::Command &command : source) {
        auto *item = new QListWidgetItem(command.name, list);
        item->setData(kCommandIdRole, command.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selectedIds.contains(command.id) ? Qt::Checked : Qt::Unchecked);
    }
    refreshCrossPhaseAnnotations();
    emit changed();
}

QStringList HooksEditorWidget::checkedIds(QListWidget *list)
{
    QStringList ids;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (item->checkState() == Qt::Checked) {
            ids << item->data(kCommandIdRole).toString();
        }
    }
    return ids;
}

namespace {
// Hooks salvos que apontam para comando fora do escopo da pasta (possível via busca) continuam marcados ao reabrir.
QVector<core::Command> sourceIncluding(const QVector<core::Command> &base,
                                        const QVector<core::Command> &pool,
                                        const QStringList &requiredIds)
{
    QVector<core::Command> result = base;
    for (const QString &id : requiredIds) {
        bool present = false;
        for (const core::Command &c : result) {
            if (c.id == id) { present = true; break; }
        }
        if (present) {
            continue;
        }
        for (const core::Command &c : pool) {
            if (c.id == id) { result << c; break; }
        }
    }
    return result;
}
}

void HooksEditorWidget::setHooks(const core::Hooks &hooks)
{
    populateList(m_preList, sourceIncluding(m_availableCommands, m_allCommands, hooks.pre), hooks.pre);
    populateList(m_postList, sourceIncluding(m_availableCommands, m_allCommands, hooks.post), hooks.post);
    populateList(m_cleanupList, sourceIncluding(m_availableCommands, m_allCommands, hooks.cleanup), hooks.cleanup);
    updatePhaseButtonLabels();
}

core::Hooks HooksEditorWidget::hooks() const
{
    core::Hooks result;
    result.pre = checkedIds(m_preList);
    result.post = checkedIds(m_postList);
    result.cleanup = checkedIds(m_cleanupList);
    return result;
}

int HooksEditorWidget::totalCount() const
{
    return checkedIds(m_preList).size() + checkedIds(m_postList).size() + checkedIds(m_cleanupList).size();
}

} // namespace kai::ui
