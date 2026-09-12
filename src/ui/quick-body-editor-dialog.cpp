#include "ui/quick-body-editor-dialog.h"
#include "utils/design-tokens.h"
#include "ui/dialog-utils.h"
#include "ui/lucide-icons.h"
#include "ui/json-syntax-highlighter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QFont>
#include <QFontDatabase>

#include "utils/translation-manager.h"

namespace kai::ui {

QuickBodyEditorDialog::QuickBodyEditorDialog(const QString &commandName, const QString &initialBody, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("quick_body_editor.title")).arg(commandName));
    setSizeGripEnabled(true);
    resize(720, 500);
    setupUi(commandName, initialBody);
    centerOnParent(this);
}

void QuickBodyEditorDialog::setupUi(const QString &commandName, const QString &initialBody)
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Barra de ações ---
    auto *headerLayout = new QHBoxLayout();
    headerLayout->addWidget(new QLabel(utils::tr(QStringLiteral("quick_body_editor.label")).arg(commandName), this));
    headerLayout->addStretch();

    auto *formatButton = new QPushButton(utils::tr(QStringLiteral("body.format")), this);
    formatButton->setIcon(LucideIcons::icon(QStringLiteral("align-left"), QColor(139, 233, 253), 16));
    connect(formatButton, &QPushButton::clicked, this, &QuickBodyEditorDialog::handleFormatJsonRequested);
    headerLayout->addWidget(formatButton);

    auto *minifyButton = new QPushButton(utils::tr(QStringLiteral("body.minify")), this);
    minifyButton->setIcon(LucideIcons::icon(QStringLiteral("minimize-2"), QColor(139, 233, 253), 16));
    connect(minifyButton, &QPushButton::clicked, this, &QuickBodyEditorDialog::handleMinifyRequested);
    headerLayout->addWidget(minifyButton);

    mainLayout->addLayout(headerLayout);

    // --- Editor com fonte monoespaçada + realce de sintaxe JSON ---
    m_bodyField = new QPlainTextEdit(this);
    m_bodyField->setPlainText(initialBody);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(11);
    m_bodyField->setFont(mono);
    m_bodyField->setLineWrapMode(QPlainTextEdit::NoWrap);
    // Tab = 2 espaços de largura (visual).
    m_bodyField->setTabStopDistance(2 * QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')));
    m_highlighter = new JsonSyntaxHighlighter(m_bodyField->document());
    connect(m_bodyField, &QPlainTextEdit::textChanged, this, &QuickBodyEditorDialog::handleValidateLive);
    mainLayout->addWidget(m_bodyField, 1);

    // --- Status de validação ao vivo ---
    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_statusLabel);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    handleValidateLive(); // status inicial
}

void QuickBodyEditorDialog::handleValidateLive()
{
    const QString raw = m_bodyField->toPlainText();
    if (raw.trimmed().isEmpty()) {
        m_statusLabel->setText(QString());
        return;
    }
    QJsonParseError err;
    QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError) {
        m_statusLabel->setText(utils::tr(QStringLiteral("json.status.valid")));
        m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::successFg())); // verde
    } else {
        // Calcula a linha aproximada do erro pelo offset.
        const int offset = err.offset;
        const int line = raw.left(offset).count(QLatin1Char('\n')) + 1;
        m_statusLabel->setText(utils::tr(QStringLiteral("json.status.invalid")).arg(err.errorString()).arg(line));
        m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::errorFg())); // vermelho
    }
}

void QuickBodyEditorDialog::handleFormatJsonRequested()
{
    const QString rawText = m_bodyField->toPlainText();
    if (rawText.trimmed().isEmpty()) {
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(rawText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // Não bloqueia com popup: o status ao vivo já mostra o erro.
        return;
    }
    m_bodyField->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
}

void QuickBodyEditorDialog::handleMinifyRequested()
{
    const QString rawText = m_bodyField->toPlainText();
    if (rawText.trimmed().isEmpty()) {
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(rawText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return;
    }
    m_bodyField->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

QString QuickBodyEditorDialog::body() const
{
    return m_bodyField->toPlainText();
}

} // namespace kai::ui
