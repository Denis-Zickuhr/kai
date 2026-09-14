#include "ui/shared/manifesto-dialog.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/asset-paths.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>

namespace kai::ui {

namespace tk = kai::utils::tokens;

namespace {
// Lê e concatena os arquivos de um manifesto. Roda FORA da thread da interface.
QString readManifesto(const QStringList &files)
{
    const QString dir = utils::assetDir(QStringLiteral("manifesto"));
    if (dir.isEmpty()) {
        return QString();
    }
    QStringList parts;
    for (const QString &name : files) {
        QFile file(QDir(dir).filePath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        parts << QString::fromUtf8(file.readAll()).trimmed();
    }
    return parts.join(QStringLiteral("\n\n---\n\n")) + QLatin1Char('\n');
}

QString mutedStyle()
{
    return QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::mutedFg());
}
} // namespace

const QVector<ManifestoDialog::Source> &ManifestoDialog::sources()
{
    // O do Kai leva junto a lista de ícones: o manifesto manda usar só nomes dela.
    static const QVector<Source> list = {
        {QStringLiteral("kai"), {QStringLiteral("kai-manifesto.md"), QStringLiteral("kai-icons.md")}},
        {QStringLiteral("kip"), {QStringLiteral("kip-manifesto.md")}},
    };
    return list;
}

ManifestoDialog::ManifestoDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(utils::tr(QStringLiteral("manifesto.title")));
    setSizeGripEnabled(true);
    resize(940, 700);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(tk::space(4), tk::space(3), tk::space(4), tk::space(3));
    root->setSpacing(tk::space(3));

    auto *intro = new QLabel(utils::tr(QStringLiteral("manifesto.intro")), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    for (const Source &source : sources()) {
        const bool kip = source.id == QLatin1String("kip");
        auto *page = new QWidget(m_tabs);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(tk::space(3), tk::space(3), tk::space(3), tk::space(3));
        pageLayout->setSpacing(tk::space(2));

        auto *description = new QLabel(utils::tr(kip ? QStringLiteral("manifesto.kip.desc")
                                                     : QStringLiteral("manifesto.kai.desc")), page);
        description->setWordWrap(true);
        description->setStyleSheet(mutedStyle());
        pageLayout->addWidget(description);

        auto *button = new QPushButton(utils::tr(QStringLiteral("manifesto.copy")), page);
        button->setProperty("kaiRole", QStringLiteral("primary"));
        button->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::buttonFg()), 16));
        button->setCursor(Qt::PointingHandCursor);
        button->setEnabled(false); // habilita quando a leitura em segundo plano termina
        auto *status = new QLabel(utils::tr(QStringLiteral("manifesto.loading")), page);
        status->setWordWrap(true);
        status->setStyleSheet(mutedStyle());
        auto *row = new QHBoxLayout();
        row->setSpacing(tk::space(3));
        row->addWidget(button, 0, Qt::AlignLeft);
        row->addWidget(status, 1);
        pageLayout->addLayout(row);

        auto *preview = new QPlainTextEdit(page);
        preview->setReadOnly(true);
        preview->setLineWrapMode(QPlainTextEdit::NoWrap);
        preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        preview->setPlaceholderText(utils::tr(QStringLiteral("manifesto.loading")));
        pageLayout->addWidget(preview, 1);

        m_tabs->addTab(page, utils::tr(kip ? QStringLiteral("manifesto.tab.kip") : QStringLiteral("manifesto.tab.kai")));

        m_buttons.insert(source.id, button);
        m_status.insert(source.id, status);
        m_previews.insert(source.id, preview);
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(2500);
        connect(timer, &QTimer::timeout, this, [this, id = source.id]() {
            m_buttons.value(id)->setText(utils::tr(QStringLiteral("manifesto.copy")));
            m_status.value(id)->setText(utils::tr(QStringLiteral("manifesto.preview_hint")));
        });
        m_resetTimers.insert(source.id, timer);
        connect(button, &QPushButton::clicked, this, [this, id = source.id]() { copyManifesto(id); });
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    stripDialogButtonIcons(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);

    centerOnParent(this);
    for (const Source &source : sources()) {
        load(source.id);
    }
}

void ManifestoDialog::load(const QString &id)
{
    const Source *source = nullptr;
    for (const Source &s : sources()) {
        if (s.id == id) source = &s;
    }
    if (!source) {
        return;
    }
    QPointer<ManifestoDialog> self(this);
    const QStringList files = source->files;
    QThreadPool::globalInstance()->start([self, id, files]() {
        const QString text = readManifesto(files);
        QMetaObject::invokeMethod(qApp, [self, id, text]() {
            if (self) {
                self->handleLoaded(id, text);
            }
        }, Qt::QueuedConnection);
    });
}

void ManifestoDialog::handleLoaded(const QString &id, const QString &text)
{
    const bool ok = !text.isEmpty();
    QLabel *status = m_status.value(id);
    if (ok) {
        m_texts.insert(id, text);
        m_previews.value(id)->setPlainText(text);
        m_buttons.value(id)->setEnabled(true);
        status->setText(utils::tr(QStringLiteral("manifesto.preview_hint")));
        status->setStyleSheet(mutedStyle());
    } else {
        status->setText(utils::tr(QStringLiteral("manifesto.missing")));
        status->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::errorFg()));
    }
    emit loaded(id, ok);
}

void ManifestoDialog::copyManifesto(const QString &id)
{
    QPushButton *button = m_buttons.value(id);
    const QString text = m_texts.value(id);
    if (!button || text.isEmpty()) {
        emit copyFinished(id, false);
        return;
    }
    QApplication::clipboard()->setText(text);
    button->setText(utils::tr(QStringLiteral("manifesto.copied")));
    QLabel *status = m_status.value(id);
    status->setText(utils::tr(QStringLiteral("manifesto.copied_detail")).arg(text.size() / 1024));
    status->setStyleSheet(mutedStyle());
    m_resetTimers.value(id)->start();
    emit copyFinished(id, true);
}

} // namespace kai::ui
