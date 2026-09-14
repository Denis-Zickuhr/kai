#include "ui/shared/parameter-field-factory.h"

#include "ui/shared/data-table-widget.h"
#include "ui/shared/date-picker-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/inline-code-field.h"

#include "ui/shared/table-utils.h"

#include "utils/path-format.h"

#include <QCheckBox>

#include <QFileInfo>

#include <QHeaderView>

#include <QJsonParseError>

#include <QLineEdit>

#include <QMenu>

#include <QPainter>
#include <QPlainTextEdit>
#include <QSpinBox>

#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace fields {

namespace {

namespace tk = kai::utils::tokens;

// Descrição de uma opção (linha secundária), lida pelo delegate abaixo.
constexpr int kDescriptionRole = Qt::UserRole + 1;

// QListWidget não alterna o item com foco ao apertar Espaço. O filtro é filho da própria lista.
class SpaceToggleFilter : public QObject {
public:
    explicit SpaceToggleFilter(QListWidget *list) : QObject(list), m_list(list) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_list && event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Space) {
                if (QListWidgetItem *item = m_list->currentItem()) {
                    item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
                }
                return true; // consome: Espaço não deve rolar/ativar de novo
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QListWidget *m_list;
};

// Rótulo com a descrição, mais discreta, embaixo; sem descrição o desenho é o padrão.
class ChoiceItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QString description = index.data(kDescriptionRole).toString();
        if (description.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        const int descHeight = descriptionHeight(opt.font);
        QStyleOptionViewItem labelOpt = opt;
        labelOpt.rect.setBottom(opt.rect.bottom() - descHeight);
        style->drawControl(QStyle::CE_ItemViewItem, &labelOpt, painter, opt.widget);

        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &labelOpt, opt.widget);
        painter->save();
        QFont small = opt.font;
        small.setPointSize(qMax(7, tk::fontSizeSmallPt()));
        painter->setFont(small);
        painter->setPen(QColor(tk::mutedFg()));
        const QRect descRect(textRect.left(), opt.rect.bottom() - descHeight, textRect.width(), descHeight);
        painter->drawText(descRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(small).elidedText(description, Qt::ElideRight, descRect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        if (!index.data(kDescriptionRole).toString().isEmpty()) {
            size.setHeight(size.height() + descriptionHeight(option.font));
        }
        return size;
    }

private:
    static int descriptionHeight(const QFont &base)
    {
        QFont small = base;
        small.setPointSize(qMax(7, tk::fontSizeSmallPt()));
        return QFontMetrics(small).height() + 2;
    }
};

void styleChoiceList(QListWidget *list)
{
    list->setFrameShape(QFrame::NoFrame);
    // O viewport fica transparente (senão pinta um quadrado escuro atrás do frame arredondado) e só o QListWidget
    // desenha o fundo. A regra é qualificada por objectName: uma regra crua num viewport quebra a cascata de QSS para
    // os descendentes (ver o scroll area do ParameterFormDialog).
    list->viewport()->setObjectName(QStringLiteral("paramMultiSelectViewport"));
    list->viewport()->setStyleSheet(QStringLiteral(
        "QWidget#paramMultiSelectViewport { background: transparent; }"));
    list->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; padding: %4px; }"
        "QListWidget::item { background: transparent; }")
        .arg(tk::bg(), tk::borderColor())
        .arg(tk::radiusMd())
        .arg(tk::space(1)));
    // Entre 5 e 8 linhas, para uma lista enorme não engolir o diálogo.
    list->setMinimumHeight(5 * standardRowHeight());
    list->setMaximumHeight(8 * standardRowHeight());
}

QLineEdit *makeFilterEdit(QWidget *parent)
{
    auto *search = new QLineEdit(parent);
    search->setPlaceholderText(utils::tr(QStringLiteral("params.multi_search.placeholder")));
    search->setClearButtonEnabled(true);
    search->setStyleSheet(QStringLiteral(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: %4px; padding: %5px %6px; }")
        .arg(tk::bg(), tk::fg(), tk::borderColor())
        .arg(tk::radiusMd())
        .arg(tk::space(2)).arg(tk::space(3)));
    search->addAction(LucideIcons::icon(QStringLiteral("search"), QColor(tk::mutedFg()), 16),
                      QLineEdit::LeadingPosition);
    return search;
}

QWidget *makeTransparentColumn(QWidget *parent, const QString &objectName, QVBoxLayout **layoutOut, int spacing)
{
    auto *container = new QWidget(parent);
    container->setObjectName(objectName);
    container->setStyleSheet(QStringLiteral("QWidget#%1 { background: transparent; }").arg(objectName));
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(spacing);
    *layoutOut = vbox;
    return container;
}

// Escreve em `target` o path convertido ao formato pedido: o diálogo nativo devolve o formato do SO do Kai, e sob WSLg
// isso costuma ser um path Windows, inútil num comando bash.
void browseInto(QWidget *dialogParent, QLineEdit *target, const QString &initialDir, const QString &pathFormat,
                bool pickFolder, const QString &filter)
{
    // Onde abrir, por precedência: o arquivo já escolhido no campo, o `initial_dir` de quem pediu (só o KIP), o último
    // diretório usado em qualquer seletor do Kai.
    const QString current = target ? target->text().trimmed() : QString();
    const QString hint = QFileInfo::exists(current) ? current : initialDir.trimmed();

    const QString path = pickFolder
        ? pickDirectory(dialogParent, utils::tr(QStringLiteral("params.select_folder")), hint)
        : pickOpenFile(dialogParent, utils::tr(QStringLiteral("params.select_file")), filter, hint);
    if (!path.isEmpty()) {
        target->setText(utils::convertFilePathFormat(path, pathFormat));
    }
}

} // namespace

QString isoDateText(const QString &mode, const QDateTime &value)
{
    if (mode == QLatin1String("time")) return value.time().toString(QStringLiteral("HH:mm:ss"));
    if (mode == QLatin1String("datetime")) return value.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    return value.date().toString(QStringLiteral("yyyy-MM-dd"));
}

QWidget *wrapWithLabel(QWidget *parent, const QString &labelText, QWidget *field, bool required)
{
    auto *container = new QWidget(parent);
    container->setObjectName(QStringLiteral("paramFieldWrap"));
    // Qualificado por objectName: uma regra crua "background: transparent;" cascateia para os filhos e deixa o
    // QLineEdit sem fundo.
    container->setStyleSheet(QStringLiteral(
        "QWidget#paramFieldWrap { background: transparent; }"));
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(1));
    // Asterisco de obrigatório; rich text só quando precisa.
    auto *labelWidget = new QLabel(container);
    if (required) {
        labelWidget->setTextFormat(Qt::RichText);
        labelWidget->setText(QStringLiteral("%1 <span style=\"color:%2;\">*</span>")
            .arg(labelText.toHtmlEscaped(), tk::errorFg()));
    } else {
        labelWidget->setText(labelText);
    }
    QFont labelFont = labelWidget->font();
    labelFont.setBold(true);
    labelWidget->setFont(labelFont);
    labelWidget->setStyleSheet(QStringLiteral("color: %1;").arg(tk::fg()));
    layout->addWidget(labelWidget);
    field->setParent(container);
    layout->addWidget(field);
    return container;
}

ChoiceOption parseParamOption(const QString &option)
{
    const int sep = option.indexOf(QLatin1Char(':'));
    return {(sep > 0) ? option.left(sep) : option, (sep > 0) ? option.mid(sep + 1) : option, QString()};
}

QLineEdit *makeTextField(QWidget *parent, const QString &initial, const QString &placeholder)
{
    auto *field = new QLineEdit(initial, parent);
    field->setPlaceholderText(placeholder);
    return field;
}

QLineEdit *makeSecretField(QWidget *parent, const QString &initial, const QString &placeholder)
{
    auto *field = new QLineEdit(initial, parent);
    field->setPlaceholderText(placeholder);
    field->setEchoMode(QLineEdit::Password);
    auto *toggle = field->addAction(LucideIcons::icon(QStringLiteral("eye"), QColor(tk::mutedFg()), 16),
                                    QLineEdit::TrailingPosition);
    toggle->setCheckable(true);
    toggle->setToolTip(utils::tr(QStringLiteral("kip.field.secret.show")));
    QObject::connect(toggle, &QAction::toggled, field, [field, toggle](bool shown) {
        field->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
        toggle->setIcon(LucideIcons::icon(shown ? QStringLiteral("eye-off") : QStringLiteral("eye"),
                                          QColor(tk::mutedFg()), 16));
        toggle->setToolTip(utils::tr(shown ? QStringLiteral("kip.field.secret.hide")
                                           : QStringLiteral("kip.field.secret.show")));
    });
    return field;
}

InlineCodeField *makeTextareaField(QWidget *parent, const QString &editorTitle, const QString &placeholder,
                                   const QString &initial)
{
    // Quebra de linha por largura, em vez do NoWrap padrão do InlineCodeField (pensado para comando/JSON): é texto livre.
    auto *field = new InlineCodeField(parent);
    field->setEditorTitle(editorTitle);
    field->setLineRange(3, 9);
    field->setPlainField(true); // fundo de campo normal (não editor de código)
    field->editor()->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    field->setPlaceholderText(placeholder);
    if (!initial.isEmpty()) {
        field->setPlainText(initial);
    }
    return field;
}

JsonField makeJsonField(QWidget *parent, const QString &label, const QString &initial)
{
    auto *field = new InlineCodeField(parent);
    field->setEditorTitle(label);
    field->setJsonSyntax(true);
    field->setLineRange(3, 6);
    if (!initial.trimmed().isEmpty()) {
        field->setPlainText(initial);
    }

    auto *labelRow = new QHBoxLayout();
    labelRow->setContentsMargins(0, 0, 0, 0);
    labelRow->setSpacing(tk::space(1));
    auto *jsonLabel = new QLabel(label, parent);
    QFont jsonLabelFont = jsonLabel->font();
    jsonLabelFont.setBold(true);
    jsonLabel->setFont(jsonLabelFont);
    jsonLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk::fg()));
    labelRow->addWidget(jsonLabel);
    labelRow->addStretch(1);
    auto *formatBtn = makeHeaderIconButton(parent, QStringLiteral("braces"),
        utils::tr(QStringLiteral("json.dialog.format_title")));
    auto *minifyBtn = makeHeaderIconButton(parent, QStringLiteral("shrink"),
        utils::tr(QStringLiteral("json.dialog.minify_title")));
    const auto reserialize = [field, parent](QJsonDocument::JsonFormat format, const QString &titleKey) {
        const QString raw = field->toPlainText();
        if (raw.trimmed().isEmpty()) {
            return;
        }
        QJsonParseError e;
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &e);
        if (e.error != QJsonParseError::NoError) {
            QMessageBox::warning(parent, utils::tr(titleKey),
                utils::tr(QStringLiteral("json.error.invalid_format")).arg(e.errorString()));
            return;
        }
        field->setPlainText(QString::fromUtf8(doc.toJson(format)));
    };
    QObject::connect(formatBtn, &QToolButton::clicked, parent, [reserialize]() {
        reserialize(QJsonDocument::Indented, QStringLiteral("json.dialog.format_title"));
    });
    QObject::connect(minifyBtn, &QToolButton::clicked, parent, [reserialize]() {
        reserialize(QJsonDocument::Compact, QStringLiteral("json.dialog.minify_title"));
    });
    labelRow->addWidget(formatBtn);
    labelRow->addWidget(minifyBtn);

    QVBoxLayout *vbox = nullptr;
    QWidget *container = makeTransparentColumn(parent, QStringLiteral("paramJsonWrap"), &vbox, tk::space(1));
    vbox->addLayout(labelRow);
    vbox->addWidget(field);
    return {container, field};
}

namespace {
QComboBox *buildCombo(QWidget *parent, QVector<ChoiceOption> opts, const QStringList &history,
                      const QString &initialValue, const QString *emptyLabel)
{
    auto *field = new QComboBox(parent);

    // Os valores usados mais recentemente vêm primeiro; o resto mantém a ordem de definição (stable_sort).
    if (!history.isEmpty()) {
        std::stable_sort(opts.begin(), opts.end(), [&history](const ChoiceOption &a, const ChoiceOption &b) {
            const int ia = history.indexOf(a.value);
            const int ib = history.indexOf(b.value);
            const int ra = (ia < 0) ? std::numeric_limits<int>::max() : ia;
            const int rb = (ib < 0) ? std::numeric_limits<int>::max() : ib;
            return ra < rb;
        });
    }

    if (emptyLabel) {
        field->addItem(*emptyLabel, QString());
    }
    for (const ChoiceOption &o : opts) {
        field->addItem(o.label, o.value);
        if (!o.description.isEmpty()) {
            field->setItemData(field->count() - 1, o.description, Qt::ToolTipRole);
        }
    }

    // Completer por "contém", case-insensitive, sobre o modelo do combo (respeita a ordem por histórico). Como o combo
    // é editável, quem lê o valor precisa resolver o texto para uma opção válida.
    field->setEditable(true);
    field->setInsertPolicy(QComboBox::NoInsert);
    if (auto *completer = field->completer()) {
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
    }

    const int defaultIndex = field->findData(initialValue);
    field->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    return field;
}
} // namespace

QComboBox *makeSelectCombo(QWidget *parent, const QVector<ChoiceOption> &options, const QStringList &usageHistory,
                           const QString &initialValue, const QString &placeholder)
{
    return buildCombo(parent, options, usageHistory, initialValue, placeholder.isEmpty() ? nullptr : &placeholder);
}

QComboBox *makeOptionalSelectCombo(QWidget *parent, const QVector<ChoiceOption> &options, const QString &emptyLabel,
                                   const QString &initialValue)
{
    return buildCombo(parent, options, {}, initialValue, &emptyLabel);
}

ListPager::ListPager(int rowCount, int pageSize, Matcher matches, Hider hide, QObject *parent)
    : QObject(parent)
    , m_rowCount(rowCount)
    , m_pageSize(qMax(0, pageSize))
    , m_matches(std::move(matches))
    , m_hide(std::move(hide))
{
    apply();
}

void ListPager::setFilter(const QString &text)
{
    const QString needle = text.trimmed().toLower();
    if (needle == m_needle) {
        return;
    }
    m_needle = needle;
    m_page = 0; // outro filtro, outra lista: volta ao começo
    apply();
}

void ListPager::setPage(int page)
{
    const int clamped = qBound(0, page, m_pageCount - 1);
    if (clamped == m_page) {
        return;
    }
    m_page = clamped;
    apply();
}

void ListPager::apply()
{
    QVector<int> matching;
    matching.reserve(m_rowCount);
    for (int row = 0; row < m_rowCount; ++row) {
        if (m_needle.isEmpty() || m_matches(row, m_needle)) {
            matching.append(row);
        }
    }
    m_matchCount = matching.size();
    m_pageCount = m_pageSize > 0 ? qMax(1, (m_matchCount + m_pageSize - 1) / m_pageSize) : 1;
    m_page = qBound(0, m_page, m_pageCount - 1);
    const int first = m_pageSize > 0 ? m_page * m_pageSize : 0;
    const int last = m_pageSize > 0 ? first + m_pageSize : m_matchCount; // exclusivo

    QVector<bool> visible(m_rowCount, false);
    for (int i = first; i < last && i < m_matchCount; ++i) {
        visible[matching[i]] = true;
    }
    for (int row = 0; row < m_rowCount; ++row) {
        m_hide(row, !visible[row]);
    }
    emit changed();
}

QWidget *makePagerBar(QWidget *parent, ListPager *pager)
{
    auto *bar = new QWidget(parent);
    bar->setObjectName(QStringLiteral("kipPagerBar"));
    bar->setAttribute(Qt::WA_StyledBackground, true);
    bar->setStyleSheet(QStringLiteral("QWidget#kipPagerBar { background: transparent; border: none; }"));
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(tk::space(2));

    const QString buttonQss = QStringLiteral(
        "QToolButton#%1 { background: transparent; border: 1px solid %2; border-radius: %3px; padding: %4px; }"
        "QToolButton#%1:hover:enabled { border-color: %5; }"
        "QToolButton#%1:disabled { border-color: transparent; }")
        .arg(QStringLiteral("kipPagerButton"), tk::borderColor())
        .arg(tk::radiusMd())
        .arg(tk::space(1))
        .arg(tk::accent());
    auto makeButton = [&](const QString &name, const QString &icon, const QString &tip) {
        auto *b = new QToolButton(bar);
        b->setObjectName(QStringLiteral("kipPagerButton"));
        b->setProperty("kipPagerRole", name);
        b->setIcon(LucideIcons::icon(icon, QColor(tk::fg()), 16));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoRaise(true);
        b->setStyleSheet(buttonQss);
        return b;
    };
    auto *prev = makeButton(QStringLiteral("prev"), QStringLiteral("chevron-left"), utils::tr(QStringLiteral("kip.pager.prev")));
    auto *next = makeButton(QStringLiteral("next"), QStringLiteral("chevron-right"), utils::tr(QStringLiteral("kip.pager.next")));
    auto *label = new QLabel(bar);
    label->setObjectName(QStringLiteral("kipPagerLabel"));
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("QLabel#kipPagerLabel { color: %1; }").arg(tk::mutedFg()));
    row->addStretch(1);
    row->addWidget(prev);
    row->addWidget(label);
    row->addWidget(next);
    row->addStretch(1);

    QObject::connect(prev, &QToolButton::clicked, pager, &ListPager::prev);
    QObject::connect(next, &QToolButton::clicked, pager, &ListPager::next);
    auto refresh = [=]() {
        prev->setEnabled(pager->page() > 0);
        next->setEnabled(pager->page() < pager->pageCount() - 1);
        label->setText(pager->matchCount() == 0
            ? utils::tr(QStringLiteral("kip.pager.no_matches"))
            : utils::tr(QStringLiteral("kip.pager.page")).arg(pager->page() + 1).arg(pager->pageCount())
                + QStringLiteral("  ·  ")
                + utils::tr(QStringLiteral("kip.pager.items")).arg(pager->matchCount()));
        bar->setVisible(pager->paged() && (pager->pageCount() > 1 || pager->matchCount() == 0));
    };
    QObject::connect(pager, &ListPager::changed, bar, refresh);
    refresh();
    return bar;
}

namespace {
ListPager *attachListPager(QListWidget *list, QLineEdit *search, int pageSize)
{
    auto *pager = new ListPager(
        list->count(), pageSize,
        [list](int row, const QString &needle) { return list->item(row)->text().toLower().contains(needle); },
        [list](int row, bool hidden) { list->item(row)->setHidden(hidden); }, list);
    if (search) {
        QObject::connect(search, &QLineEdit::textChanged, pager, &ListPager::setFilter);
    }
    return pager;
}

// Mantém a lista com a altura de uma página, para o layout não pular entre páginas. A altura da linha só é confiável
// depois de a lista receber fonte e estilo finais, então é recalculada ao mostrar ou re-estilizar, não na construção.
class PageHeightFilter : public QObject {
public:
    PageHeightFilter(QListWidget *list, int pageSize)
        : QObject(list)
        , m_list(list)
        , m_pageSize(pageSize)
    {
        list->installEventFilter(this);
        apply();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_list
            && (event->type() == QEvent::Show || event->type() == QEvent::StyleChange
                || event->type() == QEvent::FontChange || event->type() == QEvent::Polish)) {
            apply();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void apply()
    {
        int rowHeight = standardRowHeight();
        for (int i = 0; i < m_list->count(); ++i) { // a 1ª linha visível representa as demais
            if (!m_list->item(i)->isHidden()) {
                rowHeight = qMax(rowHeight, m_list->sizeHintForRow(i));
                break;
            }
        }
        const int rows = qBound(1, m_pageSize, qMax(1, m_list->count()));
        const int height = rows * rowHeight + 2 * (tk::space(1) + 1) + 4;
        if (m_list->minimumHeight() != height || m_list->maximumHeight() != height) {
            m_list->setMinimumHeight(height);
            m_list->setMaximumHeight(height);
        }
    }

    QListWidget *m_list;
    int m_pageSize;
};

// Com paginação a barra de páginas vem logo abaixo da lista. nullptr sem paginação.
QWidget *addPagerBar(QVBoxLayout *vbox, QWidget *container, ListPager *pager, QListWidget *list, int pageSize)
{
    if (pageSize <= 0) {
        return nullptr;
    }
    new PageHeightFilter(list, pageSize);
    QWidget *bar = makePagerBar(container, pager);
    vbox->addWidget(bar);
    return bar;
}
} // namespace

ChoiceListField makeCheckList(QWidget *parent, const QVector<ChoiceOption> &options, const QStringList &checkedValues,
                              bool withFilter, int pageSize)
{
    QVBoxLayout *vbox = nullptr;
    QWidget *container = makeTransparentColumn(parent, QStringLiteral("paramMultiSelectWrap"), &vbox, 4);

    QLineEdit *search = nullptr;
    if (withFilter) {
        search = makeFilterEdit(container);
        vbox->addWidget(search);
    }

    auto *list = new QListWidget(container);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    styleChoiceList(list);
    bool anyDescription = false;
    for (const ChoiceOption &o : options) {
        auto *item = new QListWidgetItem(o.label, list);
        item->setData(Qt::UserRole, o.value);
        if (!o.description.isEmpty()) {
            item->setData(kDescriptionRole, o.description);
            anyDescription = true;
        }
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checkedValues.contains(o.value) ? Qt::Checked : Qt::Unchecked);
    }
    if (anyDescription) {
        list->setItemDelegate(new ChoiceItemDelegate(list));
    }
    QObject::connect(list, &QListWidget::itemClicked, list, [](QListWidgetItem *it) {
        if (!it) return;
        it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });
    // O filtro só esconde itens; os ocultos mantêm o estado marcado, e o valor final vem de todos os marcados.
    ListPager *pager = attachListPager(list, search, pageSize);
    vbox->addWidget(list);
    QWidget *bar = addPagerBar(vbox, container, pager, list, pageSize);
    list->installEventFilter(new SpaceToggleFilter(list));
    return {container, list, search, pager, bar};
}

ChoiceListField makeSingleChoiceList(QWidget *parent, const QVector<ChoiceOption> &options,
                                     const QString &selectedValue, bool withFilter, int pageSize)
{
    QVBoxLayout *vbox = nullptr;
    QWidget *container = makeTransparentColumn(parent, QStringLiteral("paramSingleChoiceWrap"), &vbox, 4);

    QLineEdit *search = nullptr;
    if (withFilter) {
        search = makeFilterEdit(container);
        vbox->addWidget(search);
    }

    auto *list = new QListWidget(container);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    styleChoiceList(list);
    bool anyDescription = false;
    for (const ChoiceOption &o : options) {
        auto *item = new QListWidgetItem(o.label, list);
        item->setData(Qt::UserRole, o.value);
        if (!o.description.isEmpty()) {
            item->setData(kDescriptionRole, o.description);
            anyDescription = true;
        }
        if (o.value == selectedValue) {
            list->setCurrentItem(item);
        }
    }
    if (anyDescription) {
        list->setItemDelegate(new ChoiceItemDelegate(list));
    }
    ListPager *pager = attachListPager(list, search, pageSize);
    vbox->addWidget(list);
    QWidget *bar = addPagerBar(vbox, container, pager, list, pageSize);
    return {container, list, search, pager, bar};
}

QCheckBox *makeSwitchField(QWidget *parent, const QString &text, bool checked)
{
    auto *field = new QCheckBox(text, parent);
    field->setProperty("kaiRole", QStringLiteral("switch"));
    field->setChecked(checked);
    return field;
}

QSpinBox *makeSpinField(QWidget *parent, const QString &initialValue)
{
    auto *field = new QSpinBox(parent);
    field->setRange(-1000000000, 1000000000);
    field->setFont(tk::monoFont(field->font().pointSize()));
    bool ok = false;
    const int v = initialValue.toInt(&ok);
    field->setValue(ok ? v : 0);
    return field;
}

QDoubleSpinBox *makeNumberField(QWidget *parent, const core::KipField &field)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setFont(tk::monoFont(spin->font().pointSize()));
    spin->setDecimals(field.decimals);
    const double step = field.step.value_or(1.0);
    const double lowest = field.min.value_or(-1e12);
    spin->setSingleStep(step);
    spin->setMaximum(field.max.value_or(1e12));
    // Um degrau abaixo do mínimo real é o "vazio" (texto especial): o programa recebe null se não foi preenchido.
    spin->setMinimum(lowest - step);
    spin->setSpecialValueText(field.placeholder.isEmpty() ? QStringLiteral("—") : field.placeholder);
    spin->setValue(spin->minimum());
    return spin;
}

PathPickField makePathPickField(QWidget *parent, const QString &initial, const QString &pickMode,
                                const QString &initialDir, const QString &pathFormat, const QString &filter)
{
    // O botão é uma extensão do campo: uma única borda em volta do conjunto.
    auto *container = new QWidget(parent);
    container->setObjectName(QStringLiteral("fileInputGroup"));
    container->setStyleSheet(QStringLiteral(
        "QWidget#fileInputGroup { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(tk::bg(), tk::borderColor())
        .arg(tk::radiusMd()));
    auto *rowLayout = new QHBoxLayout(container);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);

    auto *edit = new QLineEdit(initial, container);
    edit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; color: %1; border: none;"
        " padding: %2px %3px; }")
        .arg(tk::fg())
        .arg(tk::space(2)).arg(tk::space(3)));
    // Ícone e tooltip refletem o modo (arquivo, pasta ou ambos); "both" usa um ícone neutro e abre um menu.
    const QString mode = pickMode.isEmpty() ? QStringLiteral("file") : pickMode;
    const QString browseIcon = mode == QStringLiteral("folder") ? QStringLiteral("folder")
        : mode == QStringLiteral("both") ? QStringLiteral("folder-open")
                                          : QStringLiteral("file");
    const QString browseTip = mode == QStringLiteral("folder")
        ? utils::tr(QStringLiteral("params.folder.browse"))
        : mode == QStringLiteral("both")
        ? utils::tr(QStringLiteral("params.pick_mode.both"))
        : utils::tr(QStringLiteral("params.file.browse"));
    auto *browseButton = makeIconButton(container, browseIcon, browseTip, QColor(tk::accent()));
    browseButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-left: 1px solid %1;"
        " border-radius: 0px; }")
        .arg(tk::borderColor()));

    QObject::connect(browseButton, &QToolButton::clicked, container,
        [container, browseButton, edit, initialDir, pathFormat, mode, filter]() {
            if (mode == QStringLiteral("both")) {
                // Nenhum QFileDialog nativo escolhe arquivo ou pasta ao mesmo tempo: pergunta antes.
                QMenu menu(container);
                QAction *fileAction = menu.addAction(utils::tr(QStringLiteral("params.pick_mode.choose_file")));
                QAction *folderAction = menu.addAction(utils::tr(QStringLiteral("params.pick_mode.choose_folder")));
                QAction *chosen = menu.exec(browseButton->mapToGlobal(QPoint(0, browseButton->height())));
                if (chosen == fileAction) {
                    browseInto(container, edit, initialDir, pathFormat, false, filter);
                } else if (chosen == folderAction) {
                    browseInto(container, edit, initialDir, pathFormat, true, filter);
                }
                return;
            }
            browseInto(container, edit, initialDir, pathFormat, mode == QStringLiteral("folder"), filter);
        });

    rowLayout->addWidget(edit, 1);
    rowLayout->addWidget(browseButton);
    return {container, edit};
}

void setDatePickValues(QLineEdit *edit, const QDateTime &start, const QDateTime &end, bool range,
                       const DateFormatter &format)
{
    edit->setProperty(kDatePropStart, start);
    const QString startFormatted = format(start);
    // "kaiDateStartFormatted" é o valor limpo devolvido a quem consome; o texto do campo pode combinar início e fim só
    // para leitura.
    edit->setProperty(kDatePropStartFormatted, startFormatted);
    if (range) {
        edit->setProperty(kDatePropEnd, end);
        const QString endFormatted = format(end);
        edit->setProperty(kDatePropEndFormatted, endFormatted);
        edit->setText(QStringLiteral("%1  —  %2").arg(startFormatted, endFormatted));
    } else {
        edit->setText(startFormatted);
    }
}

DatePickField makeDatePickField(QWidget *parent, const QString &initialText, const QString &mode, bool range,
                                const DateFormatter &format)
{
    // O campo é somente leitura: o valor só muda pelo DatePickerDialog, nunca digitado (evita texto que não bate com
    // nenhum formato válido).
    auto *container = new QWidget(parent);
    container->setObjectName(QStringLiteral("dateInputGroup"));
    container->setStyleSheet(QStringLiteral(
        "QWidget#dateInputGroup { background-color: %1; border: 1px solid %2;"
        " border-radius: %3px; }")
        .arg(tk::bg(), tk::borderColor())
        .arg(tk::radiusMd()));
    auto *rowLayout = new QHBoxLayout(container);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);

    auto *edit = new QLineEdit(initialText, container);
    edit->setReadOnly(true);
    edit->setPlaceholderText(utils::tr(QStringLiteral("params.date.empty")));
    edit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; color: %1; border: none;"
        " padding: %2px %3px; }")
        .arg(tk::fg())
        .arg(tk::space(2)).arg(tk::space(3)));
    auto *pickButton = makeIconButton(container, QStringLiteral("calendar"),
        utils::tr(QStringLiteral("params.date.pick")), QColor(tk::accent()));
    pickButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-left: 1px solid %1;"
        " border-radius: 0px; }")
        .arg(tk::borderColor()));

    QObject::connect(pickButton, &QToolButton::clicked, container, [container, edit, mode, range, format]() {
        const QDateTime seedStart = edit->property(kDatePropStart).toDateTime();
        const QDateTime seedEnd = edit->property(kDatePropEnd).toDateTime();
        DatePickerDialog dialog(mode, range, seedStart, seedEnd, container);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        setDatePickValues(edit, dialog.startValue(), range ? dialog.endValue() : QDateTime(), range, format);
    });

    rowLayout->addWidget(edit, 1);
    rowLayout->addWidget(pickButton);
    return {container, edit};
}

QTableWidget *makeDataTable(QWidget *parent, const QVector<core::KipColumn> &columns, const QJsonArray &rows,
                            const QString &rowKey, DataTableMode mode, const QVector<core::KipChip> &rowActions)
{
    const bool withActions = !rowActions.isEmpty();
    const int dataColumns = columns.size();
    auto *table = new DataTableWidget(rows.size(), dataColumns + (withActions ? 1 : 0), parent);
    const auto cellText = [](const core::KipColumn &column, const QJsonValue &value) {
        return column.format == QLatin1String("badge") ? core::kipBadgeText(value) : core::kipCellText(value);
    };
    QList<TableColumnSpec> specs;
    QStringList formats;
    bool anyFormat = withActions;
    for (int c = 0; c < dataColumns; ++c) {
        const core::KipColumn &column = columns.at(c);
        // Largura inicial pelo conteúdo (limitada); a última coluna estica. Colunas mono medem na fonte mono.
        const bool mono = column.format == QLatin1String("mono") || column.format == QLatin1String("copy");
        const QFontMetrics fm(mono ? tk::monoFont(qMax(8, tk::fontSizePt() - 1)) : table->font());
        const int extra = column.format == QLatin1String("badge") ? 30 : column.format == QLatin1String("copy") ? 28 : 0;
        int width = QFontMetrics(table->font()).horizontalAdvance(column.label) + 2 * tk::space(5);
        for (const QJsonValue &r : rows) {
            width = qMax(width, fm.horizontalAdvance(cellText(column, r.toObject().value(column.key))) + 2 * tk::space(5) + extra);
        }
        specs.append({column.label, qBound(80, width, 300), c == dataColumns - 1});
        formats << column.format;
        anyFormat = anyFormat || !column.format.isEmpty();
    }
    if (withActions) {
        specs.append({QString(), RowActionsController::columnWidthFor(rowActions.size()), false});
        formats << QString();
    }
    configureTable(table, specs);
    table->setColumnFormats(formats);
    if (anyFormat) {
        table->setItemDelegate(new DataCellDelegate(table));
        table->viewport()->setAttribute(Qt::WA_Hover, true);
        table->setMouseTracking(true);
    }
    if (withActions) {
        // A coluna de ações tem largura fixa; quem estica é a última coluna de dados.
        QHeaderView *header = table->horizontalHeader();
        header->setStretchLastSection(false);
        header->setSectionResizeMode(dataColumns - 1, QHeaderView::Stretch);
        header->setSectionResizeMode(dataColumns, QHeaderView::Fixed);
        header->resizeSection(dataColumns, RowActionsController::columnWidthFor(rowActions.size()));
    }
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(false);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(standardRowHeight());
    table->setSelectionBehavior(mode == DataTableMode::ReadOnly ? QAbstractItemView::SelectItems
                                                                : QAbstractItemView::SelectRows);
    table->setSelectionMode(mode == DataTableMode::SelectRows ? QAbstractItemView::MultiSelection
                                                              : QAbstractItemView::SingleSelection);
    for (int r = 0; r < rows.size(); ++r) {
        const QJsonObject row = rows.at(r).toObject();
        const QString key = core::kipCellText(row.value(rowKey));
        for (int c = 0; c < dataColumns; ++c) {
            const core::KipColumn &column = columns.at(c);
            const QJsonValue value = row.value(column.key);
            auto *item = new QTableWidgetItem(cellText(column, value));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setData(kRowKeyRole, key);
            item->setToolTip(item->text());
            if (column.format == QLatin1String("badge")) {
                QString tone;
                core::kipBadgeText(value, &tone);
                item->setData(kBadgeToneRole, tone);
            }
            table->setItem(r, c, item);
        }
        if (withActions) {
            auto *item = new QTableWidgetItem();
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setData(kRowKeyRole, key);
            item->setData(kRowActionsRole, core::kipRowActionIds(row, rowActions));
            table->setItem(r, dataColumns, item);
        }
    }
    if (withActions) {
        new RowActionsController(table, dataColumns, rowActions);
    }
    return table;
}

RowActionsController *rowActionsOf(const QTableWidget *table)
{
    return table ? table->findChild<RowActionsController *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

QStringList selectedRowKeys(const QTableWidget *table)
{
    QStringList keys;
    QList<int> rowsSelected;
    for (const QModelIndex &index : table->selectionModel()->selectedRows()) {
        rowsSelected << index.row();
    }
    std::sort(rowsSelected.begin(), rowsSelected.end());
    for (int row : rowsSelected) {
        if (QTableWidgetItem *item = table->item(row, 0)) {
            keys << item->data(Qt::UserRole).toString();
        }
    }
    return keys;
}

void selectRowKeys(QTableWidget *table, const QStringList &keys)
{
    table->clearSelection();
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem *item = table->item(row, 0);
        if (item && keys.contains(item->data(Qt::UserRole).toString())) {
            // Direto no modelo de seleção: QTableView::selectRow() em modo
            // MultiSelection alterna (toggle) em vez de selecionar.
            table->selectionModel()->select(table->model()->index(row, 0),
                                            QItemSelectionModel::Select | QItemSelectionModel::Rows);
            if (table->selectionMode() == QAbstractItemView::SingleSelection) {
                break;
            }
        }
    }
}

int dataTableHeight(const QTableWidget *table, int visibleRows)
{
    return table->horizontalHeader()->sizeHint().height() + visibleRows * table->verticalHeader()->defaultSectionSize()
        + 2 * table->frameWidth() + 4;
}

void fitDataTableHeight(QTableWidget *table, int visibleRows)
{
    if (auto *dataTable = dynamic_cast<DataTableWidget *>(table)) {
        dataTable->fitVisibleRows(visibleRows);
    } else {
        table->setFixedHeight(dataTableHeight(table, visibleRows));
    }
}

TableChoiceField makeTableChoice(QWidget *parent, const core::KipField &field, bool withFilter, int pageSize)
{
    QVBoxLayout *vbox = nullptr;
    QWidget *container = makeTransparentColumn(parent, QStringLiteral("paramTableChoiceWrap"), &vbox, 4);
    QLineEdit *filter = nullptr;
    if (withFilter) {
        filter = makeFilterEdit(container);
        vbox->addWidget(filter);
    }
    QTableWidget *table = makeDataTable(container, field.columns, field.rows, field.rowKey,
                                        field.multiple ? DataTableMode::SelectRows : DataTableMode::SelectRow,
                                        field.rowActions);
    const int visibleRows = pageSize > 0 ? qBound(1, pageSize, qMax(1, field.rows.size())) : qBound(3, field.rows.size(), 7);
    fitDataTableHeight(table, visibleRows);
    auto *pager = new ListPager(
        table->rowCount(), pageSize,
        [table](int row, const QString &needle) {
            for (int c = 0; c < table->columnCount(); ++c) {
                if (const QTableWidgetItem *item = table->item(row, c)) {
                    if (item->text().toLower().contains(needle)) return true;
                }
            }
            return false;
        },
        [table](int row, bool hidden) { table->setRowHidden(row, hidden); }, table);
    if (filter) {
        QObject::connect(filter, &QLineEdit::textChanged, pager, &ListPager::setFilter);
    }
    vbox->addWidget(table);
    QWidget *bar = nullptr;
    if (pageSize > 0) {
        bar = makePagerBar(container, pager);
        vbox->addWidget(bar);
    }
    return {container, table, filter, pager, bar};
}

FlagsField makeFlagsField(QWidget *parent, const QVector<core::KipFlagOption> &flags, const QJsonObject &values)
{
    QVBoxLayout *vbox = nullptr;
    QWidget *container = makeTransparentColumn(parent, QStringLiteral("paramFlagsWrap"), &vbox, tk::space(2));
    FlagsField out;
    out.container = container;
    for (const core::KipFlagOption &flag : flags) {
        auto *box = makeSwitchField(container, flag.label, values.value(flag.name).toBool(flag.defaultValue));
        vbox->addWidget(box);
        if (!flag.description.isEmpty()) {
            auto *desc = new QLabel(flag.description, container);
            desc->setWordWrap(true);
            desc->setContentsMargins(tk::space(8), 0, 0, 0);
            desc->setStyleSheet(QStringLiteral("color: %1;").arg(tk::mutedFg()));
            vbox->addWidget(desc);
        }
        out.boxes << box;
    }
    return out;
}

} // namespace fields

} // namespace kai::ui
