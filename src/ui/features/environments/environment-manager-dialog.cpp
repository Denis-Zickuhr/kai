#include "ui/features/environments/environment-manager-dialog.h"
#include "ui/shared/token-style.h"
#include <QFile>
#include "ui/shared/file-dialogs.h"
#include "core/dotenv.h"
#include "utils/translation-manager.h"

#include <QBoxLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QUuid>
#include <QSignalBlocker>
#include <QFont>
#include <QColor>
#include <QFrame>
#include <QAbstractItemView>
#include <QStyle>
#include <QStackedWidget>
#include <QKeyEvent>
#include <QTimer>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QToolButton>
#include <QMessageBox>

#include "ui/shared/adaptive-button.h"
#include "ui/features/environments/variables-editor-widget.h"
#include "ui/features/environments/dynamic-vars-inspector-widget.h"
#include "ui/shared/table-utils.h"
#include "ui/features/environments/variable-list-view.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {
namespace tk = utils::tokens;

namespace {
QString makeEnvId()
{
    return QStringLiteral("env_") + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

constexpr int kEnvNameRole = Qt::UserRole + 1;
constexpr int kEnvCountRole = Qt::UserRole + 2;
constexpr int kEnvActiveRole = Qt::UserRole + 3;
constexpr int kEnvRowHeight = 52;
constexpr int kStackBelowWidth = 640;
constexpr int kControlH = 30;

// Uma linha da lista de pacotes: bolinha (accent se ativo), nome em negrito, "N variáveis" embaixo e a etiqueta
// ATIVO à direita. Mesmo desenho da lista do histórico.
class EnvItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(100, kEnvRowHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRect box = option.rect.adjusted(tk::space(1), 2, -tk::space(1), -2);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        if (selected || hover) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(selected ? tk::selBg() : tk::hoverBg()));
            painter->drawRoundedRect(box, tk::radiusMd(), tk::radiusMd());
        }
        const bool active = index.data(kEnvActiveRole).toBool();
        const int dot = 8;
        const QPoint dotCenter(box.left() + tk::space(3), box.center().y());
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(active ? tk::accent() : tk::mutedFg()));
        painter->drawEllipse(dotCenter, dot / 2, dot / 2);

        int right = box.right() - tk::space(2);
        if (active) {
            QFont tagFont = option.font;
            tagFont.setPointSizeF(qMax<qreal>(7.0, tagFont.pointSizeF() * 0.8));
            tagFont.setBold(true);
            const QString tag = utils::tr(QStringLiteral("env.badge.active"));
            const QFontMetrics tagMetrics(tagFont);
            const int tagWidth = tagMetrics.horizontalAdvance(tag) + tk::space(3);
            const QRect tagRect(right - tagWidth, box.center().y() - (tagMetrics.height() + 4) / 2, tagWidth,
                                tagMetrics.height() + 4);
            QColor tint(tk::accent());
            tint.setAlphaF(0.18);
            painter->setBrush(tint);
            painter->drawRoundedRect(tagRect, tk::radiusSm(), tk::radiusSm());
            painter->setFont(tagFont);
            painter->setPen(QColor(tk::accent()));
            painter->drawText(tagRect, Qt::AlignCenter, tag);
            right = tagRect.left() - tk::space(2);
        }

        const int textLeft = dotCenter.x() + dot + tk::space(2);
        const int textWidth = qMax(0, right - textLeft);
        QFont title = option.font;
        title.setBold(true);
        painter->setFont(title);
        painter->setPen(QColor(tk::fg()));
        const QFontMetrics titleMetrics(title);
        const QRect titleRect(textLeft, box.top() + tk::space(1) + 1, textWidth, titleMetrics.height());
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          titleMetrics.elidedText(index.data(kEnvNameRole).toString(), Qt::ElideRight, textWidth));
        QFont meta = option.font;
        meta.setPointSizeF(meta.pointSizeF() * 0.9);
        painter->setFont(meta);
        painter->setPen(QColor(tk::mutedFg()));
        const QFontMetrics metaMetrics(meta);
        const QRect metaRect(textLeft, titleRect.bottom() + 2, textWidth, metaMetrics.height());
        painter->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter,
                          metaMetrics.elidedText(utils::tr(QStringLiteral("env.vars.count")).arg(index.data(kEnvCountRole).toInt()),
                                                 Qt::ElideRight, textWidth));
        painter->restore();
    }
};

QPushButton *makeIconTextButton(QWidget *parent, const QString &iconName, const QString &text)
{
    auto *button = new AdaptiveButton(text, parent);
    button->setIcon(LucideIcons::icon(iconName, QColor(tk::fg()), 16));
    makeCompactButton(button, kControlH);
    return button;
}
} // namespace

EnvironmentManagerDialog::EnvironmentManagerDialog(const QVector<core::Environment> &environments,
                                                   const QString &activeEnvironmentId,
                                                   QWidget *parent,
                                                   core::EnvironmentManager *envManager,
                                                   const QVector<core::Folder> &allFolders,
                                                   std::function<QMap<QString, QMap<QString, QString>>()> loadPersisted,
                                                   std::function<bool(const QMap<QString, QMap<QString, QString>> &)> savePersisted,
                                                   bool embedded)
    : QDialog(parent)
    , m_environments(environments)
    , m_activeEnvironmentId(activeEnvironmentId)
    , m_envManager(envManager)
    , m_allFolders(allFolders)
    , m_loadPersisted(std::move(loadPersisted))
    , m_savePersisted(std::move(savePersisted))
{
    if (embedded) {
        setWindowFlags(Qt::Widget);
    }
    setWindowTitle(utils::tr(QStringLiteral("env.manage.title")));
    setupUi();
    reloadList();
    if (m_list->count() > 0) {
        int sel = 0;
        for (int i = 0; i < m_environments.size(); ++i) {
            if (m_environments.at(i).id == m_activeEnvironmentId) { sel = i; break; }
        }
        m_list->setCurrentRow(sel);
    }
    if (embedded) {
        setEmbedded(true);
        return;
    }
    resize(720, 480);
    centerOnParent(this);
}

void EnvironmentManagerDialog::setEmbedded(bool embedded)
{
    m_embedded = embedded;
    setWindowFlags(embedded ? Qt::Widget : Qt::Dialog);
    setProperty("kaiEmbedded", embedded);
    if (m_buttonBox) {
        m_buttonBox->setVisible(!embedded);
    }
    if (layout()) {
        const int margin = embedded ? tk::space(2) : tk::space(4);
        layout()->setContentsMargins(margin, margin, margin, embedded ? margin : tk::space(3));
    }
    if (m_list) {
        m_list->setMinimumWidth(embedded ? 160 : 180);
    }
}

void EnvironmentManagerDialog::done(int result)
{
    if (!m_embedded) {
        QDialog::done(result);
    }
}

// Esc fecha um diálogo, mas não um painel do quadro.
void EnvironmentManagerDialog::keyPressEvent(QKeyEvent *event)
{
    if (m_embedded && event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}

// Embutido: entrega as edições sozinha, depois de uma pausa (digitar um nome não grava a cada letra).
void EnvironmentManagerDialog::scheduleChanged()
{
    if (!m_embedded || m_loadingEditor) {
        return;
    }
    if (!m_changedTimer) {
        m_changedTimer = new QTimer(this);
        m_changedTimer->setSingleShot(true);
        m_changedTimer->setInterval(500);
        connect(m_changedTimer, &QTimer::timeout, this, [this]() {
            commitCurrentEditor();
            reloadList(); // nomes e contagens dos cartões acompanham
            emit changed();
        });
    }
    m_changedTimer->start();
}

void EnvironmentManagerDialog::setFolders(const QVector<core::Folder> &folders)
{
    m_allFolders = folders;
    if (m_dynamicVarsInspector) {
        m_dynamicVarsInspector->setFolders(folders);
    }
}

void EnvironmentManagerDialog::setData(const QVector<core::Environment> &environments, const QString &activeEnvironmentId)
{
    if (m_changedTimer) {
        m_changedTimer->stop();
    }
    m_environments = environments;
    m_activeEnvironmentId = activeEnvironmentId;
    m_editingIndex = -1;
    reloadList();
    int sel = 0;
    for (int i = 0; i < m_environments.size(); ++i) {
        if (m_environments.at(i).id == m_activeEnvironmentId) {
            sel = i;
            break;
        }
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(sel);
    }
    handleSelectionChanged();
}

void EnvironmentManagerDialog::setupUi()
{
    setObjectName(QStringLiteral("variablesDialog"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::space(4), tk::space(4), tk::space(4), tk::space(3));
    outer->setSpacing(tk::space(2));

    QPushButton *packagesButton = nullptr;
    QPushButton *dynamicButton = nullptr;
    if (m_envManager) {
        auto *segmented = new QWidget(this);
        segmented->setObjectName(QStringLiteral("envSegments"));
        segmented->setAttribute(Qt::WA_StyledBackground, true);
        auto *segmentLayout = new QHBoxLayout(segmented);
        segmentLayout->setContentsMargins(2, 2, 2, 2);
        segmentLayout->setSpacing(2);
        packagesButton = new QPushButton(utils::tr(QStringLiteral("env.tab.packages")), segmented);
        dynamicButton = new QPushButton(utils::tr(QStringLiteral("env.tab.dynamic_vars")), segmented);
        for (QPushButton *button : {packagesButton, dynamicButton}) {
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setFocusPolicy(Qt::NoFocus);
            segmentLayout->addWidget(button);
        }
        packagesButton->setChecked(true);
        auto *segmentRow = new QHBoxLayout();
        segmentRow->addWidget(segmented);
        segmentRow->addStretch(1);
        outer->addLayout(segmentRow);
    }

    auto *pages = new QStackedWidget(this);
    outer->addWidget(pages, 1);

    m_splitter = new QSplitter(Qt::Horizontal, pages);
    m_splitter->setObjectName(QStringLiteral("envSplitter"));
    m_splitter->setChildrenCollapsible(false);
    pages->addWidget(m_splitter);

    auto *listCard = new QWidget(m_splitter);
    listCard->setObjectName(QStringLiteral("envListCard"));
    listCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(tk::space(2), tk::space(3), tk::space(2), tk::space(2));
    listLayout->setSpacing(tk::space(2));

    auto *listHeader = new QHBoxLayout();
    listHeader->setContentsMargins(tk::space(1), 0, 0, 0);
    auto *listTitle = new QLabel(utils::tr(QStringLiteral("env.sidebar.title")), listCard);
    listTitle->setObjectName(QStringLiteral("envListTitle"));
    listHeader->addWidget(listTitle, 1);
    auto *newButton = makeIconTextButton(listCard, QStringLiteral("plus"), utils::tr(QStringLiteral("env.action.new")));
    newButton->setProperty("kaiRole", QStringLiteral("primary"));
    newButton->setIcon(LucideIcons::icon(QStringLiteral("plus"), QColor(tk::buttonFg()), 16));
    connect(newButton, &QPushButton::clicked, this, &EnvironmentManagerDialog::handleNewEnvironment);
    listHeader->addWidget(newButton);
    listLayout->addLayout(listHeader);

    m_list = new QListWidget(listCard);
    m_list->setObjectName(QStringLiteral("envPackageList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setItemDelegate(new EnvItemDelegate(m_list));
    m_list->setMinimumWidth(180);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { handleSelectionChanged(); });
    listLayout->addWidget(m_list, 1);
    m_splitter->addWidget(listCard);

    auto *detailCard = new QWidget(m_splitter);
    detailCard->setObjectName(QStringLiteral("envDetailCard"));
    detailCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *detail = new QVBoxLayout(detailCard);
    detail->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    detail->setSpacing(tk::space(2));

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(tk::space(3));
    m_nameField = new QLineEdit(detailCard);
    m_nameField->setFixedHeight(kControlH);
    bindFieldStyle(m_nameField, [](int radius) { return QStringLiteral("QLineEdit { min-height: 0px; padding: 0px %1px; border-radius: %2px; }")
        .arg(tk::space(3)).arg(radius); });
    titleRow->addWidget(layout_helpers::wrapWithLabel(detailCard, utils::tr(QStringLiteral("env.field.name")), m_nameField), 1);

    m_activeToggle = new QCheckBox(detailCard);
    m_activeToggle->setProperty("kaiRole", QStringLiteral("switch"));
    connect(m_activeToggle, &QCheckBox::toggled, this, &EnvironmentManagerDialog::handleActiveToggled);
    titleRow->addWidget(layout_helpers::wrapWithLabel(detailCard, utils::tr(QStringLiteral("env.field.active_toggle")),
                                                      m_activeToggle), 0, Qt::AlignBottom);

    m_dupButton = makeIconButton(detailCard, QStringLiteral("copy"), utils::tr(QStringLiteral("env.action.duplicate")),
                                 QColor(tk::mutedFg()));
    m_delButton = makeIconButton(detailCard, QStringLiteral("trash-2"), utils::tr(QStringLiteral("env.action.delete")),
                                 QColor(tk::errorFg()));
    connect(m_dupButton, &QToolButton::clicked, this, &EnvironmentManagerDialog::handleDuplicateEnvironment);
    connect(m_delButton, &QToolButton::clicked, this, &EnvironmentManagerDialog::handleDeleteEnvironment);
    auto *actionsBox = new QHBoxLayout();
    actionsBox->setSpacing(tk::space(1));
    actionsBox->addWidget(m_dupButton);
    actionsBox->addWidget(m_delButton);
    titleRow->addLayout(actionsBox);
    titleRow->setAlignment(actionsBox, Qt::AlignBottom);
    detail->addLayout(titleRow);

    m_meta = new QLabel(detailCard);
    m_meta->setObjectName(QStringLiteral("envMeta"));
    detail->addWidget(m_meta);

    m_varsEditor = new VariablesEditorWidget(detailCard);
    auto *importButton = makeIconTextButton(m_varsEditor, QStringLiteral("download"), utils::tr(QStringLiteral("env.import_dotenv")));
    importButton->setToolTip(utils::tr(QStringLiteral("env.import_dotenv.tip")));
    connect(importButton, &QPushButton::clicked, this, [this]() {
        if (currentIndex() < 0) {
            return;
        }
        const QString path = pickOpenFile(this, utils::tr(QStringLiteral("env.import_dotenv")),
                                          utils::tr(QStringLiteral("env.import_dotenv.filter")));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("env.import_dotenv")), file.errorString());
            return;
        }
        const QMap<QString, QString> vars = core::parseDotenv(QString::fromUtf8(file.readAll()));
        const int merged = m_varsEditor->mergeValues(vars);
        QMessageBox::information(this, utils::tr(QStringLiteral("env.import_dotenv")),
                                 utils::tr(QStringLiteral("env.import_dotenv.done")).arg(merged));
    });
    m_varsEditor->addToolbarWidget(importButton);
    detail->addWidget(m_varsEditor, 1);
    m_splitter->addWidget(detailCard);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({240, 520});

    if (m_envManager) {
        m_dynamicVarsInspector = new DynamicVarsInspectorWidget(
            *m_envManager, m_allFolders, m_loadPersisted, m_savePersisted, pages);
        pages->addWidget(m_dynamicVarsInspector);
        connect(packagesButton, &QPushButton::clicked, this, [pages, packagesButton, dynamicButton, this]() {
            packagesButton->setChecked(true);
            dynamicButton->setChecked(false);
            pages->setCurrentWidget(m_splitter);
        });
        connect(dynamicButton, &QPushButton::clicked, this, [pages, packagesButton, dynamicButton, this]() {
            packagesButton->setChecked(false);
            dynamicButton->setChecked(true);
            pages->setCurrentWidget(m_dynamicVarsInspector);
            // As dinâmicas podem ter mudado desde a abertura (um comando rodou antes desta aba ser vista).
            m_dynamicVarsInspector->refresh();
        });
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(m_buttonBox);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        commitCurrentEditor();
        accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(m_buttonBox);
    connect(m_nameField, &QLineEdit::textEdited, this, [this]() { scheduleChanged(); });
    connect(m_varsEditor, &VariablesEditorWidget::changed, this, [this]() {
        updateMeta();
        scheduleChanged();
    });
    applyStyle();
}

// Cartões, segmentado e rótulos seguem os tokens (cores e raio da preferência de cantos): nunca um raio fixo.
void EnvironmentManagerDialog::applyStyle()
{
    const int radius = tk::radiusMd();
    const int small = tk::radiusSm();
    const QString card = QStringLiteral("QWidget#%1 { background-color: %2; border: 1px solid %3; border-radius: %4px; }");
    bindTokenStyle(this, [=, this]() { return 
        card.arg(QStringLiteral("envListCard"), tk::surface(), tk::borderColor()).arg(radius)
        + card.arg(QStringLiteral("envDetailCard"), tk::surface(), tk::borderColor()).arg(radius)
        + QStringLiteral("QSplitter#envSplitter::handle { background-color: %1; border: none; }").arg(tk::bg())
        + QStringLiteral("QListWidget#envPackageList { background: transparent; border: none; outline: none; }")
        + QStringLiteral("QLabel#envListTitle { background: transparent; color: %1; font-size: %2pt; font-weight: 600;"
                         " letter-spacing: 0.5px; }").arg(tk::mutedFg()).arg(tk::fontSizeSmallPt())
        + QStringLiteral("QLabel#envMeta { background: transparent; color: %1; font-size: %2pt; }")
              .arg(tk::mutedFg()).arg(tk::fontSizeSmallPt())
        + QStringLiteral("QWidget#envSegments { background-color: %1; border-radius: %2px; }").arg(tk::bg()).arg(radius)
        + QStringLiteral("QWidget#envSegments QPushButton { border: none; border-radius: %1px; padding: %2px %3px;"
                         " font-weight: 600; background: transparent; color: %4; min-height: 0px; }"
                         "QWidget#envSegments QPushButton:checked { background-color: %5; color: %6; }"
                         "QWidget#envSegments QPushButton:hover:!checked { background-color: %7; }")
              .arg(small).arg(tk::space(1)).arg(tk::space(3)).arg(tk::mutedFg(), tk::accent(), tk::bg(), tk::hoverBg()); });
}

void EnvironmentManagerDialog::updateOrientation()
{
    if (!m_splitter) {
        return;
    }
    const Qt::Orientation wanted = width() < kStackBelowWidth ? Qt::Vertical : Qt::Horizontal;
    if (m_splitter->orientation() != wanted) {
        m_splitter->setOrientation(wanted);
        m_splitter->setSizes(wanted == Qt::Vertical ? QList<int>{180, 360} : QList<int>{240, 520});
    }
}

void EnvironmentManagerDialog::updateMeta()
{
    if (m_meta) {
        m_meta->setText(utils::tr(QStringLiteral("env.vars.count")).arg(m_varsEditor ? m_varsEditor->values().size() : 0));
    }
}

void EnvironmentManagerDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateOrientation();
}

void EnvironmentManagerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    applyStyle();
    updateOrientation();
}

int EnvironmentManagerDialog::currentIndex() const
{
    return m_list ? m_list->currentRow() : -1;
}

void EnvironmentManagerDialog::reloadList()
{
    const QSignalBlocker blocker(m_list);
    const int keep = m_list->currentRow();
    m_list->clear();
    for (const core::Environment &e : m_environments) {
        auto *item = new QListWidgetItem(m_list);
        item->setData(kEnvNameRole, e.name);
        item->setData(kEnvCountRole, e.vars.size());
        item->setData(kEnvActiveRole, e.id == m_activeEnvironmentId);
        item->setToolTip(e.name);
    }
    if (keep >= 0 && keep < m_list->count()) {
        m_list->setCurrentRow(keep);
    }
}

void EnvironmentManagerDialog::commitCurrentEditor()
{
    if (m_editingIndex >= 0 && m_editingIndex < m_environments.size()) {
        m_environments[m_editingIndex].name = m_nameField->text().trimmed();
        m_environments[m_editingIndex].vars = m_varsEditor->values();
        m_environments[m_editingIndex].secretKeys = m_varsEditor->secretKeys();
    }
}

void EnvironmentManagerDialog::handleSelectionChanged()
{
    commitCurrentEditor();

    const int index = currentIndex();
    m_editingIndex = index;

    if (index < 0 || index >= m_environments.size()) {
        m_nameField->clear();
        m_varsEditor->setValuesWithSecrets({}, {});
        m_nameField->setEnabled(false);
        m_varsEditor->setEnabled(false);
        m_activeToggle->setEnabled(false);
        updateMeta();
        return;
    }
    m_nameField->setEnabled(true);
    m_varsEditor->setEnabled(true);
    m_activeToggle->setEnabled(true);
    const core::Environment &e = m_environments.at(index);
    m_loadingEditor = true;
    m_nameField->setText(e.name);
    m_varsEditor->setValuesWithSecrets(e.vars, e.secretKeys);
    m_loadingEditor = false;
    {
        const QSignalBlocker blocker(m_activeToggle);
        m_activeToggle->setChecked(e.id == m_activeEnvironmentId);
    }
    updateMeta();
}

void EnvironmentManagerDialog::handleNewEnvironment()
{
    commitCurrentEditor();
    core::Environment e;
    e.id = makeEnvId();
    e.name = utils::tr(QStringLiteral("env.new.default_name"));
    m_environments.append(e);
    reloadList();
    m_list->setCurrentRow(m_environments.size() - 1);
    m_nameField->setFocus();
    m_nameField->selectAll();
    scheduleChanged();
}

void EnvironmentManagerDialog::handleDuplicateEnvironment()
{
    commitCurrentEditor();
    const int index = currentIndex();
    if (index < 0 || index >= m_environments.size()) {
        return;
    }
    core::Environment copy = m_environments.at(index);
    copy.id = makeEnvId();
    copy.name = copy.name + utils::tr(QStringLiteral("duplicate.name_suffix"));
    m_environments.insert(index + 1, copy);
    reloadList();
    m_list->setCurrentRow(index + 1);
    scheduleChanged();
}

void EnvironmentManagerDialog::handleDeleteEnvironment()
{
    const int index = currentIndex();
    if (index < 0 || index >= m_environments.size()) {
        return;
    }
    // Não permite ficar sem nenhum pacote.
    if (m_environments.size() <= 1) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("env.manage.title")),
                             utils::tr(QStringLiteral("env.delete.last_warning")));
        return;
    }
    const QString name = m_environments.at(index).name;
    if (!confirmYesNo(this, utils::tr(QStringLiteral("env.action.delete")),
                      utils::tr(QStringLiteral("env.delete.confirm")).arg(name))) {
        return;
    }
    const bool wasActive = (m_environments.at(index).id == m_activeEnvironmentId);
    m_environments.remove(index);
    m_editingIndex = -1;
    if (wasActive) {
        m_activeEnvironmentId = m_environments.first().id;
    }
    reloadList();
    m_list->setCurrentRow(qMin(index, m_environments.size() - 1));
    scheduleChanged();
}

void EnvironmentManagerDialog::handleActiveToggled(bool checked)
{
    const int index = currentIndex();
    if (index < 0 || index >= m_environments.size()) {
        return;
    }
    const QString id = m_environments.at(index).id;
    if (checked) {
        m_activeEnvironmentId = id;
        reloadList(); // atualiza bolinha/pill "ATIVO" dos outros cartões
        scheduleChanged();
    } else if (id == m_activeEnvironmentId) {
        // Não dá pra ficar sem NENHUM ativo — precisa ativar outro pacote
        // em vez de simplesmente desligar este. Reverte o toggle.
        const QSignalBlocker blocker(m_activeToggle);
        m_activeToggle->setChecked(true);
    }
}

} // namespace kai::ui
