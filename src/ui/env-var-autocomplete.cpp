#include "ui/env-var-autocomplete.h"
#include "ui/fuzzy-search.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

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

enum class TriggerKind { Var, Conditional };

// Um snippet de bloco condicional oferecido quando o trigger é "{%" — o
// próprio "{%" já digitado pelo usuário NÃO entra em `prefix` (o trigger,
// igual ao caso de {{var}}, só é substituído a partir de onde o "{%"
// termina). O texto TYPED (a condição) fica entre `prefix` e `suffix` —
// commitSelection posiciona o cursor logo após `prefix`, pronto pra
// digitar a condição.
struct ConditionalSnippet {
    QString label;  // mostrado no popup
    QString prefix; // ex: "if " — vem logo depois do "{%" já digitado
    QString suffix; // ex: " %}\n{% endif %}"
};

QVector<ConditionalSnippet> conditionalSnippets()
{
    return {
        {utils::tr(QStringLiteral("template.autocomplete.if")),
         QStringLiteral(" if "), QStringLiteral(" %}\n{% endif %}")},
        {utils::tr(QStringLiteral("template.autocomplete.if_else")),
         QStringLiteral(" if "), QStringLiteral(" %}\n\n{% else %}\n\n{% endif %}")},
    };
}

// Acha o trigger ATIVO antes do cursor: "{{" (variável) ou, se
// `supportConditionals`, também "{%" (bloco condicional) — o mais recente
// que ainda não foi fechado nem interrompido por espaço/quebra de linha
// (nomes de variável e a palavra-chave "if"/"else" não têm espaço no
// meio). Retorna false se não há trigger ativo (popup deve ficar/ficar
// fechado). Quando os dois tipos de trigger estão presentes, vale o mais
// recente (mais próximo do cursor) dos dois.
bool findActiveTrigger(const QString &text, int cursorPos, bool supportConditionals,
                       int *triggerStart, QString *query, TriggerKind *kind)
{
    const int varOpenIdx = text.lastIndexOf(QStringLiteral("{{"), cursorPos - 1);
    const int condOpenIdx = supportConditionals
        ? text.lastIndexOf(QStringLiteral("{%"), cursorPos - 1) : -1;

    const bool useConditional = condOpenIdx > varOpenIdx;
    const int openIdx = useConditional ? condOpenIdx : varOpenIdx;
    if (openIdx < 0) {
        return false;
    }
    const QString closeToken = useConditional ? QStringLiteral("%}") : QStringLiteral("}}");
    const int afterOpen = openIdx + 2;
    if (afterOpen > cursorPos) {
        return false;
    }
    const QString between = text.mid(afterOpen, cursorPos - afterOpen);
    if (between.contains(closeToken)) {
        return false; // já fechado antes do cursor: trigger não está mais ativo
    }
    if (between.contains(QLatin1Char(' ')) || between.contains(QLatin1Char('\n'))
        || between.contains(QLatin1Char('\t'))) {
        return false; // usuário "saiu" do nome da variável/palavra-chave
    }
    *triggerStart = afterOpen;
    *query = between;
    *kind = useConditional ? TriggerKind::Conditional : TriggerKind::Var;
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
    // Substitui text[start:end) por `insertText` e posiciona o cursor em
    // start + cursorOffset (cursorOffset < 0, o default, significa "no fim
    // do texto inserido" — usado pra {{var}}; um snippet condicional passa
    // um offset explícito, pro cursor cair DENTRO do snippet, pronto pra
    // digitar a condição).
    std::function<void(int start, int end, const QString &insertText, int cursorOffset)> replaceRange;
    // Ponto global (canto inferior-esquerdo do cursor) para ancorar o popup.
    std::function<QPoint()> cursorGlobalPoint;
};

class EnvVarAutocompleteController : public QObject {
    Q_OBJECT

public:
    EnvVarAutocompleteController(QWidget *field, FieldAdapter adapter,
                                  std::function<QStringList()> provider,
                                  bool supportConditionals)
        : QObject(field)
        , m_field(field)
        , m_adapter(std::move(adapter))
        , m_provider(std::move(provider))
        , m_supportConditionals(supportConditionals)
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
        TriggerKind kind = TriggerKind::Var;
        if (!findActiveTrigger(m_adapter.text(), m_adapter.cursorPos(), m_supportConditionals,
                &triggerStart, &query, &kind)) {
            m_popup->hide();
            return;
        }
        m_triggerStart = triggerStart;
        m_triggerKind = kind;

        QStringList filtered;
        if (kind == TriggerKind::Conditional) {
            QStringList labels;
            for (const ConditionalSnippet &s : conditionalSnippets()) {
                labels << s.label;
            }
            // Query vazio (acabou de digitar "{%") mostra as duas opções —
            // FuzzyMatcher::search com query vazio já devolve tudo.
            for (const FuzzyMatchResult &r : FuzzyMatcher::search(query, labels)) {
                filtered << r.text;
            }
        } else {
            const QStringList vars = m_provider ? m_provider() : QStringList();
            for (const FuzzyMatchResult &r : FuzzyMatcher::search(query, vars)) {
                filtered << r.text;
            }
        }
        if (filtered.isEmpty()) {
            m_popup->hide();
            return;
        }
        m_popup->setItems(filtered);
        m_popup->move(m_adapter.cursorGlobalPoint());
        m_popup->show();
    }

    void commitSelection(const QString &chosenLabel)
    {
        if (chosenLabel.isEmpty()) {
            m_popup->hide();
            return;
        }
        // blockSignals: evita que a própria edição programática dispare
        // onTextChanged->refresh() no meio da troca (cursor/texto ainda
        // inconsistentes entre os dois passos de replaceRange).
        m_field->blockSignals(true);
        if (m_triggerKind == TriggerKind::Conditional) {
            for (const ConditionalSnippet &s : conditionalSnippets()) {
                if (s.label == chosenLabel) {
                    const QString full = s.prefix + s.suffix;
                    m_adapter.replaceRange(m_triggerStart, m_adapter.cursorPos(), full, s.prefix.size());
                    break;
                }
            }
        } else {
            m_adapter.replaceRange(m_triggerStart, m_adapter.cursorPos(),
                chosenLabel + QStringLiteral("}}"), -1);
        }
        m_field->blockSignals(false);
        m_popup->hide();
    }

    QPointer<QWidget> m_field;
    FieldAdapter m_adapter;
    std::function<QStringList()> m_provider;
    bool m_supportConditionals = false;
    VarAutocompletePopup *m_popup = nullptr;
    int m_triggerStart = 0;
    TriggerKind m_triggerKind = TriggerKind::Var;
};

} // namespace

void attachEnvVarAutocomplete(QPlainTextEdit *field, std::function<QStringList()> availableVarsProvider,
                               bool supportConditionals)
{
    if (!field) {
        return;
    }
    FieldAdapter adapter;
    adapter.text = [field]() { return field->toPlainText(); };
    adapter.cursorPos = [field]() { return field->textCursor().position(); };
    adapter.replaceRange = [field](int start, int end, const QString &insertText, int cursorOffset) {
        QTextCursor tc = field->textCursor();
        tc.setPosition(start);
        tc.setPosition(end, QTextCursor::KeepAnchor);
        tc.insertText(insertText);
        if (cursorOffset >= 0) {
            tc.setPosition(start + cursorOffset);
        }
        field->setTextCursor(tc);
    };
    adapter.cursorGlobalPoint = [field]() {
        const QRect r = field->cursorRect();
        return field->viewport()->mapToGlobal(r.bottomLeft());
    };

    auto *controller = new EnvVarAutocompleteController(field, adapter, std::move(availableVarsProvider),
                                                          supportConditionals);
    QObject::connect(field, &QPlainTextEdit::textChanged, controller,
                      &EnvVarAutocompleteController::onTextChanged);
}

void attachEnvVarAutocomplete(QLineEdit *field, std::function<QStringList()> availableVarsProvider,
                               bool supportConditionals)
{
    if (!field) {
        return;
    }
    FieldAdapter adapter;
    adapter.text = [field]() { return field->text(); };
    adapter.cursorPos = [field]() { return field->cursorPosition(); };
    adapter.replaceRange = [field](int start, int end, const QString &insertText, int cursorOffset) {
        QString t = field->text();
        t.remove(start, end - start);
        t.insert(start, insertText);
        field->setText(t);
        field->setCursorPosition(start + (cursorOffset >= 0 ? cursorOffset : insertText.size()));
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

    auto *controller = new EnvVarAutocompleteController(field, adapter, std::move(availableVarsProvider),
                                                          supportConditionals);
    QObject::connect(field, &QLineEdit::textChanged, controller,
                      [controller](const QString &) { controller->onTextChanged(); });
}

} // namespace kai::ui

#include "env-var-autocomplete.moc"
