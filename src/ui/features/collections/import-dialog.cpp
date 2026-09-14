#include "ui/features/collections/import-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"
#include "core/yaml-bridge.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace kai::ui {
namespace tk = kai::utils::tokens;

namespace {

// Cartão de fonte (pasta/arquivo) — ícone + título + descrição, clicável.
// NÃO é um QToolButton: um QToolButton não deriva o sizeHint do layout
// filho e o CHROME nativo do botão (área de conteúdo reservada pro
// ícone+texto de um tool button "normal") cortava o topo do ícone
// customizado (bug real, com foto: "Icones ficaram cortados"). QFrame
// clicável de verdade — mesmo padrão já usado em IconPickerWidget
// (mousePressEvent próprio, sem herdar de nenhum QAbstractButton) — não
// tem esse chrome, então o layout filho tem o espaço inteiro pra si.
class SourceCard : public QFrame {
    Q_OBJECT

public:
    explicit SourceCard(const QString &iconName, const QString &title,
                         const QString &description, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(150);
        setStyleSheet(QStringLiteral(
            "SourceCard { border: 1px solid %1; border-radius: %2px;"
            " background-color: %3; }"
            "SourceCard:hover { border: 1px solid %4; background-color: %5; }")
            .arg(tk::borderColor()).arg(tk::radiusMd()).arg(tk::surface())
            .arg(tk::accent()).arg(tk::hoverBg()));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
        layout->setSpacing(tk::space(1));

        auto *iconLabel = new QLabel(this);
        iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        iconLabel->setPixmap(LucideIcons::icon(iconName, QColor(tk::accent()), 28).pixmap(28, 28));
        layout->addWidget(iconLabel);

        auto *titleLabel = new QLabel(title, this);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: %1pt;")
            .arg(tk::fontSizePt() + 1));
        layout->addWidget(titleLabel);

        auto *descLabel = new QLabel(description, this);
        descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        descLabel->setProperty("kaiRole", QStringLiteral("caption"));
        descLabel->setWordWrap(true);
        layout->addWidget(descLabel);
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
            return;
        }
        QFrame::mousePressEvent(event);
    }
};

} // namespace

ImportDialog::ImportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("import.title")));
    setSizeGripEnabled(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(tk::space(5), tk::space(5), tk::space(5), tk::space(4));
    layout->setSpacing(tk::space(3));

    auto *hintBanner = layout_helpers::makeHintBanner(this, utils::tr(QStringLiteral("import.hint")));
    hintBanner->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(hintBanner);

    auto *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(tk::space(3));

    auto *projectCard = new SourceCard(QStringLiteral("folder-input"),
        utils::tr(QStringLiteral("import.source.project.title")),
        utils::tr(QStringLiteral("import.source.project.description")), this);
    connect(projectCard, &SourceCard::clicked, this, &ImportDialog::pickProjectFolder);
    cardsRow->addWidget(projectCard);

    auto *fileCard = new SourceCard(QStringLiteral("file-code"),
        utils::tr(QStringLiteral("import.source.file.title")),
        utils::tr(QStringLiteral("import.source.file.description")), this);
    connect(fileCard, &SourceCard::clicked, this, &ImportDialog::pickFile);
    cardsRow->addWidget(fileCard);

    layout->addLayout(cardsRow);
    layout->addStretch(1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    stripDialogButtonIcons(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    setMinimumWidth(520);
    resize(qMax(520, width()), height());
    centerOnParent(this);
}

void ImportDialog::pickProjectFolder()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, utils::tr(QStringLiteral("import.source.project.title")),
        QDir::homePath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty()) {
        return;
    }
    m_kind = Kind::Project;
    m_path = directory;
    accept();
}

void ImportDialog::pickFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, utils::tr(QStringLiteral("import.source.file.title")), QString(),
        utils::tr(QStringLiteral("import.source.file.filter")));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("import.title")),
            utils::tr(QStringLiteral("import.error.read_failed")).arg(file.errorString()));
        return;
    }
    const QString rawText = QString::fromUtf8(file.readAll());
    file.close();

    Kind detected;
    if (!detectKindFromText(rawText, &detected)) {
        QMessageBox::warning(this, utils::tr(QStringLiteral("import.title")),
            utils::tr(QStringLiteral("import.error.unrecognized")));
        return;
    }
    m_kind = detected;
    m_path = path;
    accept();
}

bool ImportDialog::detectKindFromText(const QString &rawText, Kind *outKind)
{
    QString jsonText = rawText;
    if (!core::looksLikeJson(rawText)) {
        bool yamlOk = false;
        QString yamlError;
        jsonText = core::yamlTextToJsonText(rawText, &yamlOk, &yamlError);
        if (!yamlOk) {
            return false;
        }
    }

    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();

    // OpenAPI/Swagger: identificado pela própria chave que a especificação
    // exige no topo ("openapi": "3.0.0" ou "swagger": "2.0").
    if (root.contains(QStringLiteral("openapi")) || root.contains(QStringLiteral("swagger"))) {
        *outKind = Kind::OpenApi;
        return true;
    }
    // Configuração exportada do Kai: marca explícita (id_free/kai_export,
    // ver ConfigManager) OU, na ausência dela (arquivo hand-authored),
    // qualquer uma das chaves estruturais que só esse formato usa.
    if (root.contains(QStringLiteral("kai_export"))
        || root.contains(QStringLiteral("commands"))
        || root.contains(QStringLiteral("folders"))
        || root.contains(QStringLiteral("collections"))
        || root.contains(QStringLiteral("settings"))) {
        *outKind = Kind::Config;
        return true;
    }

    return false;
}

} // namespace kai::ui

#include "import-dialog.moc"
