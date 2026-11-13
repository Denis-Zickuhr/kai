#include "ui/features/store/store-window.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMap>
#include <QPointer>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

#include "core/store-texts.h"
#include "ui/features/store/store-preview-dialog.h"
#include "ui/layout/panel-card.h"
#include "ui/shared/panel-metrics.h"
#include "ui/shared/flow-layout.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace tk = utils::tokens;

namespace {

constexpr int kTile = 44;
constexpr qint64 kCacheBudgetBytes = 32LL * 1024 * 1024;

class ClickableFrame : public QFrame {
public:
    explicit ClickableFrame(std::function<void()> onClick, QWidget *parent = nullptr)
        : QFrame(parent), m_onClick(std::move(onClick))
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && m_onClick) {
            m_onClick();
        }
        QFrame::mouseReleaseEvent(event);
    }

public:
    void setOnClick(std::function<void()> onClick) { m_onClick = std::move(onClick); }

private:
    std::function<void()> m_onClick;
};

// Markdown do pacote, 100% por dentro: sem HTML, sem link que abre, e as imagens só vêm do que o provedor serviu
// (carregadas antes, por StoreClient); qualquer outra referência (arquivo local, http) não carrega.
class MarkdownView : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;
    QMap<QString, QPixmap> images;

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource) {
            const auto it = images.constFind(name.toString());
            if (it != images.constEnd()) {
                return it.value();
            }
        }
        return QVariant();
    }
};

// Rolagem dentro da moldura "panelCard" do quadro principal (borda e raio dos tokens, barra global do app).
QWidget *framedScroll(QScrollArea *scroll)
{
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // O viewport do tema é um retângulo chapado e os cantos vazavam para fora do arco da moldura: quem pinta o fundo é a
    // própria área (com o raio interno do cartão) e o viewport fica transparente.
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: %1; border: none; border-radius: %2px; }"
                                         "QScrollArea QWidget#qt_scrollarea_viewport { background: transparent; }")
                              .arg(tk::bg()).arg(panelInnerRadius()));
    scroll->viewport()->setAutoFillBackground(false);
    QWidget *card = layout::wrapInPanelCard(scroll);
    card->setObjectName(QStringLiteral("nestedPanelCard"));
    return card;
}

QString tint(const QString &color, double alpha)
{
    const QColor c(color);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// Chip discreto: texto na cor do papel sobre um fundo da mesma cor bem translúcido, sem borda.
QLabel *makeChip(QWidget *parent, const QString &text, const QString &color)
{
    auto *chip = new QLabel(text, parent);
    chip->setStyleSheet(QStringLiteral("QLabel { color: %1; background: %2; border: none; border-radius: %3px;"
                                       " padding: 1px 8px; font-size: %4pt; }")
                            .arg(color, tint(color, 0.14)).arg(tk::radiusSm()).arg(tk::fontSizeSmallPt()));
    // Nunca encolhe nem corta o texto: se faltar espaço, quem manda é o layout do card.
    chip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return chip;
}

// Ícone do pacote num bloco arredondado.
QLabel *makeTile(QWidget *parent, int size)
{
    auto *tile = new QLabel(parent);
    tile->setFixedSize(size, size);
    tile->setAlignment(Qt::AlignCenter);
    tile->setStyleSheet(QStringLiteral("QLabel { background: %1; border-radius: %2px; }").arg(tint(tk::accent(), 0.12)).arg(tk::radiusMd()));
    tile->setPixmap(LucideIcons::icon(QStringLiteral("package"), QColor(tk::accent()), size / 2).pixmap(size / 2, size / 2));
    return tile;
}

QString trustColor(core::StoreTrust trust)
{
    switch (trust) {
    case core::StoreTrust::Official: return tk::accent();
    case core::StoreTrust::Verified: return tk::successFg();
    case core::StoreTrust::Community: return tk::infoFg();
    case core::StoreTrust::Custom: return tk::mutedFg();
    }
    return tk::mutedFg();
}

QString signatureText(engine::StoreSignatureState s)
{
    switch (s) {
    case engine::StoreSignatureState::None: return utils::tr(QStringLiteral("store.signature.none"));
    case engine::StoreSignatureState::Ok: return utils::tr(QStringLiteral("store.signature.ok"));
    case engine::StoreSignatureState::Invalid: return utils::tr(QStringLiteral("store.signature.invalid"));
    }
    return {};
}

// Botão de ação legível em qualquer tema: cor do botão primário do tema, texto no contraste certo.
QString primaryButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: %3px; padding: 8px 22px; font-weight: 600; }"
        "QPushButton:hover { background: %4; }"
        "QPushButton:disabled { background: %5; color: %6; }")
        .arg(tk::buttonColor(), tk::buttonFg()).arg(tk::radiusMd()).arg(tint(tk::buttonColor(), 0.85),
                                                                          tint(tk::buttonColor(), 0.25), tk::mutedFg());
}

bool isRasterPath(const QString &path)
{
    const QString p = path.toLower();
    return p.endsWith(QLatin1String(".png")) || p.endsWith(QLatin1String(".jpg")) || p.endsWith(QLatin1String(".jpeg"))
        || p.endsWith(QLatin1String(".webp")) || p.endsWith(QLatin1String(".gif"));
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        if (QLayout *child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}

} // namespace

StoreWindow::StoreWindow(const QVector<core::StoreProviderConfig> &providers, const Host &host, QWidget *parent)
    : QDialog(parent), m_host(host)
{
    for (const core::StoreProviderConfig &p : providers) {
        if (p.enabled) m_providers << p;
    }
    setModal(false);
    setSizeGripEnabled(true);
    setWindowTitle(utils::tr(QStringLiteral("store.window.title")));
    resize(1000, 700);
    setupUi();
    if (!m_providers.isEmpty()) {
        selectProvider(0);
    }
}

void StoreWindow::setEmbedded(bool embedded)
{
    m_embedded = embedded;
    setWindowFlags(embedded ? Qt::Widget : Qt::Dialog);
    setProperty("kaiEmbedded", embedded);
    setSizeGripEnabled(!embedded);
}

void StoreWindow::done(int result)
{
    if (!m_embedded) {
        QDialog::done(result);
    }
}

void StoreWindow::keyPressEvent(QKeyEvent *event)
{
    if (m_embedded && event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}

void StoreWindow::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    constexpr int kCompactWidth = 760;
    const bool compact = width() < kCompactWidth;
    if (compact != m_compact) {
        m_compact = compact;
        applyResponsive();
    }
}

void StoreWindow::applyResponsive()
{
    if (!m_topRow || !m_pages || !m_gridCard || !m_detailCard || !m_backButton || !m_categoryCombo || !m_filterLayout || !m_refreshButton) {
        return;
    }
    const bool onGrid = m_pages->currentIndex() == 0;
    // Linha de cima: provedor, busca e atualizar; os filtros ficam nela na tela larga e descem na pequena.
    for (QWidget *w : {static_cast<QWidget *>(m_providerCombo), static_cast<QWidget *>(m_search), static_cast<QWidget *>(m_kindFilter),
                       static_cast<QWidget *>(m_statusFilter), static_cast<QWidget *>(m_categoryCombo),
                       static_cast<QWidget *>(m_refreshButton)}) {
        m_topRow->removeWidget(w);
        m_filterLayout->removeWidget(w);
    }
    m_topRow->addWidget(m_providerCombo);
    m_topRow->addWidget(m_search, 1);
    if (m_compact) {
        m_filterLayout->addWidget(m_kindFilter, 1);
        m_filterLayout->addWidget(m_statusFilter, 1);
        m_filterLayout->addWidget(m_categoryCombo, 1);
    } else {
        m_topRow->addWidget(m_kindFilter);
        m_topRow->addWidget(m_statusFilter);
    }
    m_topRow->addWidget(m_refreshButton);
    m_refreshButton->setText(m_compact ? QString() : utils::tr(QStringLiteral("store.window.refresh")));
    m_refreshButton->setToolTip(m_compact ? utils::tr(QStringLiteral("store.window.refresh")) : QString());

    m_kindFilter->setVisible(onGrid);
    m_statusFilter->setVisible(onGrid);
    m_categoryList->setVisible(onGrid && !m_compact);
    m_categoryCombo->setVisible(onGrid && m_compact);
    m_filterRow->setVisible(onGrid && m_compact);

    const bool showingDetail = !m_detailId.isEmpty();
    m_gridCard->setMinimumWidth(m_compact ? 0 : 320);
    m_gridCard->setVisible(!(m_compact && showingDetail));
    m_detailCard->setVisible(!m_compact || showingDetail);
    m_backButton->setVisible(m_compact && showingDetail);
}

void StoreWindow::setProviders(const QVector<core::StoreProviderConfig> &providers)
{
    cancelInstall();
    m_providers.clear();
    for (const core::StoreProviderConfig &p : providers) {
        if (p.enabled) m_providers << p;
    }
    m_providerIndex = -1;
    m_providerCombo->clear();
    for (const core::StoreProviderConfig &p : std::as_const(m_providers)) m_providerCombo->addItem(p.name);
    if (!m_providers.isEmpty()) {
        selectProvider(0);
    }
}

StoreWindow::~StoreWindow()
{
    cancelInstall();
}

void StoreWindow::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(2));
    root->setSpacing(tk::space(2));

    auto *top = new QHBoxLayout();
    m_topRow = top;
    m_providerCombo = new QComboBox(this);
    for (const core::StoreProviderConfig &p : m_providers) m_providerCombo->addItem(p.name);
    connect(m_providerCombo, &QComboBox::activated, this, [this](int i) { selectProvider(i); });
    m_providerCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_providerCombo->setMinimumContentsLength(6);
    top->addWidget(m_providerCombo);
    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(utils::tr(QStringLiteral("store.window.search")));
    connect(m_search, &QLineEdit::textChanged, this, [this]() {
        rebuildGrid();
        rebuildManage();
    });
    top->addWidget(m_search, 1);

    // Filtros da grade: tipo e situação.
    m_kindFilter = new QComboBox(this);
    m_kindFilter->addItem(utils::tr(QStringLiteral("store.filter.all_kinds")), QString());
    for (const QString &kind : core::storePackageKinds()) {
        m_kindFilter->addItem(core::storeKindLabel(kind), kind);
    }
    connect(m_kindFilter, &QComboBox::activated, this, [this]() {
        showGrid();
        rebuildGrid();
    });
    m_kindFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_kindFilter->setMinimumContentsLength(6);
    m_statusFilter = new QComboBox(this);
    m_statusFilter->addItem(utils::tr(QStringLiteral("store.filter.any_status")), QStringLiteral("any"));
    m_statusFilter->addItem(utils::tr(QStringLiteral("store.filter.not_installed")), QStringLiteral("new"));
    m_statusFilter->addItem(utils::tr(QStringLiteral("store.filter.installed")), QStringLiteral("installed"));
    m_statusFilter->addItem(utils::tr(QStringLiteral("store.filter.updates")), QStringLiteral("updates"));
    connect(m_statusFilter, &QComboBox::activated, this, [this]() {
        showGrid();
        rebuildGrid();
    });
    m_statusFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_statusFilter->setMinimumContentsLength(6);
    auto *refresh = new QPushButton(utils::tr(QStringLiteral("store.window.refresh")), this);
    m_refreshButton = refresh;
    refresh->setIcon(LucideIcons::icon(QStringLiteral("refresh-cw"), QColor(tk::mutedFg()), 16));
    refresh->setCursor(Qt::PointingHandCursor);
    refresh->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: %3px; padding: 7px 14px; }"
        "QPushButton:hover { background: %4; border-color: %5; }")
        .arg(tk::fg(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::hoverBg(), tk::accent()));
    connect(refresh, &QPushButton::clicked, this, [this]() { reload(); });
    root->addLayout(top);
    // Segunda linha (só em telas pequenas): os filtros descem para cá; applyResponsive() distribui os widgets.
    m_filterRow = new QWidget(this);
    m_filterRow->setObjectName(QStringLiteral("storeFilterRow"));
    m_filterRow->setStyleSheet(QStringLiteral("QWidget#storeFilterRow { background: transparent; }"));
    m_filterLayout = new QHBoxLayout(m_filterRow);
    m_filterLayout->setContentsMargins(0, 0, 0, 0);
    m_filterLayout->setSpacing(tk::space(2));
    m_filterRow->setVisible(false);
    root->addWidget(m_filterRow);
    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categoryCombo->setMinimumContentsLength(6);
    connect(m_categoryCombo, &QComboBox::activated, this, [this](int row) { m_categoryList->setCurrentRow(row); });

    m_tabs = new QTabBar(this);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setDocumentMode(true);
    m_tabs->setCursor(Qt::PointingHandCursor);
    m_tabs->setStyleSheet(QStringLiteral(
        "QTabBar { background: transparent; border: none; }"
        "QTabBar::tab { background: transparent; color: %1; border: none; border-bottom: 2px solid transparent;"
        " padding: 8px 4px; margin-right: 22px; font-weight: 500; }"
        "QTabBar::tab:hover { color: %2; }"
        "QTabBar::tab:selected { color: %2; border-bottom: 2px solid %3; font-weight: 600; }")
        .arg(tk::mutedFg(), tk::fg(), tk::accent()));
    m_tabs->addTab(utils::tr(QStringLiteral("store.window.tab_browse")));
    m_tabs->addTab(utils::tr(QStringLiteral("store.window.tab_installed")));
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index == 1) {
            showManage();
        } else {
            showGrid();
        }
    });
    root->addWidget(m_tabs);

    // Aviso de falha do provedor (login recusado, rede, assinatura): visível acima da lista, não só na barra de status.
    m_banner = new QLabel(this);
    m_banner->setWordWrap(true);
    m_banner->setVisible(false);
    m_banner->setStyleSheet(QStringLiteral("QLabel { color: %1; background: %2; border: 1px solid %3; border-radius: %4px; padding: 10px 14px; }")
                                .arg(tk::errorFg(), tint(tk::errorFg(), 0.12), tint(tk::errorFg(), 0.4)).arg(tk::radiusMd()));
    root->addWidget(m_banner);

    auto *body = new QHBoxLayout();
    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(190);
    m_categoryList->setFrameShape(QFrame::NoFrame);
    m_categoryList->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { padding: 7px 12px; border-radius: %1px; color: %2; }"
        "QListWidget::item:hover { background: %3; }"
        "QListWidget::item:selected { background: %4; color: %5; }")
        .arg(tk::radiusSm()).arg(tk::mutedFg(), tk::hoverBg(), tint(tk::accent(), 0.16), tk::accent()));
    connect(m_categoryList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        m_category = m_categoryList->item(row)->data(Qt::UserRole).toString();
        if (m_categoryCombo->currentIndex() != row) {
            m_categoryCombo->setCurrentIndex(row);
        }
        showGrid();
        rebuildGrid();
    });
    body->addWidget(m_categoryList);

    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("storePages"));
    auto *gridScroll = new QScrollArea(m_pages);
    m_gridHost = new QWidget();
    m_gridHost->setObjectName(QStringLiteral("storeGridHost"));
    m_gridHost->setStyleSheet(QStringLiteral("QWidget#storeGridHost { background: transparent; }"));
    m_gridLayout = new QVBoxLayout(m_gridHost);
    m_gridLayout->setContentsMargins(tk::space(2), tk::space(2), tk::space(2), tk::space(2)); // as linhas não encostam nos cantos do painel
    m_gridLayout->setSpacing(2);
    gridScroll->setWidget(m_gridHost);
    auto *browse = new QSplitter(Qt::Horizontal, m_pages);
    browse->setObjectName(QStringLiteral("storeSplitter")); // sem linha divisória: os dois painéis já têm moldura
    browse->setChildrenCollapsible(false);
    m_gridCard = framedScroll(gridScroll);
    browse->addWidget(m_gridCard);
    m_gridCard->setMinimumWidth(320);

    auto *detailScroll = new QScrollArea(m_pages);
    m_detailHost = new QWidget();
    m_detailHost->setObjectName(QStringLiteral("storeDetailHost"));
    m_detailHost->setStyleSheet(QStringLiteral("QWidget#storeDetailHost, QWidget#storeDetailHost QLabel { background: transparent; }"));
    m_detailLayout = new QVBoxLayout(m_detailHost);
    m_detailLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
    m_detailLayout->setSpacing(tk::space(2));
    detailScroll->setWidget(m_detailHost);
    m_detailCard = framedScroll(detailScroll);
    browse->addWidget(m_detailCard);
    browse->setSizes({390, 560});
    browse->setStretchFactor(1, 1);
    // Tela pequena: lista e detalhe alternam, e "Voltar" (só visível com o detalhe aberto) devolve à lista.
    auto *browsePage = new QWidget(m_pages);
    auto *browseLayout = new QVBoxLayout(browsePage);
    browseLayout->setContentsMargins(0, 0, 0, 0);
    browseLayout->setSpacing(tk::space(1));
    m_backButton = new QPushButton(utils::tr(QStringLiteral("store.window.back")), browsePage);
    m_backButton->setIcon(LucideIcons::icon(QStringLiteral("arrow-left"), QColor(tk::mutedFg()), 16));
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setFlat(true);
    m_backButton->setVisible(false);
    connect(m_backButton, &QPushButton::clicked, this, [this]() { showPlaceholder(); });
    auto *backRow = new QHBoxLayout();
    backRow->addWidget(m_backButton);
    backRow->addStretch(1);
    browseLayout->addLayout(backRow);
    browseLayout->addWidget(browse, 1);
    m_pages->addWidget(browsePage);

    auto *manageScroll = new QScrollArea(m_pages);
    m_manageHost = new QWidget();
    m_manageHost->setObjectName(QStringLiteral("storeManageHost"));
    m_manageHost->setStyleSheet(QStringLiteral("QWidget#storeManageHost, QWidget#storeManageHost QLabel { background: transparent; }"));
    m_manageLayout = new QVBoxLayout(m_manageHost);
    m_manageLayout->setContentsMargins(tk::space(2), tk::space(2), tk::space(2), tk::space(2));
    m_manageLayout->setSpacing(tk::space(2));
    manageScroll->setWidget(m_manageHost);
    m_pages->addWidget(framedScroll(manageScroll));
    body->addWidget(m_pages, 1);
    root->addLayout(body, 1);

    auto *bottom = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setProperty("kaiRole", QStringLiteral("caption"));
    m_status->setWordWrap(true);
    bottom->addWidget(m_status, 1);
    m_cancelButton = new QPushButton(utils::tr(QStringLiteral("store.window.cancel")), this);
    m_cancelButton->hide();
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { cancelInstall(); });
    bottom->addWidget(m_cancelButton);
    root->addLayout(bottom);
    applyResponsive();
}

void StoreWindow::setStatus(const QString &text)
{
    m_status->setText(text);
}

void StoreWindow::selectProvider(int index)
{
    if (index < 0 || index >= m_providers.size()) {
        return;
    }
    cancelInstall();
    m_providerIndex = index;
    if (m_providerCombo->currentIndex() != index) m_providerCombo->setCurrentIndex(index);
    reload();
}

void StoreWindow::reload()
{
    if (m_providerIndex < 0) {
        return;
    }
    m_installer.reset(); // aponta para o cliente que vai sair
    m_busy = false;
    m_cancelButton->hide();
    if (m_client) {
        m_client->cancel();
        m_client->deleteLater();
        m_client = nullptr;
    }
    m_result = engine::StoreCatalogResult();
    m_banner->setVisible(false);
    rebuildGrid();
    const core::StoreProviderConfig &p = m_providers.at(m_providerIndex);
    engine::StoreClient::Options options;
    options.cacheDir = core::storeCacheDir(p.id);
    m_client = new engine::StoreClient(p, options, this);
    setStatus(utils::tr(QStringLiteral("store.window.loading")));
    QPointer<engine::StoreClient> client = m_client;
    m_client->loadCatalog([this, client](const engine::StoreCatalogResult &r) {
        if (!client || client != m_client) {
            return;
        }
        m_result = r;
        recordsChanged();
        m_banner->setVisible(false);
        if (!r.ok) {
            const QString text = core::storeErrorText(r.error, r.errorDetail);
            setStatus(text);
            m_banner->setText(utils::tr(QStringLiteral("store.window.provider_failed")).arg(m_providers.at(m_providerIndex).name, text));
            m_banner->setVisible(true);
        } else if (r.signature == engine::StoreSignatureState::Invalid) {
            const QString text = utils::tr(QStringLiteral("store.window.signature_invalid"));
            setStatus(text);
            m_banner->setText(text);
            m_banner->setVisible(true);
        } else {
            setStatus(utils::tr(QStringLiteral("store.window.loaded")).arg(r.catalog.packages.size()));
        }
        rebuildCategories();
        showGrid();
        showPlaceholder();
        rebuildGrid();
        engine::storeRunAsync(this, []() { core::trimStoreCache(kCacheBudgetBytes); }, []() {});
    });
}

QString StoreWindow::categoryName(const QString &id) const
{
    const QString lang = utils::TranslationManager::instance().currentLanguage();
    for (const core::StoreCategory &c : m_result.catalog.header.categories) {
        if (c.id == id) {
            const QString name = c.name.resolve(lang);
            return name.isEmpty() ? id : name;
        }
    }
    return id;
}

void StoreWindow::rebuildCategories()
{
    QSignalBlocker blocker(m_categoryList);
    QSignalBlocker comboBlocker(m_categoryCombo);
    m_categoryList->clear();
    m_categoryCombo->clear();
    auto *all = new QListWidgetItem(utils::tr(QStringLiteral("store.window.all")), m_categoryList);
    all->setData(Qt::UserRole, QString());
    QStringList ids;
    for (const core::StoreCategory &c : m_result.catalog.header.categories) ids << c.id;
    for (const core::StoreIndexEntry &e : m_result.catalog.packages) {
        if (!e.category.isEmpty() && !ids.contains(e.category)) ids << e.category;
    }
    for (const QString &id : ids) {
        auto *item = new QListWidgetItem(categoryName(id), m_categoryList);
        item->setData(Qt::UserRole, id);
    }
    for (int i = 0; i < m_categoryList->count(); ++i) {
        m_categoryCombo->addItem(m_categoryList->item(i)->text());
    }
    m_category.clear();
    m_categoryList->setCurrentRow(0);
    m_categoryCombo->setCurrentIndex(0);
}

bool StoreWindow::matchesFilter(const core::StoreIndexEntry &e) const
{
    if (!m_category.isEmpty() && e.category != m_category) {
        return false;
    }
    const QString kind = m_kindFilter->currentData().toString();
    if (!kind.isEmpty() && e.kind != kind) {
        return false;
    }
    const QString status = m_statusFilter->currentData().toString();
    if (status != QLatin1String("any")) {
        const InstalledInfo info = installedInfo(e);
        if ((status == QLatin1String("new") && info.installed) || (status == QLatin1String("installed") && !info.installed)
            || (status == QLatin1String("updates") && !info.update)) {
            return false;
        }
    }
    const QString needle = m_search->text().trimmed().toLower();
    if (needle.isEmpty()) {
        return true;
    }
    const QString lang = utils::TranslationManager::instance().currentLanguage();
    return e.id.toLower().contains(needle) || e.name.toLower().contains(needle)
        || e.tags.join(QLatin1Char(' ')).toLower().contains(needle) || e.summary.resolve(lang).toLower().contains(needle);
}

StoreWindow::InstalledInfo StoreWindow::installedInfo(const core::StoreIndexEntry &entry) const
{
    InstalledInfo info;
    if (m_providerIndex < 0) {
        return info;
    }
    const QString providerId = m_providers.at(m_providerIndex).id;
    for (const core::StoreInstalledRecord &r : m_records) {
        if (r.provider != providerId || r.package != entry.id) continue;
        if (info.installed && core::compareVersions(r.version, info.version) <= 0) continue;
        info.installed = true;
        info.version = r.version;
        info.folderId = r.folderId;
    }
    if (info.installed) {
        const core::StoreVersionEntry *best = core::latestCompatible(entry);
        if (best && core::compareVersions(best->version, info.version) > 0) {
            info.update = true;
            info.updateVersion = best->version;
        }
    }
    return info;
}

void StoreWindow::rebuildGrid()
{
    clearLayout(m_gridLayout);
    QVector<const core::StoreIndexEntry *> shown;
    for (const core::StoreIndexEntry &e : m_result.catalog.packages) {
        if (matchesFilter(e)) shown << &e;
    }
    std::sort(shown.begin(), shown.end(), [](const core::StoreIndexEntry *a, const core::StoreIndexEntry *b) {
        return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });
    for (const core::StoreIndexEntry *e : shown) {
        m_gridLayout->addWidget(buildCard(*e));
    }
    if (shown.isEmpty() && m_result.ok) {
        auto *empty = new QLabel(utils::tr(QStringLiteral("store.window.empty")), m_gridHost);
        empty->setProperty("kaiRole", QStringLiteral("caption"));
        m_gridLayout->addWidget(empty);
    }
    m_gridLayout->addStretch(1);
}

QWidget *StoreWindow::buildCard(const core::StoreIndexEntry &entry)
{
    // Linha de lista (estilo marketplace de plugins): ícone, nome, resumo e estado; clicar abre o detalhe ao lado.
    const QString id = entry.id;
    auto *row = new ClickableFrame([this, id]() { showDetail(id); }, m_gridHost);
    row->setObjectName(QStringLiteral("storeCard"));
    row->setProperty("pkgId", id);
    row->setProperty("selected", id == m_detailId);
    row->setStyleSheet(QStringLiteral(
        "QFrame#storeCard { background: transparent; border: none; border-radius: %1px; }"
        "QFrame#storeCard:hover { background: %2; }"
        "QFrame#storeCard[selected=\"true\"] { background: %3; }"
        "QFrame#storeCard QLabel { background: transparent; }")
        .arg(tk::radiusSm()).arg(tk::hoverBg(), tint(tk::accent(), 0.18)));
    const core::StoreVersionEntry *compatible = core::latestCompatible(entry);
    if (!compatible) {
        const core::StoreVersionEntry *newest = core::findVersion(entry, entry.latest);
        row->setToolTip(newest ? core::storeCompatText(core::checkCompatibility(*newest).reason) : QString());
    }

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
    layout->setSpacing(tk::space(3));
    QLabel *icon = makeTile(row, kTile);
    layout->addWidget(icon, 0, Qt::AlignTop);
    fetchIcon(entry, icon);

    auto *text = new QVBoxLayout();
    text->setSpacing(2);
    auto *name = new QLabel(entry.name, row);
    QFont f = name->font();
    f.setWeight(QFont::DemiBold);
    name->setFont(f);
    text->addWidget(name);
    const QString lang = utils::TranslationManager::instance().currentLanguage();
    auto *summary = new QLabel(row);
    summary->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(compatible ? tk::mutedFg() : tint(tk::mutedFg(), 0.6)));
    summary->setText(QFontMetrics(summary->font()).elidedText(entry.summary.resolve(lang), Qt::ElideRight, 230));
    text->addWidget(summary);
    auto *chips = new QHBoxLayout();
    chips->setSpacing(tk::space(1));
    chips->addWidget(makeChip(row, entry.latest, tk::mutedFg()));
    chips->addWidget(makeChip(row, core::storeKindLabel(entry.kind), tk::mutedFg()));
    const InstalledInfo info = installedInfo(entry);
    if (info.update) {
        chips->addWidget(makeChip(row, utils::tr(QStringLiteral("store.card.update")), tk::warningFg()));
    } else if (info.installed) {
        chips->addWidget(makeChip(row, utils::tr(QStringLiteral("store.card.installed")), tk::successFg()));
    } else if (!compatible) {
        chips->addWidget(makeChip(row, utils::tr(QStringLiteral("store.card.incompatible")), tk::errorFg()));
    }
    chips->addStretch(1);
    text->addLayout(chips);
    layout->addLayout(text, 1);
    return row;
}

void StoreWindow::applySelection()
{
    for (QFrame *row : m_gridHost->findChildren<QFrame *>(QStringLiteral("storeCard"))) {
        row->setProperty("selected", row->property("pkgId").toString() == m_detailId);
        row->style()->unpolish(row);
        row->style()->polish(row);
    }
}

void StoreWindow::showPlaceholder()
{
    m_detailId.clear();
    clearLayout(m_detailLayout);
    auto *label = new QLabel(utils::tr(QStringLiteral("store.detail.placeholder")), m_detailHost);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(tk::mutedFg()));
    m_detailLayout->addStretch(1);
    m_detailLayout->addWidget(label);
    m_detailLayout->addStretch(1);
    applySelection();
    applyResponsive();
}

void StoreWindow::fetchIcon(const core::StoreIndexEntry &entry, QLabel *target)
{
    if (entry.icon.isEmpty() || !isRasterPath(entry.icon) || !m_client) {
        return;
    }
    const QString key = m_providers.at(m_providerIndex).id + QLatin1Char('|') + entry.icon;
    if (m_icons.contains(key)) {
        target->setPixmap(m_icons.value(key));
        return;
    }
    QPointer<QLabel> label(target);
    QPointer<engine::StoreClient> client(m_client);
    m_client->fetch(entry.icon, engine::store_limits::kMediaBytes, [this, label, client, key](const engine::StoreFetchResult &r) {
        if (!r.ok || !label || !client) {
            return;
        }
        QPixmap pm;
        if (!pm.loadFromData(r.data) || pm.isNull()) {
            return;
        }
        pm = pm.scaled(kTile - 12, kTile - 12, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_icons.insert(key, pm);
        label->setPixmap(pm);
    });
}

void StoreWindow::showGrid()
{
    m_pages->setCurrentIndex(0);
    if (m_tabs->currentIndex() != 0) {
        QSignalBlocker blocker(m_tabs);
        m_tabs->setCurrentIndex(0);
    }
    applyResponsive();
}

void StoreWindow::showManage()
{
    rebuildManage();
    m_pages->setCurrentIndex(1);
    applyResponsive();
}

void StoreWindow::recordsChanged()
{
    m_records = core::StoreLocalState(m_host.configDir).loadInstalled();
    m_tabs->setTabText(1, utils::tr(QStringLiteral("store.window.tab_installed")) + QStringLiteral(" (%1)").arg(m_records.size()));
    rebuildGrid();
    if (m_pages->currentIndex() == 1) {
        rebuildManage();
    }
}

void StoreWindow::removeRecords(const QVector<core::StoreInstalledRecord> &records)
{
    const core::StoreLocalState local(m_host.configDir);
    const core::CommandsData data = m_host.commands();
    for (const core::StoreInstalledRecord &r : records) {
        bool folderExists = false;
        for (const core::Folder &f : data.folders) {
            if (!r.folderId.isEmpty() && f.id == r.folderId) folderExists = true;
        }
        if (folderExists) {
            QString message;
            if (!m_host.uninstall(r.folderId, message)) {
                QMessageBox::warning(this, utils::tr(QStringLiteral("store.window.title")), message);
                continue;
            }
        } else {
            // Ambiente/perfil ou pasta que já não existe: só sai da lista (o que foi importado fica).
            local.removeRecord(r.provider, r.package, r.folderId);
        }
    }
    recordsChanged();
}

void StoreWindow::rebuildManage()
{
    clearLayout(m_manageLayout);
    m_manageChecks.clear();
    const QString needle = m_search->text().trimmed().toLower();
    const core::CommandsData data = m_host.commands();
    const auto providerName = [this](const QString &id) {
        for (const core::StoreProviderConfig &p : m_providers) {
            if (p.id == id) return p.name;
        }
        return id;
    };

    QVector<core::StoreInstalledRecord> shown;
    for (const core::StoreInstalledRecord &r : m_records) {
        if (needle.isEmpty() || r.package.toLower().contains(needle) || providerName(r.provider).toLower().contains(needle)) {
            shown << r;
        }
    }
    std::sort(shown.begin(), shown.end(), [](const core::StoreInstalledRecord &a, const core::StoreInstalledRecord &b) {
        return a.package < b.package;
    });

    auto *bar = new QHBoxLayout();
    m_uninstallSelected = new QPushButton(utils::tr(QStringLiteral("store.manage.uninstall_selected")), m_manageHost);
    m_uninstallSelected->setEnabled(false);
    connect(m_uninstallSelected, &QPushButton::clicked, this, [this]() {
        QVector<core::StoreInstalledRecord> chosen;
        for (const auto &pair : m_manageChecks) {
            if (pair.first->isChecked()) chosen << pair.second;
        }
        if (chosen.isEmpty()) return;
        if (QMessageBox::question(this, utils::tr(QStringLiteral("store.detail.uninstall")),
                                  utils::tr(QStringLiteral("store.manage.confirm_many")).arg(chosen.size())) == QMessageBox::Yes) {
            removeRecords(chosen);
        }
    });
    m_uninstallSelected->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::errorFg()), 16));
    // Selecionar tudo: marca/desmarca todas as linhas visíveis; acompanha as caixas individuais.
    m_selectAll = new QCheckBox(utils::tr(QStringLiteral("store.manage.select_all")), m_manageHost);
    connect(m_selectAll, &QCheckBox::clicked, this, [this](bool on) {
        for (const auto &pair : m_manageChecks) pair.first->setChecked(on);
    });
    bar->addWidget(m_selectAll);
    bar->addStretch(1);
    bar->addWidget(m_uninstallSelected);
    m_manageLayout->addLayout(bar);

    if (shown.isEmpty()) {
        auto *empty = new QLabel(utils::tr(QStringLiteral("store.manage.empty")), m_manageHost);
        empty->setProperty("kaiRole", QStringLiteral("caption"));
        m_manageLayout->addWidget(empty);
    }
    for (const core::StoreInstalledRecord &r : shown) {
        bool folderExists = false;
        QString folderName;
        for (const core::Folder &f : data.folders) {
            if (!r.folderId.isEmpty() && f.id == r.folderId) {
                folderExists = true;
                folderName = f.name;
            }
        }
        auto *row = new ClickableFrame({}, m_manageHost);
        row->setObjectName(QStringLiteral("storeRow"));
        row->setStyleSheet(QStringLiteral("QFrame#storeRow { background: %1; border: none; border-radius: %2px; }"
                                          "QFrame#storeRow QLabel { background: transparent; }")
                               .arg(tk::surface()).arg(tk::radiusMd()));
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(tk::space(3), tk::space(2), tk::space(3), tk::space(2));
        layout->setSpacing(tk::space(3));

        auto *check = new QCheckBox(row);
        connect(check, &QCheckBox::toggled, this, [this]() {
            bool any = false;
            bool all = !m_manageChecks.isEmpty();
            for (const auto &pair : m_manageChecks) {
                any = any || pair.first->isChecked();
                all = all && pair.first->isChecked();
            }
            m_uninstallSelected->setEnabled(any);
            QSignalBlocker blocker(m_selectAll);
            m_selectAll->setChecked(all);
        });
        // A linha inteira marca/desmarca (os botões tratam o próprio clique).
        row->setOnClick([check]() { check->toggle(); });
        m_manageChecks.append({check, r});
        layout->addWidget(check);
        layout->addWidget(makeTile(row, 36));

        auto *text = new QVBoxLayout();
        text->setSpacing(0);
        auto *name = new QLabel(r.package, row);
        QFont f = name->font();
        f.setWeight(QFont::DemiBold);
        name->setFont(f);
        text->addWidget(name);
        const QString where = folderExists ? utils::tr(QStringLiteral("store.manage.in_folder")).arg(folderName)
                                           : (r.folderId.isEmpty() ? core::storeKindLabel(r.kind) : utils::tr(QStringLiteral("store.manage.folder_gone")));
        auto *detail = new QLabel(QStringLiteral("%1 · %2 · %3").arg(r.version, providerName(r.provider), where), row);
        detail->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }").arg(tk::mutedFg()));
        text->addWidget(detail);
        layout->addLayout(text, 1);

        // Há versão nova? Só dá para saber do catálogo carregado (o do provedor atual).
        const core::StoreIndexEntry *entry = nullptr;
        if (m_providerIndex >= 0 && r.provider == m_providers.at(m_providerIndex).id) {
            for (const core::StoreIndexEntry &e : m_result.catalog.packages) {
                if (e.id == r.package) entry = &e;
            }
        }
        if (entry) {
            const core::StoreVersionEntry *best = core::latestCompatible(*entry);
            if (best && core::compareVersions(best->version, r.version) > 0) {
                layout->addWidget(makeChip(row, utils::tr(QStringLiteral("store.card.update")), tk::warningFg()));
            }
            const QString id = entry->id;
            auto *open = new QPushButton(utils::tr(QStringLiteral("store.manage.details")), row);
            open->setFixedHeight(36);
            open->setCursor(Qt::PointingHandCursor);
            open->setStyleSheet(QStringLiteral("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: %3px; padding: 0 14px; }"
                                               "QPushButton:hover { background: %4; }")
                                    .arg(tk::fg(), tk::borderColor()).arg(tk::radiusMd()).arg(tk::hoverBg()));
            connect(open, &QPushButton::clicked, this, [this, id]() {
                showGrid();
                showDetail(id);
            });
            layout->addWidget(open, 0, Qt::AlignVCenter);
        }
        // Lixeira (só o ícone); o texto fica no tooltip.
        auto *remove = new QPushButton(row);
        remove->setIcon(LucideIcons::icon(QStringLiteral("trash-2"), QColor(tk::errorFg()), 18));
        remove->setIconSize(QSize(18, 18));
        remove->setFixedSize(36, 36);
        remove->setCursor(Qt::PointingHandCursor);
        remove->setToolTip(folderExists ? utils::tr(QStringLiteral("store.detail.uninstall")) : utils::tr(QStringLiteral("store.manage.remove_entry")));
        remove->setStyleSheet(QStringLiteral("QPushButton { border: 1px solid %1; border-radius: %2px; background: transparent; }"
                                             "QPushButton:hover { background: %3; }")
                                  .arg(tint(tk::errorFg(), 0.45)).arg(tk::radiusMd()).arg(tint(tk::errorFg(), 0.16)));
        connect(remove, &QPushButton::clicked, this, [this, r, folderExists]() {
            const QString question = folderExists ? utils::tr(QStringLiteral("store.manage.confirm_one")).arg(r.package)
                                                  : utils::tr(QStringLiteral("store.manage.confirm_entry")).arg(r.package);
            if (QMessageBox::question(this, utils::tr(QStringLiteral("store.detail.uninstall")), question) == QMessageBox::Yes) {
                removeRecords({r});
            }
        });
        layout->addWidget(remove, 0, Qt::AlignVCenter);
        m_manageLayout->addWidget(row);
    }
    m_manageLayout->addStretch(1);
}

void StoreWindow::showDetail(const QString &packageId)
{
    const core::StoreIndexEntry *found = nullptr;
    for (const core::StoreIndexEntry &e : m_result.catalog.packages) {
        if (e.id == packageId) found = &e;
    }
    if (!found) {
        return;
    }
    const core::StoreIndexEntry entry = *found;
    m_detailId = packageId;
    clearLayout(m_detailLayout);
    const core::StoreProviderConfig &provider = m_providers.at(m_providerIndex);
    const QString lang = utils::TranslationManager::instance().currentLanguage();


    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(tk::space(3));
    headRow->addWidget(makeTile(m_detailHost, 56));
    auto *title = new QLabel(entry.name, m_detailHost);
    title->setProperty("kaiRole", QStringLiteral("title"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 5);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    headRow->addWidget(title, 1);
    m_detailLayout->addLayout(headRow);

    auto *chips = new QHBoxLayout();
    chips->addWidget(makeChip(m_detailHost, core::storeKindLabel(entry.kind), tk::mutedFg()));
    chips->addWidget(makeChip(m_detailHost, core::storeTrustLabel(core::storeTrustName(provider.trust)), trustColor(provider.trust)));
    chips->addWidget(makeChip(m_detailHost, signatureText(m_result.signature),
                              m_result.signature == engine::StoreSignatureState::Invalid ? tk::errorFg() : tk::mutedFg()));
    chips->addStretch(1);
    m_detailLayout->addLayout(chips);

    auto *summary = new QLabel(entry.summary.resolve(lang), m_detailHost);
    summary->setWordWrap(true);
    m_detailLayout->addWidget(summary);

    // Descrição (Markdown sem HTML) e metadados chegam com o manifesto.
    auto *description = new MarkdownView(m_detailHost);
    description->setOpenLinks(false);
    description->setOpenExternalLinks(false);
    description->setMinimumHeight(140);
    description->setFrameShape(QFrame::NoFrame);
    description->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
    m_detailLayout->addWidget(description);
    // Capturas de tela do pacote (media.screenshots), em fileira rolável; só aparece se houver.
    auto *shotsScroll = new QScrollArea(m_detailHost);
    shotsScroll->setFrameShape(QFrame::NoFrame);
    shotsScroll->setWidgetResizable(true);
    shotsScroll->setFixedHeight(190);
    shotsScroll->setVisible(false);
    shotsScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    auto *shotsHost = new QWidget();
    shotsHost->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *shotsLayout = new QHBoxLayout(shotsHost);
    shotsLayout->setContentsMargins(0, 0, 0, 0);
    shotsLayout->setSpacing(tk::space(2));
    shotsScroll->setWidget(shotsHost);
    m_detailLayout->addWidget(shotsScroll);
    auto *meta = new QLabel(m_detailHost);
    meta->setWordWrap(true);
    meta->setProperty("kaiRole", QStringLiteral("caption"));
    m_detailLayout->addWidget(meta);

    // Versões: as incompatíveis ficam cinza, com o motivo.
    auto *versions = new QComboBox(m_detailHost);
    auto *model = new QStandardItemModel(versions);
    versions->setModel(model);
    QVector<core::StoreVersionEntry> sorted = entry.versions;
    std::sort(sorted.begin(), sorted.end(), [](const core::StoreVersionEntry &a, const core::StoreVersionEntry &b) {
        return core::compareVersions(a.version, b.version) > 0;
    });
    int firstCompatible = -1;
    for (int i = 0; i < sorted.size(); ++i) {
        const core::StoreCompatibility c = core::checkCompatibility(sorted[i]);
        auto *item = new QStandardItem(c.ok ? sorted[i].version : QStringLiteral("%1 — %2").arg(sorted[i].version, core::storeCompatText(c.reason)));
        item->setData(sorted[i].version, Qt::UserRole);
        item->setEnabled(c.ok);
        model->appendRow(item);
        if (c.ok && firstCompatible < 0) firstCompatible = i;
    }
    versions->setCurrentIndex(qMax(0, firstCompatible));
    m_detailLayout->addWidget(versions);

    const InstalledInfo info = installedInfo(entry);
    if (info.installed) {
        auto *state = new QLabel(info.update ? utils::tr(QStringLiteral("store.detail.installed_update")).arg(info.version, info.updateVersion)
                                             : utils::tr(QStringLiteral("store.detail.installed")).arg(info.version),
                                 m_detailHost);
        m_detailLayout->addWidget(state);
    }

    auto *buttons = new QHBoxLayout();
    const auto selectedVersion = [versions, entry]() -> const core::StoreVersionEntry * {
        return core::findVersion(entry, versions->currentData(Qt::UserRole).toString());
    };
    const bool anyCompatible = firstCompatible >= 0;
    if (!info.installed || info.update) {
        auto *action = new QPushButton(info.update ? utils::tr(QStringLiteral("store.detail.update")) : utils::tr(QStringLiteral("store.detail.install")), m_detailHost);
        action->setStyleSheet(primaryButtonStyle());
        action->setCursor(Qt::PointingHandCursor);
        action->setEnabled(anyCompatible && !m_busy && m_result.signature != engine::StoreSignatureState::Invalid);
        connect(action, &QPushButton::clicked, this, [this, entry, info, selectedVersion]() {
            const core::StoreVersionEntry *v = selectedVersion();
            if (!v) return;
            QString replace;
            if (info.installed) {
                // Atualizar: se o usuário editou a pasta instalada, só como cópia.
                const core::CommandsData data = m_host.commands();
                for (const core::StoreInstalledRecord &r : m_records) {
                    if (r.folderId != info.folderId || r.package != entry.id) continue;
                    const bool edited = !r.folderId.isEmpty() && core::storeContentHash(r.folderId, data) != r.contentHash;
                    if (edited) {
                        if (QMessageBox::question(this, utils::tr(QStringLiteral("store.detail.update")),
                                                  utils::tr(QStringLiteral("store.detail.edited_copy"))) != QMessageBox::Yes) {
                            return;
                        }
                    } else {
                        replace = info.folderId;
                    }
                }
            }
            beginInstall(entry, *v, replace);
        });
        buttons->addWidget(action);
    }
    if (info.installed) {
        auto *copy = new QPushButton(utils::tr(QStringLiteral("store.detail.install_copy")), m_detailHost);
        copy->setEnabled(anyCompatible && !m_busy);
        connect(copy, &QPushButton::clicked, this, [this, entry, selectedVersion]() {
            if (const core::StoreVersionEntry *v = selectedVersion()) beginInstall(entry, *v, QString());
        });
        buttons->addWidget(copy);
        auto *remove = new QPushButton(utils::tr(QStringLiteral("store.detail.uninstall")), m_detailHost);
        connect(remove, &QPushButton::clicked, this, [this, entry, info]() { confirmUninstall(entry, info.folderId); });
        buttons->addWidget(remove);
    }
    buttons->addStretch(1);
    m_detailLayout->addLayout(buttons);
    m_detailLayout->addStretch(1);
    m_pages->setCurrentIndex(0);
    applySelection();

    // Manifesto da versão mostrada (a compatível mais nova, ou a mais nova).
    const core::StoreVersionEntry *shown = core::latestCompatible(entry);
    if (!shown) shown = core::findVersion(entry, entry.latest);
    if (shown && m_client) {
        QPointer<MarkdownView> descBox(description);
        QPointer<QLabel> metaBox(meta);
        QPointer<QLabel> summaryBox(summary);
        QPointer<QScrollArea> shotsBox(shotsScroll);
        QPointer<QHBoxLayout> shotsLay(shotsLayout);
        QPointer<engine::StoreClient> client(m_client);
        const QString detailId = packageId;
        const QString manifestDir = engine::StoreClient::manifestDir(shown->manifest);
        m_client->fetchManifest(*shown, [this, descBox, metaBox, summaryBox, shotsBox, shotsLay, client, detailId, manifestDir, lang](const engine::StoreManifestResult &r) {
            if (detailId != m_detailId || !descBox || !metaBox || !client) {
                return;
            }
            if (!r.ok && r.error != QLatin1String("invalid_manifest")) {
                descBox->setPlainText(core::storeErrorText(r.error));
                return;
            }
            const core::StoreManifest mf = r.manifest;
            const QString markdown = mf.description.resolve(lang);
            QStringList lines;
            if (!mf.author.isEmpty()) lines << utils::tr(QStringLiteral("store.detail.author")).arg(mf.author);
            if (!mf.license.isEmpty()) lines << utils::tr(QStringLiteral("store.detail.license")).arg(mf.license);
            if (!mf.needs.isEmpty()) lines << utils::tr(QStringLiteral("store.detail.needs")).arg(mf.needs.join(QStringLiteral(", ")));
            if (!mf.platforms.isEmpty()) lines << utils::tr(QStringLiteral("store.detail.platforms")).arg(mf.platforms.join(QStringLiteral(", ")));
            if (!mf.secrets.isEmpty()) lines << utils::tr(QStringLiteral("store.detail.secrets")).arg(mf.secrets.join(QStringLiteral(", ")));
            metaBox->setText(lines.join(QLatin1Char('\n')));

            // Imagens do Markdown (caminhos relativos ao pacote) e capturas de tela: tudo vem do provedor, só raster e pequeno.
            static const QRegularExpression imageRef(QStringLiteral(R"(!\[[^\]]*\]\(\s*([^)\s]+)(?:\s+"[^"]*")?\s*\))"));
            QStringList mdImages;
            for (auto m = imageRef.globalMatch(markdown); m.hasNext();) {
                const QString path = m.next().captured(1);
                if (core::isSafeStorePath(path) && isRasterPath(path) && !mdImages.contains(path)) mdImages << path;
            }
            QStringList wanted = mdImages;
            for (const QString &shot : mf.screenshots) {
                if (core::isSafeStorePath(shot) && isRasterPath(shot) && !wanted.contains(shot)) wanted << shot;
            }
            struct State {
                int pending = 0;
                QMap<QString, QPixmap> images;
            };
            auto state = std::make_shared<State>();
            state->pending = static_cast<int>(wanted.size());
            const auto render = [this, state, descBox, summaryBox, shotsBox, shotsLay, detailId, markdown, mdImages, screenshots = mf.screenshots]() {
                if (detailId != m_detailId || !descBox) {
                    return;
                }
                for (const QString &path : mdImages) {
                    QPixmap pm = state->images.value(path);
                    if (pm.isNull()) continue;
                    if (pm.width() > 560) pm = pm.scaledToWidth(560, Qt::SmoothTransformation);
                    descBox->images.insert(path, pm);
                }
                descBox->document()->setMarkdown(markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
                                                               | QTextDocument::MarkdownNoHTML);
                // A descrição já diz o que o resumo diz: não repete.
                if (summaryBox && !markdown.trimmed().isEmpty()) summaryBox->setVisible(false);
                if (shotsBox && shotsLay) {
                    bool any = false;
                    for (const QString &shot : screenshots) {
                        const QPixmap pm = state->images.value(shot);
                        if (pm.isNull()) continue;
                        auto *label = new QLabel();
                        label->setPixmap(pm.scaledToHeight(170, Qt::SmoothTransformation));
                        label->setStyleSheet(QStringLiteral("QLabel { border: 1px solid %1; border-radius: %2px; background: transparent; }")
                                                 .arg(tk::borderColor()).arg(tk::radiusMd()));
                        shotsLay->addWidget(label);
                        any = true;
                    }
                    if (any) {
                        shotsLay->addStretch(1);
                        shotsBox->setVisible(true);
                    }
                }
            };
            if (wanted.isEmpty()) {
                render();
                return;
            }
            for (const QString &path : wanted) {
                client->fetch(manifestDir.isEmpty() ? path : manifestDir + QLatin1Char('/') + path, engine::store_limits::kMediaBytes,
                              [state, path, render](const engine::StoreFetchResult &fetched) {
                    QPixmap pm;
                    if (fetched.ok && pm.loadFromData(fetched.data) && !pm.isNull()) {
                        state->images.insert(path, pm);
                    }
                    if (--state->pending == 0) render();
                });
            }
        });
    }
    applyResponsive();
}

void StoreWindow::cancelInstall()
{
    if (m_installer && m_busy) {
        m_installer->cancel();
    }
    if (m_client && m_busy) {
        m_client->cancel();
    }
}

void StoreWindow::beginInstall(const core::StoreIndexEntry &entry, const core::StoreVersionEntry &version,
                               const QString &replaceFolderId)
{
    if (m_busy || !m_client) {
        return;
    }
    m_busy = true;
    m_cancelButton->show();
    setStatus(utils::tr(QStringLiteral("store.window.fetching")).arg(entry.name));
    const QString replace = replaceFolderId;
    QPointer<engine::StoreClient> client(m_client);
    m_client->fetchManifest(version, [this, client, version, replace](const engine::StoreManifestResult &r) {
        if (!client) {
            return;
        }
        if (!r.ok) {
            m_busy = false;
            m_cancelButton->hide();
            setStatus(core::storeErrorText(r.error));
            return;
        }
        const bool byReference = r.manifest.clone.has_value();
        const QString root = QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("store")))
                                 .filePath(byReference ? QStringLiteral("clones") : QStringLiteral("staging"));
        m_installer = std::make_unique<engine::StoreInstaller>(m_client);
        connect(m_installer.get(), &engine::StoreInstaller::progress, this, [this](int done, int total) {
            setStatus(utils::tr(QStringLiteral("store.window.downloading")).arg(done).arg(total));
        });
        m_installer->stage(r.manifest, version.manifest, root, [this, replace](const engine::StoreStagedPackage &staged) {
            finishInstall(staged, replace);
        });
    });
}

void StoreWindow::finishInstall(const engine::StoreStagedPackage &staged, const QString &replaceFolderId)
{
    m_busy = false;
    m_cancelButton->hide();
    if (!staged.ok) {
        setStatus(core::storeErrorText(staged.error, staged.errorDetail));
        QStringList errors;
        for (const core::ValidationIssue &i : staged.validation.issues) {
            if (i.severity == core::ValidationSeverity::Error) errors << QStringLiteral("%1: %2").arg(i.path, i.message);
        }
        if (!errors.isEmpty()) {
            QMessageBox::warning(this, utils::tr(QStringLiteral("store.window.title")), errors.join(QLatin1Char('\n')));
        }
        return;
    }
    setStatus(QString());
    const core::StoreProviderConfig &provider = m_providers.at(m_providerIndex);
    const core::CommandsData data = m_host.commands();
    StorePreviewDialog dialog(staged, provider.name, core::storeTrustLabel(core::storeTrustName(provider.trust)),
                              signatureText(m_result.signature), data.folders, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    bool installed = false;
    if (accepted) {
        core::StoreInstallRequest request;
        dialog.fillRequest(request);
        request.providerId = provider.id;
        request.replaceFolderId = replaceFolderId;
        QString message;
        installed = m_host.install(request, message);
        if (installed) {
            setStatus(message);
        } else {
            QMessageBox::warning(this, utils::tr(QStringLiteral("store.window.title")), message);
        }
    }
    // O clone por referência é o projeto e fica; todo o resto era só staging.
    if (!(installed && staged.manifest.clone)) {
        engine::StoreInstaller::discard(staged);
    }
    recordsChanged();
    if (!m_detailId.isEmpty()) showDetail(m_detailId);
}

void StoreWindow::confirmUninstall(const core::StoreIndexEntry &entry, const QString &folderId)
{
    const core::CommandsData data = m_host.commands();
    QString folderName;
    int commands = 0;
    QStringList folderIds{folderId};
    for (int i = 0; i < folderIds.size(); ++i) {
        for (const core::Folder &f : data.folders) {
            if (f.parentId.has_value() && f.parentId.value() == folderIds[i]) folderIds << f.id;
        }
    }
    for (const core::Folder &f : data.folders) {
        if (f.id == folderId) folderName = f.name;
    }
    for (const core::Command &c : data.commands) {
        if (folderIds.contains(c.folderId)) ++commands;
    }
    if (QMessageBox::question(this, utils::tr(QStringLiteral("store.detail.uninstall")),
                              utils::tr(QStringLiteral("store.detail.uninstall_confirm")).arg(entry.name, folderName).arg(commands))
        != QMessageBox::Yes) {
        return;
    }
    QString message;
    if (!m_host.uninstall(folderId, message)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("store.window.title")), message);
    } else {
        setStatus(message);
    }
    recordsChanged();
    showDetail(entry.id);
}

} // namespace kai::ui
