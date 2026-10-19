#include "ui/shared/diff-view.h"

#include "ui/features/docs/doc-highlighter.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {

constexpr int kMaxHighlightedLines = 20000;
// Cada arquivo é um cartão próprio (borda e cantos do token), com este respiro entre eles.
int fileGap() { return tk::space(2); }
constexpr int kTabWidth = 4;

QColor withAlpha(const QColor &c, int alpha)
{
    QColor out(c);
    out.setAlpha(alpha);
    return out;
}

// Base das cores de adição/remoção: o tema pode trocar (diff_add_bg / diff_del_bg); sem isso, success/error.
QColor addBase()
{
    const QString custom = tk::value(QStringLiteral("diff_add_bg"));
    return custom.isEmpty() ? QColor(tk::successFg()) : QColor(custom);
}

QColor delBase()
{
    const QString custom = tk::value(QStringLiteral("diff_del_bg"));
    return custom.isEmpty() ? QColor(tk::errorFg()) : QColor(custom);
}

QColor statusColor(core::DiffFileStatus status)
{
    switch (status) {
    case core::DiffFileStatus::Added: return QColor(tk::successFg());
    case core::DiffFileStatus::Deleted: return QColor(tk::errorFg());
    case core::DiffFileStatus::Renamed: return QColor(tk::infoFg());
    case core::DiffFileStatus::Modified: break;
    }
    return QColor(tk::warningFg());
}

int hunkAdditions(const core::DiffHunk &h)
{
    int n = 0;
    for (const core::DiffLine &l : h.lines) {
        if (l.kind == core::DiffLine::Kind::Added) ++n;
    }
    return n;
}

int hunkDeletions(const core::DiffHunk &h)
{
    int n = 0;
    for (const core::DiffLine &l : h.lines) {
        if (l.kind == core::DiffLine::Kind::Removed) ++n;
    }
    return n;
}

bool isChange(const core::DiffLine &l)
{
    return l.kind == core::DiffLine::Kind::Added || l.kind == core::DiffLine::Kind::Removed;
}

} // namespace

QString diffDisplayText(const QString &text, QVector<core::DiffSpan> *spans)
{
    if (!text.contains(QLatin1Char('\t'))) {
        return text;
    }
    QString out;
    QVector<int> map(text.size() + 1, 0); // índice cru -> índice exibido
    for (int i = 0; i < text.size(); ++i) {
        map[i] = out.size();
        if (text.at(i) == QLatin1Char('\t')) {
            out += QString(kTabWidth - (out.size() % kTabWidth), QLatin1Char(' '));
        } else {
            out += text.at(i);
        }
    }
    map[text.size()] = out.size();
    if (spans) {
        for (core::DiffSpan &s : *spans) {
            const int start = map.value(qBound(0, s.start, int(text.size())));
            const int end = map.value(qBound(0, s.start + s.length, int(text.size())));
            s.start = start;
            s.length = end - start;
        }
    }
    return out;
}

QVector<QVector<QTextLayout::FormatRange>> syntaxFormatsForLines(const QStringList &lines,
                                                                 texttools::Language language)
{
    QVector<QVector<QTextLayout::FormatRange>> out(lines.size());
    if (language == texttools::Language::Text || lines.isEmpty() || lines.size() > kMaxHighlightedLines) {
        return out;
    }
    QTextDocument doc;
    doc.setPlainText(lines.join(QLatin1Char('\n')));
    DocHighlighter highlighter(&doc, language);
    highlighter.rehighlight();
    int i = 0;
    for (QTextBlock block = doc.begin(); block.isValid() && i < out.size(); block = block.next(), ++i) {
        out[i] = block.layout()->formats();
    }
    return out;
}

texttools::Language languageFor(const QString &requested, const QString &fileName)
{
    const QString r = requested.trimmed().toLower();
    if (r == QLatin1String("json")) return texttools::Language::Json;
    if (r == QLatin1String("yaml") || r == QLatin1String("yml")) return texttools::Language::Yaml;
    if (r == QLatin1String("xml") || r == QLatin1String("html")) return texttools::Language::Xml;
    if (r == QLatin1String("markdown") || r == QLatin1String("md")) return texttools::Language::Markdown;
    if (!r.isEmpty()) return texttools::Language::Text;
    return texttools::languageForExtension(QFileInfo(fileName).suffix());
}

// ============================================================================================================

DiffView::DiffView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("kaiDiffView"));
    setProperty("kaiOwnsEnter", true); // Enter abre/fecha aqui, não envia o formulário KIP
    viewport()->setObjectName(QStringLiteral("kaiDiffViewport"));
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Sem moldura própria: os cartões dos arquivos (pintados) têm a borda e os cantos do token de raio.
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral("QAbstractScrollArea#kaiDiffView { background: transparent; border: none; }"
                                 "QWidget#kaiDiffViewport { background: transparent; }"));
    rebuildMetrics();
    rebuildRows();
}

void DiffView::setFiles(const QVector<core::DiffFile> &files, const QString &language)
{
    m_files = files;
    m_language = language;
    m_fileExpanded.clear();
    for (int i = 0; i < m_files.size(); ++i) {
        m_fileExpanded.append(m_files.size() == 1 || i < m_collapseAfter);
    }
    m_hunkExpanded.clear();
    m_foldOpen.clear();
    m_selected.clear();
    m_currentChange = -1;
    m_cursor = -1;
    m_hover = -1;
    computeFormats();
    rebuildMetrics();
    rebuildRows();
}

void DiffView::setMode(Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    rebuildRows();
}

void DiffView::setFileExpanded(int file, bool expanded)
{
    if (file < 0 || file >= m_fileExpanded.size() || m_fileExpanded.at(file) == expanded) return;
    m_fileExpanded[file] = expanded;
    rebuildRows();
}

bool DiffView::isFileExpanded(int file) const
{
    return file >= 0 && file < m_fileExpanded.size() && m_fileExpanded.at(file);
}

void DiffView::setSelectionMode(Selection selection)
{
    m_selection = selection;
    rebuildRows();
}

void DiffView::setInteractive(bool interactive)
{
    m_interactive = interactive;
    viewport()->update();
}

void DiffView::setMaxVisibleHeight(int px)
{
    m_maxVisibleHeight = qMax(80, px);
    updateScrollBars();
}

QString DiffView::fileId(int file) const
{
    return m_files.at(file).path();
}

QString DiffView::hunkId(int file, int hunk) const
{
    return m_files.at(file).hunkId(hunk);
}

QStringList DiffView::allIds() const
{
    QStringList ids;
    for (int f = 0; f < m_files.size(); ++f) {
        if (m_selection == Selection::Files) {
            ids << fileId(f);
        } else {
            for (int h = 0; h < m_files.at(f).hunks.size(); ++h) ids << hunkId(f, h);
        }
    }
    return ids;
}

QStringList DiffView::selectedIds() const
{
    QStringList out;
    for (const QString &id : allIds()) {
        if (m_selected.contains(id)) out << id;
    }
    return out;
}

void DiffView::setSelectedIds(const QStringList &ids)
{
    m_selected = QSet<QString>(ids.begin(), ids.end());
    viewport()->update();
}

int DiffView::selectedAdditions() const
{
    int n = 0;
    for (int f = 0; f < m_files.size(); ++f) {
        if (m_selection == Selection::Files) {
            if (m_selected.contains(fileId(f))) n += m_files.at(f).additions;
            continue;
        }
        for (int h = 0; h < m_files.at(f).hunks.size(); ++h) {
            if (m_selected.contains(hunkId(f, h))) n += hunkAdditions(m_files.at(f).hunks.at(h));
        }
    }
    return n;
}

int DiffView::selectedDeletions() const
{
    int n = 0;
    for (int f = 0; f < m_files.size(); ++f) {
        if (m_selection == Selection::Files) {
            if (m_selected.contains(fileId(f))) n += m_files.at(f).deletions;
            continue;
        }
        for (int h = 0; h < m_files.at(f).hunks.size(); ++h) {
            if (m_selected.contains(hunkId(f, h))) n += hunkDeletions(m_files.at(f).hunks.at(h));
        }
    }
    return n;
}

int DiffView::checkState(int file) const
{
    if (m_selection == Selection::Files) {
        return m_selected.contains(fileId(file)) ? 2 : 0;
    }
    const int total = m_files.at(file).hunks.size();
    int on = 0;
    for (int h = 0; h < total; ++h) {
        if (m_selected.contains(hunkId(file, h))) ++on;
    }
    return on == 0 ? 0 : on == total ? 2 : 1;
}

void DiffView::toggleFile(int file)
{
    if (!m_interactive) return;
    if (m_selection == Selection::Files) {
        const QString id = fileId(file);
        if (!m_selected.remove(id)) m_selected.insert(id);
    } else {
        const bool all = checkState(file) == 2;
        for (int h = 0; h < m_files.at(file).hunks.size(); ++h) {
            if (all) m_selected.remove(hunkId(file, h));
            else m_selected.insert(hunkId(file, h));
        }
    }
    viewport()->update();
    emit selectionChanged();
}

void DiffView::toggleHunk(int file, int hunk)
{
    if (!m_interactive) return;
    const QString id = hunkId(file, hunk);
    if (!m_selected.remove(id)) m_selected.insert(id);
    viewport()->update();
    emit selectionChanged();
}

bool DiffView::hunkExpanded(int file, int hunk) const
{
    return m_selection != Selection::Hunks || m_hunkExpanded.contains(key(file, hunk, 0));
}

// ---- métricas / linhas visuais ----------------------------------------------------------------------------

void DiffView::rebuildMetrics()
{
    m_mono = tk::monoFont(qMax(8, tk::fontSizePt() - 1));
    const QFontMetrics fm(m_mono);
    m_charWidth = qMax(1, fm.horizontalAdvance(QLatin1Char('0')));
    m_lineHeight = fm.height() + 4;
    m_headerHeight = qMax(QFontMetrics(font()).height() + 16, 32);
    int maxNo = 99;
    m_fileTextWidth.clear();
    int widest = 0;
    const auto measure = [&](const core::DiffLine &l) {
        maxNo = qMax(maxNo, qMax(l.oldNo, l.newNo));
        widest = qMax(widest, fm.horizontalAdvance(diffDisplayText(l.text)));
    };
    for (const core::DiffFile &f : m_files) {
        widest = 0;
        for (const core::DiffHunk &h : f.hunks) {
            for (const core::DiffLine &l : h.lines) {
                if (l.kind == core::DiffLine::Kind::Fold) {
                    for (const core::DiffLine &inner : l.folded) measure(inner);
                } else {
                    measure(l);
                }
            }
        }
        m_fileTextWidth.append(widest);
    }
    m_numberWidth = fm.horizontalAdvance(QString::number(maxNo)) + 18;
    m_signWidth = m_charWidth + 12;
}

void DiffView::computeFormats()
{
    m_formats.clear();
    m_foldFormats.clear();
    for (int f = 0; f < m_files.size(); ++f) {
        const core::DiffFile &file = m_files.at(f);
        const texttools::Language language = languageFor(m_language, file.path());
        if (language == texttools::Language::Text) continue;
        QStringList texts;
        for (const core::DiffHunk &h : file.hunks) {
            for (const core::DiffLine &l : h.lines) {
                if (l.kind == core::DiffLine::Kind::Fold) {
                    for (const core::DiffLine &inner : l.folded) texts << diffDisplayText(inner.text);
                } else {
                    texts << diffDisplayText(l.text);
                }
            }
        }
        const auto formats = syntaxFormatsForLines(texts, language);
        int i = 0;
        for (int h = 0; h < file.hunks.size(); ++h) {
            const auto &lines = file.hunks.at(h).lines;
            for (int l = 0; l < lines.size(); ++l) {
                if (lines.at(l).kind == core::DiffLine::Kind::Fold) {
                    QVector<QVector<QTextLayout::FormatRange>> inner;
                    for (int k = 0; k < lines.at(l).folded.size(); ++k) inner.append(formats.value(i++));
                    m_foldFormats.insert(key(f, h, l), inner);
                } else {
                    m_formats.insert(key(f, h, l), formats.value(i++));
                }
            }
        }
    }
}

void DiffView::rebuildRows()
{
    m_rows.clear();
    m_changes.clear();
    m_fileSpans.clear();
    m_textWidth = 0;
    int y = 0;
    const auto add = [&](Row row, int height) {
        row.y = y;
        row.height = height;
        y += height;
        m_rows.append(row);
    };
    for (int f = 0; f < m_files.size(); ++f) {
        const core::DiffFile &file = m_files.at(f);
        if (f > 0) y += fileGap();
        m_fileSpans.append({y, y});
        Row header;
        header.kind = Row::Kind::FileHeader;
        header.file = f;
        add(header, m_headerHeight);
        const auto closeCard = qScopeGuard([&]() { m_fileSpans[f].second = y; });
        if (!isFileExpanded(f)) continue;
        // a rolagem horizontal só conta as linhas que estão à vista
        if (!file.binary && !file.hunks.isEmpty()) m_textWidth = qMax(m_textWidth, m_fileTextWidth.value(f));
        if (file.binary || file.hunks.isEmpty()) {
            Row note;
            note.kind = Row::Kind::Note;
            note.file = f;
            note.note = utils::tr(file.binary ? QStringLiteral("diff.binary") : QStringLiteral("diff.no_content_changes"));
            add(note, m_lineHeight + 8);
            continue;
        }
        if (file.wholeFile && file.additions == 0 && file.deletions == 0) {
            Row note;
            note.kind = Row::Kind::Note;
            note.file = f;
            note.note = utils::tr(QStringLiteral("diff.identical"));
            add(note, m_lineHeight + 8);
        }
        for (int h = 0; h < file.hunks.size(); ++h) {
            const core::DiffHunk &hunk = file.hunks.at(h);
            if (!file.wholeFile) {
                Row hh;
                hh.kind = Row::Kind::HunkHeader;
                hh.file = f;
                hh.hunk = h;
                add(hh, m_selection == Selection::Hunks ? m_lineHeight + 10 : m_lineHeight + 4);
            }
            if (!hunkExpanded(f, h)) continue;
            const int n = hunk.lines.size();
            int change = -1;
            bool inChange = false;
            const auto noteChange = [&](int lineIndex) {
                if (!inChange) {
                    m_changes.append({f, h, lineIndex});
                    change = m_changes.size() - 1;
                    inChange = true;
                }
                return change;
            };
            int i = 0;
            while (i < n) {
                const core::DiffLine &line = hunk.lines.at(i);
                if (line.kind == core::DiffLine::Kind::Fold) {
                    inChange = false;
                    if (m_foldOpen.contains(key(f, h, i))) {
                        for (int k = 0; k < line.folded.size(); ++k) {
                            Row r;
                            r.kind = m_mode == Mode::Split ? Row::Kind::Split : Row::Kind::Line;
                            r.file = f;
                            r.hunk = h;
                            r.line = i;
                            r.right = i;
                            r.fold = k;
                            r.rightFold = k;
                            add(r, m_lineHeight);
                        }
                    } else {
                        Row r;
                        r.kind = Row::Kind::Fold;
                        r.file = f;
                        r.hunk = h;
                        r.line = i;
                        add(r, m_lineHeight + 6);
                    }
                    ++i;
                    continue;
                }
                if (m_mode == Mode::Unified || line.kind == core::DiffLine::Kind::Context) {
                    Row r;
                    r.kind = m_mode == Mode::Split ? Row::Kind::Split : Row::Kind::Line;
                    r.file = f;
                    r.hunk = h;
                    r.line = i;
                    r.right = line.kind == core::DiffLine::Kind::Context ? i : -1;
                    if (isChange(line)) {
                        r.change = noteChange(i);
                    } else {
                        inChange = false;
                    }
                    add(r, m_lineHeight);
                    ++i;
                    continue;
                }
                // lado a lado: um bloco de removidas e o de adicionadas logo depois, emparelhados
                const int delStart = i;
                while (i < n && hunk.lines.at(i).kind == core::DiffLine::Kind::Removed) ++i;
                const int addStart = i;
                while (i < n && hunk.lines.at(i).kind == core::DiffLine::Kind::Added) ++i;
                const int dels = addStart - delStart;
                const int adds = i - addStart;
                const int c = noteChange(delStart);
                for (int k = 0; k < qMax(dels, adds); ++k) {
                    Row r;
                    r.kind = Row::Kind::Split;
                    r.file = f;
                    r.hunk = h;
                    r.line = k < dels ? delStart + k : -1;
                    r.right = k < adds ? addStart + k : -1;
                    r.change = c;
                    add(r, m_lineHeight);
                }
                inChange = false;
            }
        }
    }
    m_contentHeight = y;
    if (m_currentChange >= m_changes.size()) m_currentChange = -1;
    if (m_cursor >= m_rows.size()) m_cursor = m_rows.size() - 1;
    updateScrollBars();
    viewport()->update();
}

void DiffView::updateScrollBars()
{
    const int frame = 0;
    const int vh = qMin(m_contentHeight, m_maxVisibleHeight);
    const int gutter = m_mode == Mode::Unified ? 2 * m_numberWidth + m_signWidth : m_numberWidth + m_signWidth;
    const int areaWidth = qMax(100, viewport()->width());
    const int textAvail = (m_mode == Mode::Unified ? areaWidth : areaWidth / 2) - gutter;
    const int hMax = qMax(0, m_textWidth + 24 - textAvail);
    horizontalScrollBar()->setRange(0, hMax);
    horizontalScrollBar()->setPageStep(qMax(1, textAvail));
    horizontalScrollBar()->setSingleStep(m_charWidth * 4);
    const int hbar = hMax > 0 ? horizontalScrollBar()->sizeHint().height() : 0;
    const int wanted = vh + frame + hbar;
    if (height() != wanted) {
        setFixedHeight(wanted);
    }
    const int visible = qMax(1, wanted - frame - hbar);
    verticalScrollBar()->setRange(0, qMax(0, m_contentHeight - visible));
    verticalScrollBar()->setPageStep(visible);
    verticalScrollBar()->setSingleStep(m_lineHeight);
}

QSize DiffView::sizeHint() const
{
    return QSize(480, height());
}

QSize DiffView::minimumSizeHint() const
{
    return QSize(160, height());
}

int DiffView::rowAt(int y) const
{
    const int absolute = y + verticalScrollBar()->value();
    int lo = 0;
    int hi = m_rows.size() - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const Row &r = m_rows.at(mid);
        if (absolute < r.y) hi = mid - 1;
        else if (absolute >= r.y + r.height) lo = mid + 1;
        else return mid;
    }
    return -1;
}

QRect DiffView::rowRect(const Row &row) const
{
    return QRect(0, row.y - verticalScrollBar()->value(), viewport()->width(), row.height);
}

void DiffView::ensureRowVisible(int index)
{
    if (index < 0 || index >= m_rows.size()) return;
    const Row &r = m_rows.at(index);
    QScrollBar *v = verticalScrollBar();
    const int visible = viewport()->height();
    if (r.y < v->value()) v->setValue(r.y);
    else if (r.y + r.height > v->value() + visible) v->setValue(r.y + r.height - visible);
}

void DiffView::goToChange(int index)
{
    if (m_changes.isEmpty()) return;
    index = qBound(0, index, int(m_changes.size()) - 1);
    m_currentChange = index;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).change == index) {
            // um pouco de contexto acima da mudança
            const int top = qMax(0, m_rows.at(i).y - 2 * m_lineHeight);
            verticalScrollBar()->setValue(top);
            m_cursor = i;
            break;
        }
    }
    viewport()->update();
    emit currentChangeChanged(m_currentChange);
}

void DiffView::nextChange()
{
    goToChange(m_currentChange < 0 ? 0 : (m_currentChange + 1) % qMax(1, int(m_changes.size())));
}

void DiffView::previousChange()
{
    const int n = m_changes.size();
    goToChange(m_currentChange <= 0 ? n - 1 : m_currentChange - 1);
}

const core::DiffLine *DiffView::lineFor(const Row &row, bool right) const
{
    const int index = right ? row.right : row.line;
    if (row.file < 0 || row.hunk < 0 || index < 0) return nullptr;
    const core::DiffLine &line = m_files.at(row.file).hunks.at(row.hunk).lines.at(index);
    const int fold = right ? row.rightFold : row.fold;
    if (fold >= 0) return &line.folded.at(fold);
    return &line;
}

const QVector<QTextLayout::FormatRange> *DiffView::formatsFor(const Row &row, bool right) const
{
    const int index = right ? row.right : row.line;
    if (index < 0) return nullptr;
    const quint64 k = key(row.file, row.hunk, index);
    const int fold = right ? row.rightFold : row.fold;
    if (fold >= 0) {
        auto it = m_foldFormats.constFind(k);
        return it != m_foldFormats.constEnd() && fold < it->size() ? &it->at(fold) : nullptr;
    }
    auto it = m_formats.constFind(k);
    return it != m_formats.constEnd() ? &*it : nullptr;
}

QString DiffView::rowCopyText(const Row &row) const
{
    if (row.kind == Row::Kind::Line || row.kind == Row::Kind::Split) {
        const core::DiffLine *line = lineFor(row, row.right >= 0 && row.kind == Row::Kind::Split);
        if (!line) line = lineFor(row, false);
        return line ? line->text : QString();
    }
    if (row.kind == Row::Kind::HunkHeader) {
        const core::DiffHunk &h = m_files.at(row.file).hunks.at(row.hunk);
        return h.header() + (h.section.isEmpty() ? QString() : QLatin1Char(' ') + h.section);
    }
    return row.file >= 0 ? fileId(row.file) : QString();
}

// ---- pintura ------------------------------------------------------------------------------------------------

QPainterPath DiffView::cardPath(int file) const
{
    QPainterPath path;
    if (file < 0 || file >= m_fileSpans.size()) return path;
    const int top = verticalScrollBar()->value();
    const QRectF card(0.5, m_fileSpans.at(file).first - top + 0.5, viewport()->width() - 1.0,
                      m_fileSpans.at(file).second - m_fileSpans.at(file).first - 1.0);
    const qreal radius = qMin<qreal>(tk::radiusMd(), card.height() / 2.0);
    path.addRoundedRect(card, radius, radius);
    return path;
}

void DiffView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const QRect area = viewport()->rect();
    const int top = verticalScrollBar()->value();
    if (m_rows.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath empty;
        empty.addRoundedRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5), tk::radiusMd(), tk::radiusMd());
        p.fillPath(empty, QColor(tk::codeBg()));
        p.setPen(QColor(tk::borderColor()));
        p.drawPath(empty);
        p.setPen(QColor(tk::mutedFg()));
        p.drawText(area, Qt::AlignCenter, utils::tr(QStringLiteral("diff.empty")));
        return;
    }
    // Fundo dos cartões visíveis; as linhas são pintadas recortadas no cartão do arquivo delas (cantos redondos).
    QHash<int, QPainterPath> cards;
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int f = 0; f < m_fileSpans.size(); ++f) {
        const int y0 = m_fileSpans.at(f).first - top;
        const int y1 = m_fileSpans.at(f).second - top;
        if (y1 < 0 || y0 > area.height()) continue;
        cards.insert(f, cardPath(f));
        p.fillPath(cards.value(f), QColor(tk::codeBg()));
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    // primeira linha visível (o topo pode cair no respiro entre dois cartões)
    int first = 0;
    for (int lo = 0, hi = m_rows.size() - 1; lo <= hi;) {
        const int mid = (lo + hi) / 2;
        if (m_rows.at(mid).y + m_rows.at(mid).height <= top) {
            lo = mid + 1;
            first = lo;
        } else {
            hi = mid - 1;
        }
    }
    for (int i = first; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        if (row.y - top > area.height()) break;
        const QRect r = rowRect(row);
        const bool hovered = i == m_hover;
        p.save();
        p.setClipPath(cards.value(row.file));
        switch (row.kind) {
        case Row::Kind::FileHeader: paintFileHeader(p, row, r, hovered); break;
        case Row::Kind::HunkHeader: paintHunkHeader(p, row, r, hovered); break;
        case Row::Kind::Line: paintUnifiedLine(p, row, r); break;
        case Row::Kind::Split: paintSplitLine(p, row, r); break;
        case Row::Kind::Fold: paintFold(p, row, r, hovered); break;
        case Row::Kind::Note: paintNote(p, row, r); break;
        }
        if (row.change >= 0 && row.change == m_currentChange) {
            p.fillRect(QRect(r.left(), r.top(), 3, r.height()), QColor(tk::accent()));
        }
        if (i == m_cursor && hasFocus() && m_keyboardCursor) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(withAlpha(QColor(tk::accent()), 150), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r).adjusted(2.5, 1.5, -2.5, -1.5), tk::radiusSm(), tk::radiusSm());
        }
        p.restore();
    }
    // A borda por cima de tudo: os fundos das linhas não a cobrem.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(tk::borderColor()), 1));
    p.setBrush(Qt::NoBrush);
    for (auto it = cards.constBegin(); it != cards.constEnd(); ++it) {
        p.drawPath(it.value());
    }
}

QRect DiffView::checkRect(const QRect &rowRect, int indent) const
{
    const int side = 16;
    return QRect(rowRect.left() + indent, rowRect.center().y() - side / 2 + 1, side, side);
}

void DiffView::paintCheck(QPainter &p, const QRect &box, int state) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal radius = qMin(4, tk::radiusSm());
    const QColor accent(tk::accent());
    const bool on = state > 0;
    QColor border = on ? accent : QColor(tk::mutedFg());
    if (!m_interactive) border = withAlpha(border, 120);
    p.setPen(QPen(border, 1.5));
    p.setBrush(on ? (m_interactive ? accent : withAlpha(accent, 120)) : QColor(tk::surface()));
    p.drawRoundedRect(QRectF(box).adjusted(0.75, 0.75, -0.75, -0.75), radius, radius);
    const QColor mark = QColor(tk::buttonFg());
    QPen pen(mark, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const QPointF c = QRectF(box).center();
    if (state == 2) {
        QPainterPath check;
        check.moveTo(c.x() - 4, c.y() + 0.5);
        check.lineTo(c.x() - 1, c.y() + 3.5);
        check.lineTo(c.x() + 4.5, c.y() - 3);
        p.drawPath(check);
    } else if (state == 1) {
        p.drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
    }
    p.restore();
}

void DiffView::paintFileHeader(QPainter &p, const Row &row, const QRect &r, bool hovered)
{
    const core::DiffFile &file = m_files.at(row.file);
    p.fillRect(r, QColor(hovered ? tk::hoverBg() : tk::altBg()));
    p.setPen(QColor(tk::borderColor()));
    if (isFileExpanded(row.file)) p.drawLine(r.bottomLeft(), r.bottomRight());

    int x = r.left() + 8;
    const QString chevron = isFileExpanded(row.file) ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right");
    LucideIcons::icon(chevron, QColor(tk::mutedFg()), 16).paint(&p, QRect(x, r.center().y() - 8, 16, 16));
    x += 22;
    if (m_selection != Selection::None) {
        paintCheck(p, checkRect(r, x - r.left()), checkState(row.file));
        x += 24;
    }

    // lado direito: +N −N e a barrinha de 5 quadrados
    const int squares = 5;
    const int sq = 7;
    const int barWidth = squares * sq + (squares - 1) * 2;
    int right = r.right() - 10;
    if (!file.binary) {
        const int total = file.additions + file.deletions;
        const int na = total ? qRound(double(squares) * file.additions / total) : 0;
        const int nd = total ? qMin(squares - na, qRound(double(squares) * file.deletions / total)) : 0;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < squares; ++i) {
            const QColor c = i < na ? addBase() : i < na + nd ? delBase() : QColor(tk::borderColor());
            p.setBrush(c);
            p.drawRoundedRect(QRectF(right - barWidth + i * (sq + 2), r.center().y() - sq / 2.0, sq, sq), 1.5, 1.5);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
        right -= barWidth + 10;
        p.setFont(m_mono);
        const QFontMetrics fm(m_mono);
        const QString dels = file.deletions ? QStringLiteral("−%1").arg(file.deletions) : QString();
        const QString adds = QStringLiteral("+%1").arg(file.additions);
        if (!dels.isEmpty()) {
            const int w = fm.horizontalAdvance(dels);
            p.setPen(delBase());
            p.drawText(QRect(right - w, r.top(), w, r.height()), Qt::AlignVCenter, dels);
            right -= w + 6;
        }
        const int w = fm.horizontalAdvance(adds);
        p.setPen(addBase());
        p.drawText(QRect(right - w, r.top(), w, r.height()), Qt::AlignVCenter, adds);
        right -= w + 12;
    }

    if (file.wholeFile) {
        LucideIcons::icon(QStringLiteral("file"), QColor(tk::mutedFg()), 15).paint(&p, QRect(x, r.center().y() - 8, 16, 16));
        x += 22;
        p.setFont(m_mono);
        const QFontMetrics fm(m_mono);
        const QString left = file.oldPath.isEmpty() ? utils::tr(QStringLiteral("diff.left")) : file.oldPath;
        const QString rightName = file.newPath.isEmpty() ? utils::tr(QStringLiteral("diff.right")) : file.newPath;
        const int avail = qMax(40, right - x);
        const QString arrow = QStringLiteral("  ↔  ");
        const int arrowW = fm.horizontalAdvance(arrow);
        const int half = (avail - arrowW) / 2;
        const QString l = fm.elidedText(left, Qt::ElideMiddle, half);
        const QString rn = fm.elidedText(rightName, Qt::ElideMiddle, half);
        p.setPen(QColor(tk::fg()));
        p.drawText(QRect(x, r.top(), half, r.height()), Qt::AlignVCenter, l);
        const int lw = fm.horizontalAdvance(l);
        p.setPen(QColor(tk::mutedFg()));
        p.drawText(QRect(x + lw, r.top(), arrowW, r.height()), Qt::AlignVCenter, arrow);
        p.setPen(QColor(tk::fg()));
        p.drawText(QRect(x + lw + arrowW, r.top(), half, r.height()), Qt::AlignVCenter, rn);
        return;
    }

    // pílula de status
    const QColor sc = statusColor(file.status);
    QFont pillFont = m_mono;
    pillFont.setBold(true);
    pillFont.setPointSize(qMax(7, m_mono.pointSize() - 1));
    const QRect pill(x, r.center().y() - 9, 20, 18);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(sc, 46));
    p.drawRoundedRect(pill, tk::radiusSm(), tk::radiusSm());
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(pillFont);
    p.setPen(sc);
    p.drawText(pill, Qt::AlignCenter, core::diffFileStatusLetter(file.status));
    x += 28;

    // caminho: pasta apagada, nome forte
    p.setFont(m_mono);
    const QFontMetrics fm(m_mono);
    QString path = file.path();
    if (file.status == core::DiffFileStatus::Renamed && !file.oldPath.isEmpty() && file.oldPath != file.newPath) {
        path = file.oldPath + QStringLiteral(" → ") + file.newPath;
    }
    const int avail = qMax(40, right - x);
    const QString elided = fm.elidedText(path, Qt::ElideMiddle, avail);
    const int slash = elided.lastIndexOf(QLatin1Char('/'));
    const QString dir = slash >= 0 && elided == path ? elided.left(slash + 1) : QString();
    const QString name = dir.isEmpty() ? elided : elided.mid(slash + 1);
    p.setPen(QColor(tk::mutedFg()));
    p.drawText(QRect(x, r.top(), avail, r.height()), Qt::AlignVCenter, dir);
    p.setPen(QColor(tk::fg()));
    p.drawText(QRect(x + fm.horizontalAdvance(dir), r.top(), avail, r.height()), Qt::AlignVCenter, name);
}

void DiffView::paintHunkHeader(QPainter &p, const Row &row, const QRect &r, bool hovered)
{
    const core::DiffHunk &hunk = m_files.at(row.file).hunks.at(row.hunk);
    const QColor info(tk::infoFg());
    const bool selecting = m_selection == Selection::Hunks;
    p.fillRect(r, selecting ? QColor(hovered ? tk::hoverBg() : tk::surface2()) : withAlpha(info, 20));
    p.setFont(m_mono);
    const QFontMetrics fm(m_mono);
    int x;
    if (selecting) {
        p.setPen(QColor(tk::borderColor()));
        p.drawLine(r.bottomLeft(), r.bottomRight());
        x = r.left() + 30;
        const QString chevron = hunkExpanded(row.file, row.hunk) ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right");
        LucideIcons::icon(chevron, QColor(tk::mutedFg()), 14).paint(&p, QRect(x, r.center().y() - 7, 14, 14));
        x += 20;
        paintCheck(p, checkRect(r, x - r.left()), m_selected.contains(hunkId(row.file, row.hunk)) ? 2 : 0);
        x += 24;
        // contadores à direita
        int right = r.right() - 10;
        const int dels = hunkDeletions(hunk);
        const int adds = hunkAdditions(hunk);
        if (dels) {
            const QString t = QStringLiteral("−%1").arg(dels);
            const int w = fm.horizontalAdvance(t);
            p.setPen(delBase());
            p.drawText(QRect(right - w, r.top(), w, r.height()), Qt::AlignVCenter, t);
            right -= w + 6;
        }
        const QString t = QStringLiteral("+%1").arg(adds);
        const int w = fm.horizontalAdvance(t);
        p.setPen(addBase());
        p.drawText(QRect(right - w, r.top(), w, r.height()), Qt::AlignVCenter, t);
    } else {
        x = r.left() + (m_mode == Mode::Unified ? 2 * m_numberWidth + m_signWidth : m_numberWidth + m_signWidth);
    }
    const QString head = hunk.header();
    p.setPen(info);
    p.drawText(QRect(x, r.top(), r.width(), r.height()), Qt::AlignVCenter, head);
    if (!hunk.section.isEmpty()) {
        p.setPen(QColor(tk::mutedFg()));
        p.drawText(QRect(x + fm.horizontalAdvance(head + QLatin1Char(' ')), r.top(), r.width(), r.height()),
                   Qt::AlignVCenter, hunk.section);
    }
}

void DiffView::paintCode(QPainter &p, const core::DiffLine &line, const QVector<QTextLayout::FormatRange> *formats,
                         const QRect &textRect, const QColor &wordColor)
{
    QVector<core::DiffSpan> spans = line.spans;
    const QString text = diffDisplayText(line.text, &spans);
    if (text.isEmpty()) return;
    QTextLayout layout(text, m_mono);
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(option);
    QVector<QTextLayout::FormatRange> all;
    if (formats) all = *formats;
    for (const core::DiffSpan &s : spans) {
        QTextLayout::FormatRange range;
        range.start = s.start;
        range.length = s.length;
        range.format.setBackground(wordColor);
        all.append(range);
    }
    layout.setFormats(all);
    layout.beginLayout();
    QTextLine tl = layout.createLine();
    if (tl.isValid()) tl.setLineWidth(1e7);
    layout.endLayout();
    p.save();
    p.setClipRect(textRect);
    p.setPen(QColor(tk::codeFg()));
    const qreal y = textRect.top() + (textRect.height() - (tl.isValid() ? tl.height() : 0)) / 2.0;
    layout.draw(&p, QPointF(textRect.left() + 4 - horizontalScrollBar()->value(), y));
    p.restore();
}

void DiffView::paintUnifiedLine(QPainter &p, const Row &row, const QRect &r)
{
    const core::DiffLine *line = lineFor(row, false);
    if (!line) return;
    QColor bg, gutter, word;
    QString sign;
    QColor signColor(tk::mutedFg());
    if (line->kind == core::DiffLine::Kind::Added) {
        bg = withAlpha(addBase(), 28);
        gutter = withAlpha(addBase(), 46);
        word = withAlpha(addBase(), 78);
        sign = QStringLiteral("+");
        signColor = addBase();
    } else if (line->kind == core::DiffLine::Kind::Removed) {
        bg = withAlpha(delBase(), 26);
        gutter = withAlpha(delBase(), 43);
        word = withAlpha(delBase(), 72);
        sign = QStringLiteral("−");
        signColor = delBase();
    }
    if (bg.isValid()) {
        p.fillRect(r, bg);
        p.fillRect(QRect(r.left(), r.top(), 2 * m_numberWidth, r.height()), gutter);
    }
    p.setFont(m_mono);
    p.setPen(withAlpha(QColor(tk::mutedFg()), 210));
    if (line->oldNo > 0) {
        p.drawText(QRect(r.left(), r.top(), m_numberWidth - 8, r.height()), Qt::AlignVCenter | Qt::AlignRight,
                   QString::number(line->oldNo));
    }
    if (line->newNo > 0) {
        p.drawText(QRect(r.left() + m_numberWidth, r.top(), m_numberWidth - 8, r.height()),
                   Qt::AlignVCenter | Qt::AlignRight, QString::number(line->newNo));
    }
    p.setPen(signColor);
    p.drawText(QRect(r.left() + 2 * m_numberWidth, r.top(), m_signWidth, r.height()), Qt::AlignCenter, sign);
    const int textX = r.left() + 2 * m_numberWidth + m_signWidth;
    paintCode(p, *line, formatsFor(row, false), QRect(textX, r.top(), r.right() - textX, r.height()), word);
}

void DiffView::paintSplitLine(QPainter &p, const Row &row, const QRect &r)
{
    const int half = r.width() / 2;
    for (int side = 0; side < 2; ++side) {
        const bool right = side == 1;
        const QRect hr(r.left() + (right ? half : 0), r.top(), right ? r.width() - half : half, r.height());
        const core::DiffLine *line = lineFor(row, right);
        if (!line) {
            p.fillRect(hr, QBrush(withAlpha(QColor(tk::borderColor()), 200), Qt::BDiagPattern));
            continue;
        }
        QColor bg, gutter, word;
        const bool added = line->kind == core::DiffLine::Kind::Added;
        const bool removed = line->kind == core::DiffLine::Kind::Removed;
        if (added) {
            bg = withAlpha(addBase(), 28);
            gutter = withAlpha(addBase(), 46);
            word = withAlpha(addBase(), 78);
        } else if (removed) {
            bg = withAlpha(delBase(), 26);
            gutter = withAlpha(delBase(), 43);
            word = withAlpha(delBase(), 72);
        }
        if (bg.isValid()) {
            p.fillRect(hr, bg);
            p.fillRect(QRect(hr.left(), hr.top(), m_numberWidth, hr.height()), gutter);
        }
        const int number = right ? line->newNo : line->oldNo;
        p.setFont(m_mono);
        p.setPen(withAlpha(QColor(tk::mutedFg()), 210));
        if (number > 0) {
            p.drawText(QRect(hr.left(), hr.top(), m_numberWidth - 8, hr.height()), Qt::AlignVCenter | Qt::AlignRight,
                       QString::number(number));
        }
        if (added || removed) {
            p.setPen(added ? addBase() : delBase());
            p.drawText(QRect(hr.left() + m_numberWidth, hr.top(), m_signWidth, hr.height()), Qt::AlignCenter,
                       added ? QStringLiteral("+") : QStringLiteral("−"));
        }
        const int textX = hr.left() + m_numberWidth + m_signWidth;
        paintCode(p, *line, formatsFor(row, right), QRect(textX, hr.top(), hr.right() - textX, hr.height()), word);
    }
    p.setPen(QColor(tk::borderColor()));
    p.drawLine(QPoint(r.left() + half, r.top()), QPoint(r.left() + half, r.bottom()));
}

void DiffView::paintFold(QPainter &p, const Row &row, const QRect &r, bool hovered)
{
    const core::DiffLine &line = m_files.at(row.file).hunks.at(row.hunk).lines.at(row.line);
    p.fillRect(r, QColor(hovered ? tk::hoverBg() : tk::surface2()));
    QPen dashed(QColor(tk::borderColor()));
    dashed.setStyle(Qt::DashLine);
    p.setPen(dashed);
    p.drawLine(r.topLeft(), r.topRight());
    p.drawLine(r.bottomLeft(), r.bottomRight());
    const int x = r.left() + (m_mode == Mode::Unified ? 2 * m_numberWidth : m_numberWidth) + 4;
    const QColor muted(hovered ? tk::fg() : tk::mutedFg());
    LucideIcons::icon(QStringLiteral("unfold-vertical"), muted, 14).paint(&p, QRect(x, r.center().y() - 7, 14, 14));
    p.setFont(font());
    p.setPen(muted);
    p.drawText(QRect(x + 20, r.top(), r.width(), r.height()), Qt::AlignVCenter,
               utils::tr(QStringLiteral("diff.fold")).arg(line.folded.size()));
}

void DiffView::paintNote(QPainter &p, const Row &row, const QRect &r)
{
    p.setFont(font());
    p.setPen(QColor(tk::mutedFg()));
    p.drawText(r.adjusted(16, 0, -16, 0), Qt::AlignVCenter, row.note);
}

// ---- interação ----------------------------------------------------------------------------------------------

void DiffView::activateRow(int index, const QPoint &pos, bool viaKeyboard)
{
    if (index < 0 || index >= m_rows.size()) return;
    const Row row = m_rows.at(index);
    const QRect r = rowRect(row);
    switch (row.kind) {
    case Row::Kind::FileHeader: {
        if (m_selection != Selection::None) {
            const int indent = 30;
            const QRect box = checkRect(r, indent).adjusted(-4, -4, 4, 4);
            if (!viaKeyboard && box.contains(pos)) {
                toggleFile(row.file);
                return;
            }
        }
        setFileExpanded(row.file, !isFileExpanded(row.file));
        return;
    }
    case Row::Kind::HunkHeader: {
        if (m_selection != Selection::Hunks) return;
        const QRect box = checkRect(r, 50).adjusted(-4, -4, 4, 4);
        if (!viaKeyboard && box.contains(pos)) {
            toggleHunk(row.file, row.hunk);
            return;
        }
        const quint64 k = key(row.file, row.hunk, 0);
        if (!m_hunkExpanded.remove(k)) m_hunkExpanded.insert(k);
        rebuildRows();
        return;
    }
    case Row::Kind::Fold:
        m_foldOpen.insert(key(row.file, row.hunk, row.line));
        rebuildRows();
        return;
    default:
        viewport()->update();
        return;
    }
}

void DiffView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    const int index = rowAt(event->position().toPoint().y());
    m_cursor = index;
    m_keyboardCursor = false;
    activateRow(index, event->position().toPoint(), false);
    viewport()->update();
}

void DiffView::mouseMoveEvent(QMouseEvent *event)
{
    const int index = rowAt(event->position().toPoint().y());
    if (index != m_hover) {
        m_hover = index;
        const bool clickable = index >= 0
            && (m_rows.at(index).kind == Row::Kind::FileHeader || m_rows.at(index).kind == Row::Kind::Fold
                || (m_rows.at(index).kind == Row::Kind::HunkHeader && m_selection == Selection::Hunks));
        viewport()->setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void DiffView::leaveEvent(QEvent *event)
{
    m_hover = -1;
    viewport()->update();
    QAbstractScrollArea::leaveEvent(event);
}

void DiffView::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (event->matches(QKeySequence::Copy)) {
        if (m_cursor >= 0 && m_cursor < m_rows.size()) {
            QApplication::clipboard()->setText(rowCopyText(m_rows.at(m_cursor)));
        }
        return;
    }
    if (key == Qt::Key_F7 || key == Qt::Key_N || key == Qt::Key_P) {
        if (key == Qt::Key_P || (key == Qt::Key_F7 && (event->modifiers() & Qt::ShiftModifier))) previousChange();
        else nextChange();
        return;
    }
    if (m_rows.isEmpty()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    m_keyboardCursor = true;
    switch (key) {
    case Qt::Key_Down:
    case Qt::Key_Up: {
        m_cursor = qBound(0, m_cursor + (key == Qt::Key_Down ? 1 : -1), int(m_rows.size()) - 1);
        ensureRowVisible(m_cursor);
        viewport()->update();
        return;
    }
    case Qt::Key_Space:
        if (m_cursor >= 0) {
            const Row &row = m_rows.at(m_cursor);
            if (row.kind == Row::Kind::FileHeader && m_selection != Selection::None) toggleFile(row.file);
            else if (row.kind == Row::Kind::HunkHeader && m_selection == Selection::Hunks) toggleHunk(row.file, row.hunk);
            else activateRow(m_cursor, QPoint(), true);
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (m_cursor >= 0) {
            const Row &row = m_rows.at(m_cursor);
            const bool collapse = key == Qt::Key_Left;
            const bool expand = key == Qt::Key_Right;
            if (row.kind == Row::Kind::FileHeader && (collapse || expand)) {
                setFileExpanded(row.file, expand);
            } else if (!collapse && !expand) {
                activateRow(m_cursor, QPoint(), true);
            } else {
                QAbstractScrollArea::keyPressEvent(event); // rolagem horizontal
            }
        }
        return;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void DiffView::contextMenuEvent(QContextMenuEvent *event)
{
    const int index = rowAt(event->pos().y());
    if (index < 0) return;
    m_cursor = index;
    const Row row = m_rows.at(index);
    QMenu menu(this);
    QAction *copyLine = nullptr;
    if (row.kind == Row::Kind::Line || row.kind == Row::Kind::Split || row.kind == Row::Kind::HunkHeader) {
        copyLine = menu.addAction(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::mutedFg()), 16),
                                  utils::tr(QStringLiteral("diff.copy_line")));
    }
    QAction *copyPath = nullptr;
    if (row.file >= 0 && !m_files.at(row.file).wholeFile) {
        copyPath = menu.addAction(LucideIcons::icon(QStringLiteral("file"), QColor(tk::mutedFg()), 16),
                                  utils::tr(QStringLiteral("diff.copy_path")));
    }
    if (menu.isEmpty()) return;
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen && chosen == copyLine) QApplication::clipboard()->setText(rowCopyText(row));
    else if (chosen && chosen == copyPath) QApplication::clipboard()->setText(fileId(row.file));
}

void DiffView::changeEvent(QEvent *event)
{
    QAbstractScrollArea::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        rebuildMetrics();
        rebuildRows();
    }
}

void DiffView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void DiffView::scrollContentsBy(int, int)
{
    viewport()->update();
}

void DiffView::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    if (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason) {
        m_keyboardCursor = true;
    }
    if (m_cursor < 0 && !m_rows.isEmpty()) m_cursor = rowAt(0) >= 0 ? rowAt(0) : 0;
    viewport()->update();
}

void DiffView::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    viewport()->update();
}

} // namespace kai::ui
