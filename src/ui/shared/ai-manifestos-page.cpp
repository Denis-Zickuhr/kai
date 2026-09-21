#include "ui/shared/ai-manifestos-page.h"

#include "ui/shared/dialog-utils.h"
#include "ui/shared/lucide-icons.h"
#include "utils/asset-paths.h"
#include "utils/design-tokens.h"
#include "utils/translation-manager.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
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
} // namespace

const QVector<AiManifestosPage::Source> &AiManifestosPage::sources()
{
    // O do kai.json leva junto a lista de ícones: o manifesto manda usar só nomes dela.
    static const QVector<Source> list = {
        {QStringLiteral("kai_json"), {QStringLiteral("kai-json-manifesto.md"), QStringLiteral("kai-icons.md")}},
        {QStringLiteral("kip"), {QStringLiteral("kip-manifesto.md")}},
    };
    return list;
}

AiManifestosPage::AiManifestosPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(tk::space(4), tk::space(3), tk::space(4), tk::space(3));
    root->setSpacing(tk::space(3));

    auto *title = new QLabel(utils::tr(QStringLiteral("help.topic.ai_manifestos.title")), this);
    title->setProperty("kaiRole", QStringLiteral("title"));
    root->addWidget(title);
    auto *intro = new QLabel(utils::tr(QStringLiteral("help.manifestos.intro")), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    for (const Source &source : sources()) {
        const bool kip = source.id == QLatin1String("kip");
        auto *card = layout_helpers::makeSurfaceCard(this);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(tk::space(4), tk::space(3), tk::space(4), tk::space(3));
        cardLayout->setSpacing(tk::space(2));

        auto *name = new QLabel(utils::tr(kip ? QStringLiteral("help.manifestos.kip.title")
                                              : QStringLiteral("help.manifestos.kai.title")), card);
        name->setStyleSheet(QStringLiteral("font-weight: 600; background: transparent; border: none;"));
        cardLayout->addWidget(name);
        auto *description = new QLabel(utils::tr(kip ? QStringLiteral("help.manifestos.kip.desc")
                                                     : QStringLiteral("help.manifestos.kai.desc")), card);
        description->setWordWrap(true);
        description->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::mutedFg()));
        cardLayout->addWidget(description);

        auto *button = new QPushButton(utils::tr(QStringLiteral("help.manifestos.copy")), card);
        button->setProperty("kaiRole", QStringLiteral("primary"));
        button->setIcon(LucideIcons::icon(QStringLiteral("copy"), QColor(tk::buttonFg()), 16));
        button->setCursor(Qt::PointingHandCursor);
        auto *status = new QLabel(card);
        status->setWordWrap(true);
        status->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(tk::mutedFg()));
        status->setVisible(false);
        auto *row = new QHBoxLayout();
        row->setSpacing(tk::space(3));
        row->addWidget(button, 0, Qt::AlignLeft);
        row->addWidget(status, 1);
        cardLayout->addLayout(row);
        root->addWidget(card);

        m_buttons.insert(source.id, button);
        m_status.insert(source.id, status);
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(2500);
        connect(timer, &QTimer::timeout, this, [this, id = source.id]() {
            m_buttons.value(id)->setText(utils::tr(QStringLiteral("help.manifestos.copy")));
            m_status.value(id)->setVisible(false);
        });
        m_resetTimers.insert(source.id, timer);
        connect(button, &QPushButton::clicked, this, [this, id = source.id]() { copyManifesto(id); });
    }
    root->addStretch(1);
}

void AiManifestosPage::copyManifesto(const QString &id)
{
    const Source *source = nullptr;
    for (const Source &s : sources()) {
        if (s.id == id) source = &s;
    }
    QPushButton *button = m_buttons.value(id);
    if (!source || !button || !button->isEnabled()) {
        return;
    }
    button->setEnabled(false);
    button->setText(utils::tr(QStringLiteral("help.manifestos.copying")));

    QPointer<AiManifestosPage> self(this);
    const QStringList files = source->files;
    QThreadPool::globalInstance()->start([self, id, files]() {
        const QString text = readManifesto(files);
        QMetaObject::invokeMethod(qApp, [self, id, text]() {
            if (!self) {
                return;
            }
            QPushButton *b = self->m_buttons.value(id);
            QLabel *status = self->m_status.value(id);
            const bool ok = !text.isEmpty();
            if (ok) {
                QApplication::clipboard()->setText(text);
            }
            b->setEnabled(true);
            b->setText(ok ? utils::tr(QStringLiteral("help.manifestos.copied")) : utils::tr(QStringLiteral("help.manifestos.copy")));
            status->setText(ok ? utils::tr(QStringLiteral("help.manifestos.copied_detail")).arg(text.size() / 1024)
                               : utils::tr(QStringLiteral("help.manifestos.missing")));
            status->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;")
                                      .arg(ok ? tk::mutedFg() : tk::errorFg()));
            status->setVisible(true);
            self->m_resetTimers.value(id)->start();
            emit self->copyFinished(id, ok);
        }, Qt::QueuedConnection);
    });
}

} // namespace kai::ui
