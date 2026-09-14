#include "ui/features/http/http-body-editor-widget.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/features/http/http-entries-editor-widget.h"
#include "ui/features/http/http-ui.h"
#include "ui/shared/inline-code-field.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/json-template.h"
#include "utils/translation-manager.h"

namespace kai::ui {

namespace {
constexpr int kPageNone = 0;
constexpr int kPageText = 1;
constexpr int kPageFields = 2;
constexpr int kPageFile = 3;
} // namespace

HttpBodyEditorWidget::HttpBodyEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    makeTransparentHost(this, QStringLiteral("httpBodyEditor"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(utils::tokens::space(2));

    // "BODY" + tipo + formatar, na mesma linha.
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(utils::tokens::space(1));
    m_label = new QLabel(utils::tr(QStringLiteral("command.group.body")).toUpper(), this);
    m_label->setStyleSheet(QStringLiteral(
        "color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px; background: transparent;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    row->addWidget(m_label);
    row->addStretch(1);

    m_type = new QComboBox(this);
    m_type->addItem(utils::tr(QStringLiteral("http.body.none")), QStringLiteral("none"));
    m_type->addItem(utils::tr(QStringLiteral("http.body.raw")), QStringLiteral("raw"));
    m_type->addItem(QStringLiteral("JSON"), QStringLiteral("json"));
    m_type->addItem(QStringLiteral("XML"), QStringLiteral("xml"));
    m_type->addItem(utils::tr(QStringLiteral("http.body.form")), QStringLiteral("form"));
    m_type->addItem(utils::tr(QStringLiteral("http.body.multipart")), QStringLiteral("multipart"));
    m_type->addItem(utils::tr(QStringLiteral("http.body.file")), QStringLiteral("file"));
    m_type->addItem(QStringLiteral("GraphQL"), QStringLiteral("graphql"));
    row->addWidget(m_type);

    m_formatButton = new QToolButton(this);
    m_formatButton->setCursor(Qt::PointingHandCursor);
    m_formatButton->setAutoRaise(true);
    m_formatButton->setIcon(LucideIcons::icon(QStringLiteral("file-text"), QColor(utils::tokens::mutedFg()), 16));
    m_formatButton->setIconSize(QSize(16, 16));
    m_formatButton->setToolTip(utils::tr(QStringLiteral("command.format_json")));
    m_formatButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: %1px; background: transparent; }"
        "QToolButton:hover { background: %2; }")
        .arg(utils::tokens::radiusSm()).arg(utils::tokens::hoverBg()));
    connect(m_formatButton, &QToolButton::clicked, this, &HttpBodyEditorWidget::formatJson);
    row->addWidget(m_formatButton);
    layout->addLayout(row);

    m_stack = new QStackedWidget(this);
    makeTransparentHost(m_stack, QStringLiteral("httpBodyStack"));

    auto *nonePage = new QWidget(m_stack);
    makeTransparentHost(nonePage, QStringLiteral("httpBodyNone"));
    m_stack->addWidget(nonePage);

    auto *textPage = new QWidget(m_stack);
    makeTransparentHost(textPage, QStringLiteral("httpBodyText"));
    auto *textLayout = new QVBoxLayout(textPage);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(utils::tokens::space(1));
    m_text = new InlineCodeField(textPage);
    m_text->setEditorTitle(utils::tr(QStringLiteral("command_editor.body.title")));
    m_text->setLineRange(4, 12);
    m_text->setJsonSyntax(true);
    textLayout->addWidget(m_text, 1);

    m_status = new QLabel(textPage);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    textLayout->addWidget(m_status);
    connect(m_text, &InlineCodeField::textChanged, this, [this]() {
        const QString type = m_type->currentData().toString();
        const QString raw = m_text->toPlainText();
        if (raw.trimmed().isEmpty() || (type != QStringLiteral("raw") && type != QStringLiteral("json"))) {
            m_status->clear();
            return;
        }
        QJsonParseError e;
        QJsonDocument::fromJson(utils::maskTemplateVars(raw).text.toUtf8(), &e);
        if (e.error == QJsonParseError::NoError) {
            m_status->setText(utils::tr(QStringLiteral("json.status.valid")));
            m_status->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::successFg()));
        } else {
            const int line = raw.left(e.offset).count(QLatin1Char('\n')) + 1;
            m_status->setText(utils::tr(QStringLiteral("json.status.invalid")).arg(e.errorString()).arg(line));
            m_status->setStyleSheet(QStringLiteral("color: %1;").arg(utils::tokens::errorFg()));
        }
        emit changed();
    });

    // GraphQL: as variáveis (JSON) numa caixa abaixo da query.
    m_graphqlBox = new QWidget(textPage);
    makeTransparentHost(m_graphqlBox, QStringLiteral("httpBodyGraphql"));
    auto *variablesLayout = new QVBoxLayout(m_graphqlBox);
    variablesLayout->setContentsMargins(0, 0, 0, 0);
    variablesLayout->setSpacing(utils::tokens::space(1));
    auto *variablesLabel = new QLabel(utils::tr(QStringLiteral("http.body.graphql_variables")).toUpper(), m_graphqlBox);
    variablesLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: %2pt; font-weight: 600; letter-spacing: 0.5px; background: transparent;")
        .arg(utils::tokens::mutedFg()).arg(utils::tokens::fontSizeSmallPt()));
    variablesLayout->addWidget(variablesLabel);
    m_graphqlVariables = new InlineCodeField(m_graphqlBox);
    m_graphqlVariables->setPlaceholderText(QStringLiteral("{\"id\": \"{{USER_ID}}\"}"));
    m_graphqlVariables->setEditorTitle(utils::tr(QStringLiteral("http.body.graphql_variables")));
    m_graphqlVariables->setLineRange(2, 6);
    m_graphqlVariables->setJsonSyntax(true);
    variablesLayout->addWidget(m_graphqlVariables);
    textLayout->addWidget(m_graphqlBox);
    m_stack->addWidget(textPage);

    m_fields = new HttpEntriesEditorWidget(m_stack);
    m_fields->setMultilineValue(true);
    m_stack->addWidget(m_fields);
    connect(m_fields, &HttpEntriesEditorWidget::changed, this, &HttpBodyEditorWidget::changed);

    auto *fileHost = new QWidget(m_stack);
    makeTransparentHost(fileHost, QStringLiteral("httpBodyFile"));
    auto *fileOuter = new QVBoxLayout(fileHost);
    fileOuter->setContentsMargins(0, 0, 0, 0);
    auto *fileRow = new QHBoxLayout();
    m_file = new QLineEdit(fileHost);
    m_file->setPlaceholderText(utils::tr(QStringLiteral("http.body.file.placeholder")));
    fileRow->addWidget(m_file, 1);
    auto *browse = new QPushButton(utils::tr(QStringLiteral("http.body.file.browse")), fileHost);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, utils::tr(QStringLiteral("http.body.file.browse")));
        if (!path.isEmpty()) {
            m_file->setText(path);
        }
    });
    fileRow->addWidget(browse);
    fileOuter->addLayout(fileRow);
    fileOuter->addStretch(1);
    connect(m_file, &QLineEdit::textChanged, this, &HttpBodyEditorWidget::changed);
    m_stack->addWidget(fileHost);

    layout->addWidget(m_stack, 1);

    connect(m_type, &QComboBox::currentIndexChanged, this, [this]() {
        updateTypeUi();
        emit changed();
    });
    m_type->setCurrentIndex(m_type->findData(QStringLiteral("raw")));
    updateTypeUi();
}

void HttpBodyEditorWidget::updateTypeUi()
{
    const QString type = m_type->currentData().toString();
    int page = kPageText;
    if (type == QStringLiteral("none")) page = kPageNone;
    else if (type == QStringLiteral("form") || type == QStringLiteral("multipart")) page = kPageFields;
    else if (type == QStringLiteral("file")) page = kPageFile;
    m_stack->setCurrentIndex(page);
    // Só a página atual conta no tamanho (o QStackedWidget mede pela maior).
    for (int i = 0; i < m_stack->count(); ++i) {
        m_stack->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                          i == page ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    }

    m_fields->setFileSupport(type == QStringLiteral("multipart"));
    m_graphqlBox->setVisible(type == QStringLiteral("graphql"));
    const bool jsonLike = type == QStringLiteral("raw") || type == QStringLiteral("json");
    m_formatButton->setVisible(jsonLike);
    m_text->setJsonSyntax(jsonLike);
    if (type == QStringLiteral("xml")) {
        m_text->setPlaceholderText(QStringLiteral("<root><item>value</item></root>"));
    } else if (type == QStringLiteral("graphql")) {
        m_text->setPlaceholderText(QStringLiteral("query { viewer { id } }"));
    } else {
        m_text->setPlaceholderText(QStringLiteral("{\"key\": \"value\"}"));
    }
    m_status->clear();
    if (page == kPageText) {
        emit m_text->textChanged();
    }
}

void HttpBodyEditorWidget::formatJson()
{
    const QString raw = m_text->toPlainText();
    if (raw.trimmed().isEmpty()) {
        return;
    }
    const utils::MaskedTemplate masked = utils::maskTemplateVars(raw);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(masked.text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("json.dialog.format_title")),
            utils::tr(QStringLiteral("command_editor.body.invalid_format")).arg(parseError.errorString()));
        return;
    }
    m_text->setPlainText(utils::unmaskTemplateVars(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)), masked));
}

void HttpBodyEditorWidget::setLabelVisible(bool visible)
{
    m_label->setVisible(visible);
}

void HttpBodyEditorWidget::setBodyType(core::HttpBodyType type)
{
    m_type->setCurrentIndex(qMax(0, m_type->findData(core::httpBodyTypeToString(type))));
}

void HttpBodyEditorWidget::setConfig(const core::HttpConfig &config)
{
    setBodyType(config.bodyType);
    m_text->setPlainText(config.body);
    m_graphqlVariables->setPlainText(config.graphqlVariables);
    m_fields->setEntries(config.formFields);
    m_file->setText(config.bodyFile);
    updateTypeUi();
}

void HttpBodyEditorWidget::applyTo(core::HttpConfig &config) const
{
    config.bodyType = core::httpBodyTypeFromString(m_type->currentData().toString());
    config.body = m_text->toPlainText();
    config.graphqlVariables = m_graphqlVariables->toPlainText();
    config.formFields = m_fields->entries();
    config.bodyFile = m_file->text().trimmed();
}

} // namespace kai::ui
