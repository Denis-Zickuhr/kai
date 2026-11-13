#include "ui/features/store/store-preview-dialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "core/store-texts.h"
#include "ui/features/http/http-ui.h"
#include "ui/layout/panel-card.h"
#include "ui/shared/panel-metrics.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/folder-picker-widget.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

QLabel *caption(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setProperty("kaiRole", QStringLiteral("caption"));
    return label;
}

QString toolLine(const QString &name)
{
    return QStringLiteral("%1 — %2").arg(name, core::storeToolFound(name) ? utils::tr(QStringLiteral("store.preview.found"))
                                                                         : utils::tr(QStringLiteral("store.preview.not_found")));
}

} // namespace

StorePreviewDialog::StorePreviewDialog(const engine::StoreStagedPackage &staged, const QString &providerName,
                                       const QString &trustLabel, const QString &signatureLabel,
                                       const QVector<core::Folder> &folders, QWidget *parent)
    : QDialog(parent), m_staged(staged)
{
    namespace lh = layout_helpers;
    const core::StoreManifest &m = staged.manifest;
    setWindowTitle(utils::tr(QStringLiteral("store.preview.title")).arg(m.name, m.version));
    resize(760, 620);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 12);
    outer->setSpacing(utils::tokens::space(3));

    // Origem em chips discretos.
    auto *sourceRow = new QHBoxLayout();
    sourceRow->setSpacing(tk::space(1));
    for (const QString &text : {providerName, trustLabel, signatureLabel}) {
        auto *chip = new QLabel(text, this);
        chip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const QColor c(tk::mutedFg());
        chip->setStyleSheet(QStringLiteral("QLabel { color: %1; background: rgba(%2, %3, %4, 0.14); border-radius: %5px;"
                                           " padding: 2px 10px; font-size: %6pt; }")
                                .arg(tk::mutedFg()).arg(c.red()).arg(c.green()).arg(c.blue()).arg(tk::radiusSm()).arg(tk::fontSizeSmallPt()));
        sourceRow->addWidget(chip);
    }
    sourceRow->addStretch(1);
    outer->addLayout(sourceRow);
    if (staged.refMoves) {
        outer->addWidget(caption(this, utils::tr(QStringLiteral("store.preview.ref_moves"))));
    }

    const core::StorePreview &p = staged.preview;
    if (p.isExportPackage) {
        outer->addWidget(caption(this, utils::tr(QStringLiteral("store.preview.export_package")).arg(p.exportCategories.join(QStringLiteral(", ")))));
    }

    // Lista de comandos + código somente leitura.
    auto *split = new QSplitter(Qt::Horizontal, this);
    m_split = split;
    m_commands = new QListWidget;
    for (const core::StorePreviewCommand &c : p.commands) {
        const QString path = c.folder.isEmpty() ? c.name : c.folder + QLatin1Char('/') + c.name;
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(path, c.type), m_commands);
        item->setData(Qt::UserRole, c.type == QLatin1String("http") ? c.url : c.body);
        item->setData(Qt::UserRole + 1, c.name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    m_code = new QPlainTextEdit;
    m_code->setReadOnly(true);
    m_code->setFont(utils::tokens::monoFont());
    m_code->setLineWrapMode(QPlainTextEdit::WidgetWidth); // sem barra horizontal quebrando o canto do painel
    connect(m_commands, &QListWidget::currentRowChanged, this, [this](int row) {
        m_code->setPlainText(row >= 0 ? m_commands->item(row)->data(Qt::UserRole).toString() : QString());
    });
    split->setObjectName(QStringLiteral("storeSplitter"));
    split->setChildrenCollapsible(false);
    split->setMinimumHeight(200);
    m_commands->viewport()->installEventFilter(this);
    m_commands->setFrameShape(QFrame::NoFrame);
    m_commands->setStyleSheet(QStringLiteral("QListWidget { border-radius: %1px; }"
                                             "QListWidget QWidget#qt_scrollarea_viewport { background: transparent; }")
                                  .arg(panelInnerRadius()));
    m_commands->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_commands->setTextElideMode(Qt::ElideRight);
    m_code->setFrameShape(QFrame::NoFrame);
    m_code->setStyleSheet(QStringLiteral("QPlainTextEdit { background: %1; border: none; border-radius: %2px; padding: 8px; }")
                              .arg(tk::codeBg()).arg(panelInnerRadius()));
    for (QWidget *inner : {static_cast<QWidget *>(m_commands), static_cast<QWidget *>(m_code)}) {
        QWidget *card = layout::wrapInPanelCard(inner);
        card->setObjectName(QStringLiteral("nestedPanelCard"));
        split->addWidget(card);
    }
    split->setStretchFactor(1, 1);
    split->setSizes({260, 460});
    auto *commandsLabel = new QLabel(utils::tr(QStringLiteral("store.preview.commands")).toUpper(), this);
    commandsLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px; background: transparent;")
                                     .arg(tk::mutedFg()).arg(tk::fontSizeSmallPt()));
    // Instalação parcial: cada comando tem uma caixa; "Todos"/"Nenhum" marcam em massa.
    auto *commandsHeader = new QHBoxLayout();
    commandsHeader->addWidget(commandsLabel);
    commandsHeader->addStretch(1);
    m_selectionCount = new QLabel(this);
    m_selectionCount->setProperty("kaiRole", QStringLiteral("caption"));
    commandsHeader->addWidget(m_selectionCount);
    for (const bool all : {true, false}) {
        auto *button = new QPushButton(utils::tr(all ? QStringLiteral("store.preview.select_all") : QStringLiteral("store.preview.select_none")), this);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; color: %1; padding: 2px 8px; }"
                                             "QPushButton:hover { text-decoration: underline; }").arg(tk::accent()));
        connect(button, &QPushButton::clicked, this, [this, all]() {
            for (int i = 0; i < m_commands->count(); ++i) {
                m_commands->item(i)->setCheckState(all ? Qt::Checked : Qt::Unchecked);
            }
        });
        commandsHeader->addWidget(button);
    }
    outer->addLayout(commandsHeader);
    outer->addWidget(split, 1);
    if (m_commands->count() > 0) {
        m_commands->setCurrentRow(0);
    }

    // Binários exigidos (manifesto + interpretadores do conteúdo) e URLs chamadas.
    QStringList needs = m.needs;
    for (const QString &i : p.interpreters) {
        if (!needs.contains(i)) needs << i;
    }
    if (!needs.isEmpty()) {
        QStringList lines;
        for (const QString &n : needs) lines << toolLine(n);
        outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.preview.needs")), caption(this, lines.join(QLatin1Char('\n')))));
    }
    if (!p.httpUrls.isEmpty()) {
        outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.preview.urls")), caption(this, p.httpUrls.join(QLatin1Char('\n')))));
    }
    if (p.hooksSkipped > 0) {
        outer->addWidget(caption(this, utils::tr(QStringLiteral("store.preview.hooks_skipped")).arg(p.hooksSkipped)));
    }
    if (!m.secrets.isEmpty()) {
        outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.preview.secrets")), caption(this, m.secrets.join(QStringLiteral(", ")))));
    }
    for (const core::ValidationIssue &w : staged.validation.issues) {
        if (w.severity == core::ValidationSeverity::Warning) {
            outer->addWidget(caption(this, QStringLiteral("%1: %2").arg(w.path, w.message)));
        }
    }

    // Decisões: tudo desligado por padrão.
    auto *decisions = new QWidget(this);
    makeTransparentHost(decisions, QStringLiteral("storeDecisions"));
    auto *decisionsLayout = new QVBoxLayout(decisions);
    decisionsLayout->setContentsMargins(0, 0, 0, 0);
    decisionsLayout->setSpacing(utils::tokens::space(1));
    for (const core::StorePreviewCommand &c : p.commands) {
        if (c.cron.isEmpty() && !c.autoRun) continue;
        Decision d;
        d.name = c.name;
        if (!c.cron.isEmpty()) {
            d.cron = new QCheckBox(utils::tr(QStringLiteral("store.preview.enable_cron")).arg(c.name, c.cron), decisions);
            d.cron->setProperty("kaiRole", QStringLiteral("switch"));
            decisionsLayout->addWidget(d.cron);
        }
        if (c.autoRun) {
            d.autoRun = new QCheckBox(utils::tr(QStringLiteral("store.preview.enable_auto_run")).arg(c.name), decisions);
            d.autoRun->setProperty("kaiRole", QStringLiteral("switch"));
            decisionsLayout->addWidget(d.autoRun);
        }
        m_decisions << d;
    }
    if (!m_decisions.isEmpty()) {
        outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.preview.decisions")), decisions));
    } else {
        decisions->deleteLater();
    }

    // Destino (só para o que vira pasta).
    if (m.kind != QLatin1String("environment") && m.kind != QLatin1String("profile")) {
        m_destination = new FolderPickerWidget(this);
        m_destination->setFolders(folders);
        m_destination->enableNoneOption(utils::tr(QStringLiteral("store.preview.destination.root")));
        outer->addWidget(lh::wrapWithLabel(this, utils::tr(QStringLiteral("store.preview.destination")), m_destination));
    }

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *install = m_installButton = buttons->addButton(utils::tr(QStringLiteral("store.preview.install")), QDialogButtonBox::AcceptRole);
    install->setCursor(Qt::PointingHandCursor);
    install->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: %3px; padding: 8px 22px; font-weight: 600; }"
        "QPushButton:hover { background: %4; }")
        .arg(tk::buttonColor(), tk::buttonFg()).arg(tk::radiusMd()).arg(tk::accent()));
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
    connect(m_commands, &QListWidget::itemChanged, this, [this]() { updateSelection(); });
    updateSelection();
}

void StorePreviewDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    if (m_split) {
        const Qt::Orientation wanted = width() < 560 ? Qt::Vertical : Qt::Horizontal;
        if (m_split->orientation() != wanted) {
            m_split->setOrientation(wanted);
        }
    }
}

bool StorePreviewDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (m_commands && watched == m_commands->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (QListWidgetItem *item = m_commands->itemAt(mouse->pos()); item && (item->flags() & Qt::ItemIsUserCheckable)) {
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
                return true; // consome: senão o clique na caixa marcaria de novo e desfaria
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void StorePreviewDialog::updateSelection()
{
    int checked = 0;
    for (int i = 0; i < m_commands->count(); ++i) {
        if (m_commands->item(i)->checkState() == Qt::Checked) ++checked;
    }
    const int total = m_commands->count();
    m_selectionCount->setVisible(total > 0);
    m_selectionCount->setText(utils::tr(QStringLiteral("store.preview.partial")).arg(checked).arg(total));
    // Sem nenhum comando marcado não há o que instalar (pacotes sem comandos, como ambientes, seguem normais).
    m_installButton->setEnabled(total == 0 || checked > 0);
}

void StorePreviewDialog::fillRequest(core::StoreInstallRequest &request) const
{
    request.secrets = m_staged.manifest.secrets;
    request.kaiFilePath = m_staged.kaiFilePath;
    request.kind = m_staged.manifest.kind;
    request.packageId = m_staged.manifest.id;
    request.version = m_staged.manifest.version;
    request.parentFolderId = m_destination ? m_destination->selectedFolderId() : QString();
    for (int i = 0; i < m_commands->count(); ++i) {
        if (m_commands->item(i)->checkState() != Qt::Checked) {
            request.excludeCommands << m_commands->item(i)->data(Qt::UserRole + 1).toString();
        }
    }
    for (const Decision &d : m_decisions) {
        if (d.cron && d.cron->isChecked()) request.allowCron << d.name;
        if (d.autoRun && d.autoRun->isChecked()) request.allowAutoRun << d.name;
    }
    if (m_staged.manifest.clone) {
        request.cloneDir = m_staged.dir;
    }
}

} // namespace kai::ui
