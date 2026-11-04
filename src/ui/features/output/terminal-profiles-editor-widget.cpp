#include "ui/features/output/terminal-profiles-editor-widget.h"

#include "ui/shared/table-utils.h"
#include "ui/features/output/terminal-profile-dialog.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/icon-picker-widget.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QFrame>
#include <QToolButton>
#include <QPushButton>
#include <QMenu>
#include <QColor>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QStyle>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QPoint>
#include <QResizeEvent>
#include <QSizePolicy>

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

// MIME próprio do drag-reorder entre cards (DragHandleLabel/ProfileCardFrame); carrega só o índice de origem, como texto.
const char *kProfileRowMime = "application/x-kai-profile-row";

// Label que elide (…) o texto conforme a largura disponível: QLabel comum reporta a largura sem quebra como preferida
// e um nome longo empurrava os botões do card (e o diálogo de Configurações) para fora da área visível.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString &text, bool bold, QWidget *parent = nullptr)
        : QLabel(parent), m_fullText(text)
    {
        if (bold) {
            QFont f = font();
            f.setBold(true);
            setFont(f);
        }
        setToolTip(text);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        updateElidedText();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElidedText();
    }

private:
    void updateElidedText()
    {
        const QFontMetrics fm(font());
        setText(fm.elidedText(m_fullText, Qt::ElideRight, width()));
    }

    QString m_fullText;
};

// Alça de arrastar (⋮⋮): só ESTA label inicia o QDrag (o resto do card segue clicável); o card onde cai o drop
// (ProfileCardFrame) decide a reordenação.
class DragHandleLabel : public QLabel {
public:
    explicit DragHandleLabel(QWidget *parent) : QLabel(QStringLiteral("⋮⋮"), parent)
    {
        setCursor(Qt::OpenHandCursor);
        setProperty("kaiRole", QStringLiteral("caption"));
        setToolTip(utils::tr(QStringLiteral("terminals.drag_hint")));
    }

    int row = -1;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressPos = event->pos();
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) && row >= 0
            && (event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            auto *drag = new QDrag(this);
            auto *mime = new QMimeData();
            mime->setData(QLatin1String(kProfileRowMime), QByteArray::number(row));
            drag->setMimeData(mime);
            drag->exec(Qt::MoveAction);
        }
        QLabel::mouseMoveEvent(event);
    }

private:
    QPoint m_pressPos;
};

// Card de UM perfil. Aceita DROP do DragHandleLabel de outro card para reordenar (TerminalProfilesEditorWidget::moveRow).
class ProfileCardFrame : public QFrame {
public:
    explicit ProfileCardFrame(QWidget *parent) : QFrame(parent)
    {
        setObjectName(QStringLiteral("profileCard"));
        setAcceptDrops(true);
    }

    int row = -1;
    std::function<void(int from, int to)> onReordered;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat(QLatin1String(kProfileRowMime))) {
            const int sourceRow = event->mimeData()->data(QLatin1String(kProfileRowMime)).toInt();
            if (sourceRow != row) {
                event->acceptProposedAction();
                setProperty("kaiDrop", true);
                style()->unpolish(this);
                style()->polish(this);
            }
        }
    }

    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        setProperty("kaiDrop", false);
        style()->unpolish(this);
        style()->polish(this);
    }

    void dropEvent(QDropEvent *event) override
    {
        setProperty("kaiDrop", false);
        style()->unpolish(this);
        style()->polish(this);
        if (!event->mimeData()->hasFormat(QLatin1String(kProfileRowMime))) {
            return;
        }
        const int sourceRow = event->mimeData()->data(QLatin1String(kProfileRowMime)).toInt();
        if (sourceRow == row) {
            return;
        }
        event->acceptProposedAction();
        if (onReordered) {
            onReordered(sourceRow, row);
        }
    }
};

} // namespace

TerminalProfilesEditorWidget::TerminalProfilesEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void TerminalProfilesEditorWidget::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(10);

    // Cabeçalho: título + subtítulo à esquerda, "+ Novo Perfil" à direita.
    auto *header = new QHBoxLayout();
    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    auto *titleLabel = new QLabel(utils::tr(QStringLiteral("settings.group.terminals")), this);
    titleLabel->setProperty("kaiRole", QStringLiteral("title"));
    titleBox->addWidget(titleLabel);
    auto *subtitleLabel = new QLabel(utils::tr(QStringLiteral("terminals.page.subtitle")), this);
    subtitleLabel->setProperty("kaiRole", QStringLiteral("caption"));
    subtitleLabel->setWordWrap(true);
    // Ignored: senão o subtítulo empurrava o cabeçalho (e o botão à direita) para fora da largura visível.
    subtitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titleBox->addWidget(subtitleLabel);
    header->addLayout(titleBox, 1);

    // O botão "+" abre um menu de tipos; cada um abre o formulário no modo simples daquele tipo.
    m_addButton = new QToolButton(this);
    m_addButton->setText(utils::tr(QStringLiteral("terminals.add")));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addButton->setProperty("kaiRole", QStringLiteral("primary"));
    // Accent explícito: o QSS global só pinta kaiRole="primary" em QPushButton, e este é um QToolButton (tem menu).
    {
        const QColor accent(utils::tokens::accent());
        const QString onAccent = accent.lightnessF() > 0.6
            ? QStringLiteral("#101014") : QStringLiteral("#ffffff");
        m_addButton->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(onAccent), 14));
        m_addButton->setStyleSheet(QStringLiteral(
            "QToolButton { background-color: %1; color: %2; border: none;"
            " border-radius: %3px; padding: %4px %5px; font-weight: 600; }"
            "QToolButton:hover { background-color: %6; }"
            "QToolButton::menu-indicator { image: none; width: 0; }")
            .arg(utils::tokens::accent(), onAccent).arg(utils::tokens::radiusMd())
            .arg(utils::tokens::space(1)).arg(utils::tokens::space(3))
            .arg(QColor(utils::tokens::accent()).lighter(115).name()));
    }
    m_addButton->setPopupMode(QToolButton::InstantPopup);
    auto *presetMenu = new QMenu(m_addButton);
    struct Preset { QString labelKey; core::TemplateKind kind; };
    const QVector<Preset> presets = {
        {QStringLiteral("terminal_targets.preset.wsl"), core::TemplateKind::Wsl},
        {QStringLiteral("terminal_profile.kind.docker"), core::TemplateKind::Docker},
        {QStringLiteral("terminal_targets.preset.powershell"), core::TemplateKind::PowerShell},
        {QStringLiteral("terminal_targets.preset.local_shell"), core::TemplateKind::LocalShell},
    };
    for (const Preset &p : presets) {
        const QString name = utils::tr(p.labelKey);
        const core::TemplateKind kind = p.kind;
        connect(presetMenu->addAction(name), &QAction::triggered, this, [this, name, kind]() {
            core::TerminalProfile t;
            t.name = name;
            t.shell = core::defaultShellFor(kind);
            t.usePty = core::defaultPtyFor(kind);
            addProfileViaForm(t, kind);
        });
    }
    presetMenu->addSeparator();
    connect(presetMenu->addAction(utils::tr(QStringLiteral("terminals.add_blank"))),
            &QAction::triggered, this, [this]() { addProfileViaForm(core::TerminalProfile{}, std::nullopt); });
    m_addButton->setMenu(presetMenu);
    header->addWidget(m_addButton, 0, Qt::AlignTop);
    outer->addLayout(header);

    // Lista de cards rolável; layout vertical simples porque o widget também é usado standalone.
    auto *listContainer = new QWidget(this);
    m_listLayout = new QVBoxLayout(listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(10);
    outer->addWidget(listContainer, 1);
}

QWidget *TerminalProfilesEditorWidget::buildCard(int row)
{
    const core::TerminalProfile &t = m_targets.at(row);

    auto *card = new ProfileCardFrame(this);
    card->row = row;
    card->setProperty("kaiState", t.isDefault ? QStringLiteral("default") : QString());
    card->onReordered = [this](int from, int to) { moveRow(from, to); };

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(8);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    auto *handle = new DragHandleLabel(card);
    handle->row = row;
    topRow->addWidget(handle);

    auto *iconLabel = new QLabel(card);
    const QIcon profileIcon = !t.icon.isEmpty()
        ? IconPickerWidget::iconForName(t.icon)
        : (t.isDefault ? LucideIcons::icon(QStringLiteral("star-filled"), QColor(tk::warningFg()), 18)
                       : LucideIcons::icon(QStringLiteral("terminal"), QColor(tk::mutedFg()), 18));
    iconLabel->setPixmap(profileIcon.pixmap(18, 18));
    topRow->addWidget(iconLabel);

    auto *nameLabel = new ElidedLabel(t.name, /*bold=*/true, card);
    // Stretch=1: com SizePolicy::Ignored a label não tem sizeHint, e sem stretch colapsava para largura 0; assim ela
    // reclama a fatia disponível antes de elidir.
    topRow->addWidget(nameLabel, 1);

    if (t.isDefault) {
        auto *badge = new QLabel(utils::tr(QStringLiteral("terminal_targets.field.is_default")).toUpper(), card);
        badge->setObjectName(QStringLiteral("profileDefaultBadge"));
        badge->setStyleSheet(QStringLiteral(
            "QLabel#profileDefaultBadge { background-color: %1; color: %2;"
            " border-radius: %3px; padding: 1px 8px; font-weight: 700; font-size: %4pt; }")
            .arg(tk::warningFg(), tk::bg()).arg(tk::radiusSm()).arg(tk::fontSizeSmallPt()));
        topRow->addWidget(badge);
    }

    topRow->addStretch();

    if (!t.isDefault) {
        auto *setDefaultButton = new QPushButton(utils::tr(QStringLiteral("terminals.set_default")), card);
        connect(setDefaultButton, &QPushButton::clicked, this, [this, row]() { setDefaultRow(row); });
        topRow->addWidget(setDefaultButton);
    }

    topRow->addWidget(makeRowActionsCell(card,
        [this, row]() { if (editRowViaForm(row)) rebuildList(); },
        [this, row]() { removeRow(row); }));

    cardLayout->addLayout(topRow);

    // Preview do comando: caixa monoespaçada (tk::codeAreaQss()).
    auto *previewLabel = new QLabel(t.commandTemplate, card);
    previewLabel->setWordWrap(true);
    // Ignored (não Preferred): QLabel::sizeHint() reporta a largura sem quebra mesmo com wordWrap, o que forçava o
    // card (e o diálogo inteiro) a crescer com comandos longos.
    previewLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    previewLabel->setContentsMargins(10, 6, 10, 6);
    previewLabel->setStyleSheet(tk::codeAreaQss());
    cardLayout->addWidget(previewLabel);

    return card;
}

void TerminalProfilesEditorWidget::rebuildList()
{
    QLayoutItem *item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    for (int row = 0; row < m_targets.size(); ++row) {
        m_listLayout->addWidget(buildCard(row));
    }
    m_listLayout->addStretch();
}

void TerminalProfilesEditorWidget::setTargets(const QVector<core::TerminalProfile> &targets)
{
    m_targets = targets;
    rebuildList();
}

QVector<core::TerminalProfile> TerminalProfilesEditorWidget::targets() const
{
    QVector<core::TerminalProfile> result;
    for (const core::TerminalProfile &t : m_targets) {
        if (!t.name.trimmed().isEmpty()) {
            result.append(t);
        }
    }
    return result;
}

void TerminalProfilesEditorWidget::removeRow(int row)
{
    if (row < 0 || row >= m_targets.size()) {
        return;
    }
    m_targets.remove(row);
    rebuildList();
}

void TerminalProfilesEditorWidget::setDefaultRow(int row)
{
    if (row < 0 || row >= m_targets.size()) {
        return;
    }
    for (int i = 0; i < m_targets.size(); ++i) {
        m_targets[i].isDefault = (i == row);
    }
    rebuildList();
}

void TerminalProfilesEditorWidget::moveRow(int from, int to)
{
    if (from < 0 || from >= m_targets.size() || to < 0 || to >= m_targets.size() || from == to) {
        return;
    }
    m_targets.move(from, to);
    rebuildList();
}

bool TerminalProfilesEditorWidget::editRowViaForm(int row)
{
    if (row < 0 || row >= m_targets.size()) {
        return false;
    }
    TerminalProfileDialog dialog(m_targets.at(row), std::nullopt, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    applyEdited(row, dialog.profile());
    return true;
}

void TerminalProfilesEditorWidget::addProfileViaForm(const core::TerminalProfile &initial,
                                                     std::optional<core::TemplateKind> kind)
{
    TerminalProfileDialog dialog(initial, kind, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_targets.append(core::TerminalProfile{});
    applyEdited(m_targets.size() - 1, dialog.profile());
    rebuildList();
}

void TerminalProfilesEditorWidget::applyEdited(int row, const core::TerminalProfile &updated)
{
    m_targets[row] = updated;
    // Exclusividade do "Padrão": se este virou padrão, desmarca os outros.
    if (updated.isDefault) {
        for (int i = 0; i < m_targets.size(); ++i) {
            if (i != row) {
                m_targets[i].isDefault = false;
            }
        }
    }
}

} // namespace kai::ui
