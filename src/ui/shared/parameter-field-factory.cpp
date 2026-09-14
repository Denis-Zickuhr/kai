#include "ui/shared/parameter-field-factory.h"

#include "ui/shared/date-picker-dialog.h"
#include "ui/shared/dialog-utils.h"
#include "ui/shared/inline-code-field.h"
#include "ui/shared/lucide-icons.h"
#include "ui/shared/table-utils.h"
#include "utils/design-tokens.h"
#include "utils/path-format.h"
#include "utils/translation-manager.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include "ui/shared/file-dialogs.h"

namespace kai::ui {

namespace fields {

namespace {

namespace tk = kai::utils::tokens;

// Descrição de uma opção (linha secundária), lida pelo delegate abaixo.
constexpr int kDescriptionRole = Qt::UserRole + 1;

// Alterna o item com foco ao apertar Espaço numa lista de checkboxes —
// QListWidget não faz isso sozinho (diretriz da tela de Params). Instalado
// como filtro na própria lista, que é o pai do filtro (vive e morre com ela).
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

// Item com rótulo + descrição mais discreta embaixo (só quando há descrição;
// sem ela o desenho é o do QStyledItemDelegate padrão).
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

// Estilo de QListWidget compartilhado pelas listas de escolha.
void styleChoiceList(QListWidget *list)
{
    list->setFrameShape(QFrame::NoFrame);
    // O viewport interno pintava um quadrado escuro POR TRÁS do frame
    // arredondado (o "duplo fundo" relatado). Deixamos o viewport transparente;
    // só o QListWidget desenha o fundo, com o raio do tema (preferência de
    // borda). QUALIFICADO por objectName — não um "background: transparent;"
    // cru: uma regra CRUA aplicada via setStyleSheet() num viewport quebra a
    // cascata de QSS para os DESCENDENTES daquele viewport (ver o comentário
    // equivalente no scroll area do ParameterFormDialog).
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
    // A caixinha mostra pelo menos 5 linhas e no máximo 8 (para não engolir o
    // diálogo quando a lista de opções é enorme).
    list->setMinimumHeight(5 * standardRowHeight());
    list->setMaximumHeight(8 * standardRowHeight());
}

// Campo de filtro com lupa embutida, mesmo visual dos demais campos.
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

// Escolhe o arquivo/pasta e escreve em `target` já convertido ao formato de
// path pedido (pedido do usuário: o diálogo nativo devolve o formato do SO do
// Kai — sob WSLg isso costuma ser um path Windows, inútil num comando bash).
void browseInto(QWidget *dialogParent, QLineEdit *target, const QString &initialDir, const QString &pathFormat,
                bool pickFolder, const QString &filter)
{
    // ONDE ABRIR. Ordem de precedência:
    //  1) a pasta do arquivo JÁ escolhido no campo (continuar de onde parou);
    //  2) a pasta sugerida por quem pediu o campo (só o KIP traz uma: `initial_dir`);
    //  3) o último diretório usado em qualquer seletor do Kai.
    const QString current = target ? target->text().trimmed() : QString();
    const QString hint = QFileInfo::exists(current) ? current : initialDir.trimmed();

    // Sempre o diálogo nativo do sistema operacional (sem DontUseNativeDialog).
    const QString path = pickFolder
        ? pickDirectory(dialogParent, utils::tr(QStringLiteral("params.select_folder")), hint)
        : pickOpenFile(dialogParent, utils::tr(QStringLiteral("params.select_file")), filter, hint);
    if (!path.isEmpty()) {
        target->setText(utils::convertFilePathFormat(path, pathFormat));
    }
}

// Tabela de dados: Ctrl+C e o menu de contexto copiam a célula atual (§11).
class DataTableWidget : public QTableWidget {
public:
    using QTableWidget::QTableWidget;

    // Altura que mostra `rows` linhas inteiras. É recalculada quando o tema é
    // aplicado e ao exibir: na criação o header ainda não tem o padding do QSS
    // e parece bem menor do que será, o que roubava linhas da tabela.
    void fitVisibleRows(int rows)
    {
        m_visibleRows = rows;
        refit();
    }

protected:
    bool event(QEvent *e) override
    {
        const bool handled = QTableWidget::event(e);
        if (e->type() == QEvent::StyleChange || e->type() == QEvent::Polish
            || e->type() == QEvent::Show || e->type() == QEvent::FontChange) {
            refit();
        }
        return handled;
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->matches(QKeySequence::Copy)) {
            copyCurrentCell();
            return;
        }
        QTableWidget::keyPressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QTableWidgetItem *item = itemAt(event->pos());
        if (!item) {
            return;
        }
        setCurrentItem(item);
        QMenu menu(this);
        QAction *copy = menu.addAction(
            LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 16),
            utils::tr(QStringLiteral("kip.table.copy_cell")));
        if (menu.exec(event->globalPos()) == copy) {
            copyCurrentCell();
        }
    }

private:
    void refit()
    {
        if (m_visibleRows <= 0) {
            return;
        }
        const int h = horizontalHeader()->sizeHint().height()
            + m_visibleRows * verticalHeader()->defaultSectionSize() + 2 * frameWidth() + 4;
        if (h != height() || h != minimumHeight() || h != maximumHeight()) {
            setFixedHeight(h);
        }
    }

    void copyCurrentCell()
    {
        if (QTableWidgetItem *item = currentItem()) {
            QApplication::clipboard()->setText(item->text());
        }
    }

    int m_visibleRows = 0;
};

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
    // Transparente, mas QUALIFICADO por objectName: uma regra crua
    // ("background: transparent;" sem seletor) CASCATEIA para os filhos e
    // deixava o campo (QLineEdit do tipo Text) SEM fundo escuro — herdava o
    // transparent em vez do bg() do QSS global (relatado; File/Select não
    // sofriam porque têm um container de fundo próprio). Restrito a #objectName,
    // só o wrap fica transparente; os campos mantêm seu bg.
    container->setStyleSheet(QStringLiteral(
        "QWidget#paramFieldWrap { background: transparent; }"));
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(tk::space(1));
    // Marcação de OBRIGATÓRIO (diretrizes da tela de Params: "campos
    // obrigatórios devem ter uma pequena marcação, como um asterisco vermelho
    // sutil, ao lado do rótulo"). Rich text só quando precisa.
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

// ---------------------------------------------------------------- texto

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
    // Reaproveita InlineCodeField — o MESMO widget "campo compacto que cresce
    // sozinho + botão de expandir para um editor grande" usado pelo Shell
    // COMMAND e pelo HTTP BODY em CommandEditorDialog. word-wrap (WidgetWidth)
    // em vez do NoWrap padrão do widget (pensado pra comando/JSON): é texto livre.
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
    // MINI EDITOR JSON: também reaproveita InlineCodeField, com realce JSON —
    // ganha o botão de expandir de graça.
    auto *field = new InlineCodeField(parent);
    field->setEditorTitle(label);
    field->setJsonSyntax(true);
    field->setLineRange(3, 6);
    if (!initial.trimmed().isEmpty()) {
        field->setPlainText(initial);
    }

    // Rótulo + Formatar/Minificar na MESMA linha (mesmo padrão de "COMMAND
    // SCRIPT"+engrenagem / "BODY"+formatar do CommandEditorDialog).
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

// ---------------------------------------------------------------- escolhas

namespace {
QComboBox *buildCombo(QWidget *parent, QVector<ChoiceOption> opts, const QStringList &history,
                      const QString &initialValue, const QString *emptyLabel)
{
    auto *field = new QComboBox(parent);

    // Ordenação por HISTÓRICO DE USO: os valores usados mais recentemente vêm
    // primeiro; o resto mantém a ordem original de definição. stable_sort
    // preserva a ordem relativa dentro de cada grupo.
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

    // Busca avançada: combo editável com completer que filtra por "contém"
    // (não só prefixo), case-insensitive. O completer usa o modelo do próprio
    // combo, então respeita a ordem por histórico. Como é editável, quem lê o
    // valor precisa resolver o texto para uma opção válida.
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
                           const QString &initialValue)
{
    return buildCombo(parent, options, usageHistory, initialValue, nullptr);
}

QComboBox *makeOptionalSelectCombo(QWidget *parent, const QVector<ChoiceOption> &options, const QString &emptyLabel,
                                   const QString &initialValue)
{
    return buildCombo(parent, options, {}, initialValue, &emptyLabel);
}

// ---------------------------------------------------------------- filtro + paginação

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
// Liga o filtro (se houver) e a paginação a uma QListWidget já populada.
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

// Mantém a lista com a altura de UMA página (o layout não pula entre páginas).
// A altura da linha só é confiável depois que a lista recebeu fonte e estilo
// finais (o pai só aparece depois), então é recalculada quando ela é mostrada
// ou re-estilizada, e não na construção.
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
    // Clicar em QUALQUER lugar da linha alterna o check.
    QObject::connect(list, &QListWidget::itemClicked, list, [](QListWidgetItem *it) {
        if (!it) return;
        it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });
    // Filtro in-mem: esconde itens cujo rótulo não contém o texto. Não altera o
    // estado marcado dos ocultos — o valor final vem de todos os itens marcados.
    ListPager *pager = attachListPager(list, search, pageSize);
    vbox->addWidget(list);
    QWidget *bar = addPagerBar(vbox, container, pager, list, pageSize);
    // "Espaço" marca/desmarca o item com foco; setas já são nativas.
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

// ---------------------------------------------------------------- bool / número

QCheckBox *makeSwitchField(QWidget *parent, const QString &text, bool checked)
{
    // Toggle switch com rótulo explícito: reaproveita o MESMO
    // QCheckBox/kaiRole="switch" já usado no Settings.
    auto *field = new QCheckBox(text, parent);
    field->setProperty("kaiRole", QStringLiteral("switch"));
    field->setChecked(checked);
    return field;
}

QSpinBox *makeSpinField(QWidget *parent, const QString &initialValue)
{
    // Parâmetro numérico: QSpinBox (inteiro) com faixa ampla. Fonte
    // monoespaçada (diretriz do usuário: "fonte monospace para leitura clara").
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
    // Um degrau abaixo do mínimo real vira o "vazio" (mostra o texto especial):
    // o programa recebe null quando o usuário não preencheu o número.
    spin->setMinimum(lowest - step);
    spin->setSpecialValueText(field.placeholder.isEmpty() ? QStringLiteral("—") : field.placeholder);
    spin->setValue(spin->minimum());
    return spin;
}

// ---------------------------------------------------------------- arquivo / data

PathPickField makePathPickField(QWidget *parent, const QString &initial, const QString &pickMode,
                                const QString &initialDir, const QString &pathFormat, const QString &filter)
{
    // "Arquivo / Expressão": o botão [...] vira uma EXTENSÃO do input
    // (input-group-addon) — uma única borda em volta do conjunto, sem gap/raio
    // destoando entre campo e botão.
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
    // Botão de ícone colado ao campo, com só um separador fino entre os dois.
    // Ícone/tooltip refletem o MODO configurado ("arquivo, pastas ou ambos").
    // "both" usa um ícone neutro (o botão abre um menu perguntando qual dos dois).
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
                // Nenhum QFileDialog nativo deixa escolher arquivo OU pasta ao
                // mesmo tempo — pergunta qual dos dois antes de abrir o diálogo.
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
    // "kaiDateStartFormatted" é o valor LIMPO (sem o "— fim" decorativo) que
    // quem consome devolve como valor — field->text() pode ficar "rico"
    // (combinando início/fim) só pra leitura visual.
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
    // Mesmo bloco visual "input-group" do arquivo (campo + botão fundidos numa
    // borda só) — o botão abre a "janelinha" (DatePickerDialog). O campo em si
    // é SOMENTE LEITURA: o valor só muda pela janelinha, nunca digitado à mão
    // (evita um texto que não bate com nenhum formato válido).
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

// ---------------------------------------------------------------- tabela / flags

QTableWidget *makeDataTable(QWidget *parent, const QVector<core::KipColumn> &columns, const QJsonArray &rows,
                            const QString &rowKey, DataTableMode mode)
{
    auto *table = new DataTableWidget(rows.size(), columns.size(), parent);
    QList<TableColumnSpec> specs;
    for (int c = 0; c < columns.size(); ++c) {
        // Largura inicial pelo conteúdo (limitada); a última coluna estica.
        const QFontMetrics fm(table->font());
        int width = fm.horizontalAdvance(columns.at(c).label) + 2 * tk::space(5);
        for (const QJsonValue &r : rows) {
            width = qMax(width, fm.horizontalAdvance(core::kipCellText(r.toObject().value(columns.at(c).key)))
                                    + 2 * tk::space(5));
        }
        specs.append({columns.at(c).label, qBound(80, width, 300), c == columns.size() - 1});
    }
    configureTable(table, specs);
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
        for (int c = 0; c < columns.size(); ++c) {
            auto *item = new QTableWidgetItem(core::kipCellText(row.value(columns.at(c).key)));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setData(Qt::UserRole, key);
            item->setToolTip(item->text());
            table->setItem(r, c, item);
        }
    }
    return table;
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
                                        field.multiple ? DataTableMode::SelectRows : DataTableMode::SelectRow);
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

// ============================================================================
// KipFieldEditor
// ============================================================================

namespace {

QDateTime parseIso(const QString &text, const QString &mode)
{
    if (mode == QLatin1String("time")) {
        const QTime t = QTime::fromString(text, QStringLiteral("HH:mm:ss"));
        return t.isValid() ? QDateTime(QDate::currentDate(), t) : QDateTime();
    }
    if (mode == QLatin1String("datetime")) {
        return QDateTime::fromString(text, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    }
    const QDate d = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    return d.isValid() ? QDateTime(d, QTime(0, 0)) : QDateTime();
}

QVector<fields::ChoiceOption> choiceOptions(const core::KipField &field)
{
    QVector<fields::ChoiceOption> out;
    for (const core::KipOption &o : field.options) {
        out.append({o.label, o.value, o.description});
    }
    return out;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) out << v.toString();
    } else if (value.isString() && !value.toString().isEmpty()) {
        out << value.toString();
    }
    return out;
}

} // namespace

KipFieldEditor::KipFieldEditor(const core::KipField &field, const QJsonValue &initial, QWidget *parent)
    : QObject(nullptr)
    , m_field(field)
{
    m_widget = nullptr;
    // O editor vive e morre junto do widget que ele criou.
    build(initial);
    if (m_widget) {
        m_widget->setParent(parent);
        setParent(m_widget);
    }
}

void KipFieldEditor::build(const QJsonValue &initial)
{
    QWidget *host = nullptr; // pai provisório; o chamador reparenta
    const QString placeholder = m_field.placeholder;
    const auto lineText = [&initial]() { return initial.isString() ? initial.toString() : QString(); };

    switch (m_field.type) {
    case core::KipFieldType::Text:
        m_line = fields::makeTextField(host, lineText(), placeholder);
        m_widget = m_focus = m_line;
        break;
    case core::KipFieldType::Secret:
        m_line = fields::makeSecretField(host, lineText(), placeholder);
        m_widget = m_focus = m_line;
        break;
    case core::KipFieldType::Textarea:
        m_code = fields::makeTextareaField(host, m_field.label.isEmpty() ? m_field.name : m_field.label,
                                           placeholder, lineText());
        m_widget = m_code;
        m_focus = m_code->editor();
        break;
    case core::KipFieldType::Number:
        m_spin = fields::makeNumberField(host, m_field);
        m_numberEmpty = m_spin->minimum();
        m_widget = m_focus = m_spin;
        break;
    case core::KipFieldType::Date: {
        const QString mode = m_field.dateMode;
        auto picked = fields::makeDatePickField(host, QString(), mode, m_field.range,
            [mode](const QDateTime &dt) { return fields::isoDateText(mode, dt); });
        m_line = picked.edit;
        m_widget = picked.container;
        m_focus = m_line;
        break;
    }
    case core::KipFieldType::Select: {
        const QString initialText = initial.isString() ? initial.toString() : QString();
        m_combo = fields::makeOptionalSelectCombo(host, choiceOptions(m_field), QStringLiteral("—"), initialText);
        m_widget = m_focus = m_combo;
        break;
    }
    case core::KipFieldType::List: {
        const bool withFilter = m_field.searchable.value_or(m_field.options.size() > fields::kFilterThreshold);
        fields::ChoiceListField built = m_field.multiple
            ? fields::makeCheckList(host, choiceOptions(m_field), stringList(initial), withFilter, m_field.pageSize)
            : fields::makeSingleChoiceList(host, choiceOptions(m_field),
                                           initial.isString() ? initial.toString() : QString(), withFilter,
                                           m_field.pageSize);
        m_list = built.list;
        m_filter = built.filter;
        m_pager = built.pager;
        m_widget = built.container;
        m_focus = m_list;
        break;
    }
    case core::KipFieldType::Table: {
        fields::TableChoiceField built = fields::makeTableChoice(
            host, m_field, m_field.searchable.value_or(m_field.rows.size() > fields::kFilterThreshold),
            m_field.pageSize);
        m_table = built.table;
        m_filter = built.filter;
        m_pager = built.pager;
        m_widget = built.container;
        m_focus = m_table;
        break;
    }
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick: {
        const bool folder = m_field.type == core::KipFieldType::Folderpick;
        auto picked = fields::makePathPickField(host, lineText(), folder ? QStringLiteral("folder") : QStringLiteral("file"),
                                                m_field.initialDir, m_field.pathFormat, m_field.filter);
        m_line = picked.edit;
        m_widget = picked.container;
        m_focus = m_line;
        break;
    }
    case core::KipFieldType::Flags: {
        auto flags = fields::makeFlagsField(host, m_field.flags, initial.isObject() ? initial.toObject() : QJsonObject());
        m_flagBoxes = flags.boxes;
        m_widget = flags.container;
        m_focus = m_flagBoxes.isEmpty() ? m_widget : static_cast<QWidget *>(m_flagBoxes.first());
        break;
    }
    }

    // Valor inicial dos tipos que o construtor não recebe pronto.
    if (m_field.type == core::KipFieldType::Number || m_field.type == core::KipFieldType::Date
        || m_field.type == core::KipFieldType::Table) {
        m_applying = true;
        setValue(initial);
        m_applying = false;
    }

    m_lastValue = value();
    // Sinais de mudança → valueChanged/userEdited.
    const auto touch = [this]() { notify(true); };
    if (m_line) connect(m_line, &QLineEdit::textChanged, this, touch); // no Date, é a janelinha que escreve o texto
    if (m_code) connect(m_code, &InlineCodeField::textChanged, this, touch);
    if (m_spin) connect(m_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, touch);
    if (m_combo) {
        connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, touch);
        connect(m_combo, &QComboBox::editTextChanged, this, touch);
    }
    if (m_list) {
        connect(m_list, &QListWidget::itemChanged, this, touch);
        connect(m_list, &QListWidget::itemSelectionChanged, this, touch);
    }
    if (m_table) connect(m_table, &QTableWidget::itemSelectionChanged, this, touch);
    for (QCheckBox *box : m_flagBoxes) {
        connect(box, &QCheckBox::toggled, this, touch);
    }
}

void KipFieldEditor::notify(bool fromUser)
{
    // O Qt costuma emitir mais de um sinal por mudança (um combo emite
    // currentIndexChanged E editTextChanged): só notifica quando o VALOR mudou
    // de fato — senão um `watch` mandaria dois `change` para uma escolha.
    const QJsonValue now = value();
    if (now == m_lastValue) {
        return;
    }
    m_lastValue = now;
    emit valueChanged();
    if (fromUser && !m_applying) {
        emit userEdited();
    }
}

bool KipFieldEditor::isTextLike() const
{
    switch (m_field.type) {
    case core::KipFieldType::Text:
    case core::KipFieldType::Secret:
    case core::KipFieldType::Textarea:
    case core::KipFieldType::Number:
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick:
        return true;
    default:
        return false;
    }
}

QJsonValue KipFieldEditor::value() const
{
    switch (m_field.type) {
    case core::KipFieldType::Text:
    case core::KipFieldType::Secret:
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick:
        return m_line->text();
    case core::KipFieldType::Textarea:
        return m_code->toPlainText();
    case core::KipFieldType::Number: {
        const double v = m_spin->value();
        if (v <= m_numberEmpty) {
            return QJsonValue(QJsonValue::Null);
        }
        // O vazio é um degrau abaixo do mínimo; valores entre os dois (com
        // casas decimais) ainda violam o mínimo declarado: prende nele.
        return m_field.min && v < *m_field.min ? QJsonValue(*m_field.min) : QJsonValue(v);
    }
    case core::KipFieldType::Date: {
        const QDateTime start = m_line->property(fields::kDatePropStart).toDateTime();
        if (!start.isValid()) {
            return QJsonValue(QJsonValue::Null);
        }
        if (!m_field.range) {
            return fields::isoDateText(m_field.dateMode, start);
        }
        const QDateTime end = m_line->property(fields::kDatePropEnd).toDateTime();
        if (!end.isValid()) {
            return QJsonValue(QJsonValue::Null);
        }
        return QJsonObject{{"start", fields::isoDateText(m_field.dateMode, start)},
                           {"end", fields::isoDateText(m_field.dateMode, end)}};
    }
    case core::KipFieldType::Select: {
        // Combo editável: o texto pode ser algo digitado. Resolve para uma
        // opção real (rótulo exato, depois valor exato); texto que não bate com
        // nenhuma opção é "sem escolha" — o programa nunca recebe lixo digitado.
        const QString text = m_combo->currentText();
        int idx = m_combo->findText(text);
        if (idx >= 0) {
            return m_combo->itemData(idx).toString();
        }
        idx = m_combo->findData(text);
        return idx >= 0 ? text : QString();
    }
    case core::KipFieldType::List: {
        QStringList chosen;
        if (m_field.multiple) {
            for (int i = 0; i < m_list->count(); ++i) {
                if (m_list->item(i)->checkState() == Qt::Checked) {
                    chosen << m_list->item(i)->data(Qt::UserRole).toString();
                }
            }
            return QJsonArray::fromStringList(chosen);
        }
        const QListWidgetItem *item = m_list->currentItem();
        return item && item->isSelected() ? item->data(Qt::UserRole).toString() : QString();
    }
    case core::KipFieldType::Table: {
        const QStringList keys = fields::selectedRowKeys(m_table);
        if (m_field.multiple) {
            return QJsonArray::fromStringList(keys);
        }
        return keys.isEmpty() ? QString() : keys.first();
    }
    case core::KipFieldType::Flags: {
        QJsonObject out;
        for (int i = 0; i < m_field.flags.size() && i < m_flagBoxes.size(); ++i) {
            out[m_field.flags.at(i).name] = m_flagBoxes.at(i)->isChecked();
        }
        return out;
    }
    }
    return QJsonValue(QJsonValue::Null);
}

void KipFieldEditor::setValue(const QJsonValue &value)
{
    const bool wasApplying = m_applying;
    m_applying = true;
    switch (m_field.type) {
    case core::KipFieldType::Text:
    case core::KipFieldType::Secret:
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick:
        m_line->setText(value.toString());
        break;
    case core::KipFieldType::Textarea:
        m_code->setPlainText(value.toString());
        break;
    case core::KipFieldType::Number:
        m_spin->setValue(value.isDouble() ? value.toDouble() : m_numberEmpty);
        break;
    case core::KipFieldType::Date: {
        const QString mode = m_field.dateMode;
        const fields::DateFormatter format = [mode](const QDateTime &dt) { return fields::isoDateText(mode, dt); };
        m_line->setProperty(fields::kDatePropStart, QVariant());
        m_line->setProperty(fields::kDatePropEnd, QVariant());
        m_line->setProperty(fields::kDatePropStartFormatted, QVariant());
        m_line->setProperty(fields::kDatePropEndFormatted, QVariant());
        if (m_field.range && value.isObject()) {
            const QDateTime start = parseIso(value.toObject().value("start").toString(), mode);
            const QDateTime end = parseIso(value.toObject().value("end").toString(), mode);
            if (start.isValid() && end.isValid()) {
                fields::setDatePickValues(m_line, start, end, true, format);
                break;
            }
        } else if (!m_field.range && value.isString()) {
            const QDateTime start = parseIso(value.toString(), mode);
            if (start.isValid()) {
                fields::setDatePickValues(m_line, start, QDateTime(), false, format);
                break;
            }
        }
        m_line->clear();
        break;
    }
    case core::KipFieldType::Select: {
        int idx = m_combo->findData(value.toString());
        m_combo->setCurrentIndex(idx >= 0 ? idx : 0);
        break;
    }
    case core::KipFieldType::List: {
        const QStringList wanted = stringList(value);
        if (m_field.multiple) {
            for (int i = 0; i < m_list->count(); ++i) {
                m_list->item(i)->setCheckState(
                    wanted.contains(m_list->item(i)->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
            }
        } else {
            m_list->clearSelection();
            for (int i = 0; i < m_list->count(); ++i) {
                if (!wanted.isEmpty() && m_list->item(i)->data(Qt::UserRole).toString() == wanted.first()) {
                    m_list->setCurrentRow(i);
                    break;
                }
            }
        }
        break;
    }
    case core::KipFieldType::Table:
        fields::selectRowKeys(m_table, stringList(value));
        break;
    case core::KipFieldType::Flags: {
        const QJsonObject o = value.toObject();
        for (int i = 0; i < m_field.flags.size() && i < m_flagBoxes.size(); ++i) {
            m_flagBoxes.at(i)->setChecked(o.value(m_field.flags.at(i).name).toBool(false));
        }
        break;
    }
    }
    m_applying = wasApplying;
}

bool KipFieldEditor::isFilled() const
{
    const QJsonValue v = value();
    switch (m_field.type) {
    case core::KipFieldType::Text:
    case core::KipFieldType::Secret:
    case core::KipFieldType::Textarea:
    case core::KipFieldType::Filepick:
    case core::KipFieldType::Folderpick:
        return !v.toString().trimmed().isEmpty();
    case core::KipFieldType::Number:
    case core::KipFieldType::Date:
        return !v.isNull();
    case core::KipFieldType::Select:
        return !v.toString().isEmpty();
    case core::KipFieldType::List:
    case core::KipFieldType::Table:
        return v.isArray() ? !v.toArray().isEmpty() : !v.toString().isEmpty();
    case core::KipFieldType::Flags: {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.value().toBool()) return true;
        }
        return false;
    }
    }
    return false;
}

void KipFieldEditor::setReadOnly(bool readOnly)
{
    if (m_widget) {
        m_widget->setEnabled(!readOnly);
    }
}

} // namespace kai::ui
