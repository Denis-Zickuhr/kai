#include "ui/shared/icon-picker-widget.h"
#include "ui/shared/icon-picker-dialog.h"
#include "ui/shared/lucide-icons.h"
#include "utils/translation-manager.h"
#include "utils/design-tokens.h"

#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QMap>
#include <QImageReader>
#include <QPixmap>
#include <QFileInfo>
#include <QDir>
#include <QSet>

namespace kai::ui {

namespace {
constexpr const char *kCustomIconPrefix = "file:";
constexpr int kIconSize = 28;

// Pool de ícones: a CHAVE (esquerda) é estável e persistida em Folder::icon / Command::icon, nunca muda
// (compatibilidade com kai.yml salvos). O VALOR é o nome do ícone Lucide (:/icons/lucide/).
const QVector<QPair<QString, QString>> &iconPool()
{
    static QVector<QPair<QString, QString>> pool;
    if (!pool.isEmpty()) {
        return pool;
    }
    // Aliases curados (chave estável -> nome Lucide).
    pool = {
        {QStringLiteral("folder"), QStringLiteral("folder")},
        {QStringLiteral("terminal"), QStringLiteral("terminal")},
        {QStringLiteral("network"), QStringLiteral("network")},
        {QStringLiteral("file"), QStringLiteral("file")},
        {QStringLiteral("play"), QStringLiteral("play")},
        {QStringLiteral("gear"), QStringLiteral("settings")},
        {QStringLiteral("database"), QStringLiteral("database")},
        {QStringLiteral("browser"), QStringLiteral("globe")},
        {QStringLiteral("trash"), QStringLiteral("trash-2")},
        {QStringLiteral("info"), QStringLiteral("info")},
        {QStringLiteral("warning"), QStringLiteral("triangle-alert")},
        {QStringLiteral("home"), QStringLiteral("house")},
        {QStringLiteral("rocket"), QStringLiteral("rocket")},
        {QStringLiteral("api"), QStringLiteral("webhook")},
        {QStringLiteral("build"), QStringLiteral("hammer")},
        {QStringLiteral("test"), QStringLiteral("flask-conical")},
    };
    // Além dos aliases, todos os ícones Lucide embarcados (key == lucideName), sem duplicar os já mapeados por um alias.
    QSet<QString> alreadyLucide;
    for (const auto &e : pool) {
        alreadyLucide.insert(e.second);
    }
    const QDir dir(QStringLiteral(":/icons/lucide"));
    QStringList files = dir.entryList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        QString name = f;
        name.chop(4); // remove ".svg"
        if (name == QStringLiteral("chevron-down-fg")) {
            continue;
        }
        if (alreadyLucide.contains(name)) {
            continue;
        }
        pool.append({name, name});
    }
    return pool;
}

// Resolve a chave estável do pool (ex: "gear") para o nome Lucide. Se não estiver no pool mas já for um nome
// Lucide embarcado, usa-a direto; senão, string vazia.
QString lucideNameForKey(const QString &key)
{
    for (const auto &entry : iconPool()) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    if (LucideIcons::has(key)) {
        return key;
    }
    return QString();
}

QString displayNameFor(const QString &iconName)
{
    static const QMap<QString, QString> labels = {
        {QStringLiteral("folder"), utils::tr(QStringLiteral("icon_picker.name.folder"))},
        {QStringLiteral("terminal"), utils::tr(QStringLiteral("icon_picker.name.terminal"))},
        {QStringLiteral("network"), utils::tr(QStringLiteral("icon_picker.name.network"))},
        {QStringLiteral("file"), utils::tr(QStringLiteral("icon_picker.name.file"))},
        {QStringLiteral("play"), utils::tr(QStringLiteral("icon_picker.name.play"))},
        {QStringLiteral("gear"), utils::tr(QStringLiteral("icon_picker.name.gear"))},
        {QStringLiteral("database"), utils::tr(QStringLiteral("icon_picker.name.database"))},
        {QStringLiteral("browser"), utils::tr(QStringLiteral("icon_picker.name.browser"))},
        {QStringLiteral("trash"), utils::tr(QStringLiteral("icon_picker.name.trash"))},
        {QStringLiteral("info"), utils::tr(QStringLiteral("icon_picker.name.info"))},
        {QStringLiteral("warning"), utils::tr(QStringLiteral("icon_picker.name.warning"))},
        {QStringLiteral("home"), utils::tr(QStringLiteral("icon_picker.name.home"))},
        {QStringLiteral("rocket"), utils::tr(QStringLiteral("icon_picker.name.rocket"))},
        {QStringLiteral("api"), utils::tr(QStringLiteral("icon_picker.name.api"))},
        {QStringLiteral("build"), utils::tr(QStringLiteral("icon_picker.name.build"))},
        {QStringLiteral("test"), utils::tr(QStringLiteral("icon_picker.name.test"))},
    };
    return labels.value(iconName, iconName);
}
}

IconPickerWidget::IconPickerWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void IconPickerWidget::setupUi()
{
    namespace tk = utils::tokens;

    // Chip com borda no padrão de QLineEdit/QComboBox; o widget inteiro é clicável. WA_StyledBackground é
    // necessário para um QWidget puro pintar border/background do QSS.
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("iconPickerField"));
    setCursor(Qt::PointingHandCursor);
    // Qualificado por #iconPickerField: uma regra sem seletor cascateia a borda aos filhos (botão e QLabel).
    setStyleSheet(QStringLiteral(
        "QWidget#iconPickerField { border: 1px solid %1; border-radius: %2px;"
        " background-color: %3; }")
        .arg(tk::borderColor()).arg(tk::radiusMd()).arg(tk::bg()));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(tk::space(2), tk::space(1), tk::space(2), tk::space(1));
    layout->setSpacing(tk::space(1));

    m_iconButton = new QToolButton(this);
    m_iconButton->setIconSize(QSize(kIconSize, kIconSize));
    m_iconButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_iconButton->setAutoRaise(true);
    m_iconButton->setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; }"));
    m_iconButton->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(m_iconButton, &QToolButton::clicked, this, &IconPickerWidget::handleChooseIconClicked);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    layout->addWidget(m_iconButton);
    layout->addWidget(m_nameLabel, 1);

    updateButtonDisplay();
}

void IconPickerWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        handleChooseIconClicked();
        return;
    }
    QWidget::mousePressEvent(event);
}

void IconPickerWidget::handleChooseIconClicked()
{
    // Diálogo modal em grade no lugar do QComboBox (o popup dele pode crashar sob Wayland).
    IconPickerDialog dialog(m_selectedIconName, this);
    if (dialog.exec() == QDialog::Accepted) {
        setSelectedIconName(dialog.chosenIconName());
    }
}

void IconPickerWidget::updateButtonDisplay()
{
    const QIcon icon = iconForName(m_selectedIconName);
    m_iconButton->setIcon(icon);

    if (m_selectedIconName.isEmpty()) {
        // Placeholder convidativo, distinto de icon_picker.none (o item "limpar" da grade do diálogo).
        m_nameLabel->setText(utils::tr(QStringLiteral("icon_picker.select_placeholder")));
    } else if (m_selectedIconName.startsWith(QString::fromLatin1(kCustomIconPrefix))) {
        m_nameLabel->setText(utils::tr(QStringLiteral("icon_picker.custom"))
            .arg(QFileInfo(m_selectedIconName.mid(qstrlen(kCustomIconPrefix))).fileName()));
    } else {
        m_nameLabel->setText(displayNameFor(m_selectedIconName));
    }
}

QString IconPickerWidget::selectedIconName() const
{
    return m_selectedIconName;
}

void IconPickerWidget::setSelectedIconName(const QString &iconName)
{
    m_selectedIconName = iconName;
    updateButtonDisplay();
}

QStringList IconPickerWidget::poolIconNames()
{
    QStringList names;
    for (const auto &entry : iconPool()) {
        names << entry.first;
    }
    return names;
}

QString IconPickerWidget::displayNameForPoolIcon(const QString &iconName)
{
    return displayNameFor(iconName);
}

QIcon IconPickerWidget::iconForName(const QString &iconName)
{
    if (iconName.isEmpty()) {
        return QIcon();
    }

    if (iconName.startsWith(QString::fromLatin1(kCustomIconPrefix))) {
        const QString filePath = iconName.mid(qstrlen(kCustomIconPrefix));
        if (QFileInfo::exists(filePath)) {
            // Formatos animados (.gif, .webp): QIcon não anima, então extrai só o primeiro quadro, já no tamanho do ícone.
            // Fazer à mão evita decodificar a animação inteira e garante um quadro válido mesmo com primeiro quadro parcial.
            QImageReader reader(filePath);
            if (reader.supportsAnimation() && reader.imageCount() > 1) {
                reader.setScaledSize(QSize(kIconSize, kIconSize));
                const QImage frame = reader.read();
                if (!frame.isNull()) {
                    return QIcon(QPixmap::fromImage(frame));
                }
            }
            return QIcon(filePath);
        }
        // Arquivo customizado removido/movido: ícone nulo; o chamador aplica o próprio fallback.
        return QIcon();
    }

    // Ícone do pool: resolve a chave para o nome Lucide e recolore com o accent do tema atual (em runtime).
    const QString lucideName = lucideNameForKey(iconName);
    if (lucideName.isEmpty()) {
        return QIcon();
    }
    return LucideIcons::icon(lucideName, QColor(utils::tokens::accent()), kIconSize);
}

} // namespace kai::ui
