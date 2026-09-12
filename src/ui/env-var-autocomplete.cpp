#include "ui/env-var-autocomplete.h"
#include "ui/fuzzy-search.h"
#include "utils/design-tokens.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextCursor>
#include <QVBoxLayout>

namespace kai::ui {

namespace {

// Acha o trigger "{{" ATIVO antes do cursor (o mais recente que ainda não
// foi fechado por "}}" nem interrompido por espaço/quebra de linha —
// nomes de variável não têm espaço). Retorna false se não há trigger ativo
// (popup deve ficar/ficar fechado).
bool findActiveTrigger(const QString &text, int cursorPos, int *triggerStart, QString *query)
{
    const int openIdx = text.lastIndexOf(QStringLiteral("{{"), cursorPos - 1);
    if (openIdx < 0) {
        return false;
    }
    const int afterOpen = openIdx + 2;
    if (afterOpen > cursorPos) {
        return false;
    }
    const QString between = text.mid(afterOpen, cursorPos - afterOpen);
    if (between.contains(QStringLiteral("}}"))) {
        return false; // já fechado antes do cursor: trigger não está mais ativo
    }
    if (between.contains(QLatin1Char(' ')) || between.contains(QLatin1Char('\n'))
        || between.contains(QLatin1Char('\t'))) {
        return false; // usuário "saiu" do nome da variável
    }
    *triggerStart = afterOpen;
    *query = between;
    return true;
}

// Popup leve, sem foco próprio (Qt::ToolTip + WA_ShowWithoutActivating):
// o campo de texto NUNCA perde o foco enquanto o popup está aberto — quem
// intercepta Up/Down/Enter/Escape é o eventFilter no próprio campo (ver
// EnvVarAutocompleteController), não o popup.
class VarAutocompletePopup : public QWidget {
    Q_OBJECT

public:
    explicit VarAutocompletePopup(QWidget *parent)
        : QWidget(parent, Qt::ToolTip)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFocusPolicy(Qt::NoFocus);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        m_list = new QListWidget(this);
        m_list->setFocusPolicy(Qt::NoFocus);
        m_list->setUniformItemSizes(true);
        m_list->setFrameShape(QFrame::NoFrame);
        layout->addWidget(m_list);

        using namespace kai::utils;
        setStyleSheet(QStringLiteral(
            "QListWidget {"
            " background-color: %1; color: %2; border: 1px solid %3;"
            " border-radius: %4px; outline: none; padding: 2px; }"
            "QListWidget::item { padding: 4px 8px; border-radius: %5px; }"
            "QListWidget::item:selected { background-color: %6; color: %2; }")
            .arg(tokens::surface2(), tokens::fg(), tokens::borderColor())
            .arg(tokens::radiusMd()).arg(tokens::radiusSm())
            .arg(tokens::selBg()));

        connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            emit itemChosen(item->text());
        });
    }

    void setItems(const QStringList &items)
    {
        m_list->clear();
        m_list->addItems(items);
        if (!items.isEmpty()) {
            m_list->setCurrentRow(0);
        }
        const int rowH = m_list->sizeHintForRow(0) > 0 ? m_list->sizeHintForRow(0) : 22;
        const int visibleRows = qMin(items.size(), 8);
        m_list->setFixedHeight(visibleRows * rowH + 6);
        setFixedWidth(qMax(180, m_list->sizeHintForColumn(0) + 24));
        adjustSize();
    }

    void moveSelection(int delta)
    {
        const int count = m_list->count();
        if (count == 0) {
            return;
        }
        int row = m_list->currentRow();
        row = (row + delta + count) % count;
        m_list->setCurrentRow(row);
    }

    QString currentText() const
    {
        QListWidgetItem *item = m_list->currentItem();
        return item ? item->text() : QString();
    }

signals:
    void itemChosen(const QString &text);

private:
    QListWidget *m_list = nullptr;
};

// Adapta as diferenças de API entre QPlainTextEdit e QLineEdit atrás de um
// conjunto pequeno de callbacks — evita duplicar toda a lógica de
// trigger/filtro/navegação para cada tipo de widget.
struct FieldAdapter {
    std::function<QString()> text;
    std::function<int()> cursorPos;
    // Substitui text[start:end) por `insertText` e posiciona o cursor logo após.
    std::function<void(int start, int end, const QString &insertText)> replaceRange;
    // Ponto global (canto inferior-esquerdo do cursor) para ancorar o popup.
    std::function<QPoint()> cursorGlobalPoint;
};

class EnvVarAutocompleteController : public QObject {
    Q_OBJECT

public:
    EnvVarAutocompleteController(QWidget *field, FieldAdapter adapter,
                                  std::function<QStringList()> provider)
        : QObject(field)
        , m_field(field)
        , m_adapter(std::move(adapter))
        , m_provider(std::move(provider))
    {
        m_popup = new VarAutocompletePopup(field->window());
        connect(m_popup, &VarAutocompletePopup::itemChosen, this,
                &EnvVarAutocompleteController::commitSelection);
        field->installEventFilter(this);
    }

public slots:
    void onTextChanged()
    {
        refresh();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_field && m_popup->isVisible() && event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            switch (ke->key()) {
            case Qt::Key_Down:
                m_popup->moveSelection(1);
                return true;
            case Qt::Key_Up:
                m_popup->moveSelection(-1);
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Tab:
                commitSelection(m_popup->currentText());
                return true;
            case Qt::Key_Escape:
                m_popup->hide();
                return true;
            default:
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void refresh()
    {
        int triggerStart = 0;
        QString query;
        if (!findActiveTrigger(m_adapter.text(), m_adapter.cursorPos(), &triggerStart, &query)) {
            m_popup->hide();
            return;
        }
        m_triggerStart = triggerStart;
        const QStringList vars = m_provider ? m_provider() : QStringList();
        QStringList filtered;
        for (const FuzzyMatchResult &r : FuzzyMatcher::search(query, vars)) {
            filtered << r.text;
        }
        if (filtered.isEmpty()) {
            m_popup->hide();
            return;
        }
        m_popup->setItems(filtered);
        m_popup->move(m_adapter.cursorGlobalPoint());
        m_popup->show();
    }

    void commitSelection(const QString &varName)
    {
        if (varName.isEmpty()) {
            m_popup->hide();
            return;
        }
        // blockSignals: evita que a própria edição programática dispare
        // onTextChanged->refresh() no meio da troca (cursor/texto ainda
        // inconsistentes entre os dois passos de replaceRange).
        m_field->blockSignals(true);
        m_adapter.replaceRange(m_triggerStart, m_adapter.cursorPos(), varName + QStringLiteral("}}"));
        m_field->blockSignals(false);
        m_popup->hide();
    }

    QPointer<QWidget> m_field;
    FieldAdapter m_adapter;
    std::function<QStringList()> m_provider;
    VarAutocompletePopup *m_popup = nullptr;
    int m_triggerStart = 0;
};

} // namespace

void attachEnvVarAutocomplete(QPlainTextEdit *field, std::function<QStringList()> availableVarsProvider)
{
    if (!field) {
        return;
    }
    FieldAdapter adapter;
    adapter.text = [field]() { return field->toPlainText(); };
    adapter.cursorPos = [field]() { return field->textCursor().position(); };
    adapter.replaceRange = [field](int start, int end, const QString &insertText) {
        QTextCursor tc = field->textCursor();
        tc.setPosition(start);
        tc.setPosition(end, QTextCursor::KeepAnchor);
        tc.insertText(insertText);
        field->setTextCursor(tc);
    };
    adapter.cursorGlobalPoint = [field]() {
        const QRect r = field->cursorRect();
        return field->viewport()->mapToGlobal(r.bottomLeft());
    };

    auto *controller = new EnvVarAutocompleteController(field, adapter, std::move(availableVarsProvider));
    QObject::connect(field, &QPlainTextEdit::textChanged, controller,
                      &EnvVarAutocompleteController::onTextChanged);
}

void attachEnvVarAutocomplete(QLineEdit *field, std::function<QStringList()> availableVarsProvider)
{
    if (!field) {
        return;
    }
    FieldAdapter adapter;
    adapter.text = [field]() { return field->text(); };
    adapter.cursorPos = [field]() { return field->cursorPosition(); };
    adapter.replaceRange = [field](int start, int end, const QString &insertText) {
        QString t = field->text();
        t.remove(start, end - start);
        t.insert(start, insertText);
        field->setText(t);
        field->setCursorPosition(start + insertText.size());
    };
    adapter.cursorGlobalPoint = [field]() {
        // QLineEdit::cursorRect() é PROTECTED (diferente de
        // QPlainTextEdit::cursorRect(), público) — aproxima a posição pela
        // largura do texto até o cursor via QFontMetrics. Não considera
        // scroll horizontal interno em campos muito longos com o cursor
        // fora da parte visível, mas é preciso o bastante pros campos
        // deste app (URL/Default), de largura generosa.
        const QFontMetrics fm(field->font());
        const int textX = fm.horizontalAdvance(field->text().left(field->cursorPosition()));
        const QPoint local(textX + 4, field->height());
        return field->mapToGlobal(local);
    };

    auto *controller = new EnvVarAutocompleteController(field, adapter, std::move(availableVarsProvider));
    QObject::connect(field, &QLineEdit::textChanged, controller,
                      [controller](const QString &) { controller->onTextChanged(); });
}

} // namespace kai::ui

#include "env-var-autocomplete.moc"
