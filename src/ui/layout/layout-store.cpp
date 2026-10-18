#include "ui/layout/layout-store.h"

#include "utils/logger.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace kai::ui::layout {

namespace {
const QString kLogTag = QStringLiteral("Layout");
constexpr int kSaveDelayMs = 400;

bool writeAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(bytes) == bytes.size() && file.commit();
}
} // namespace

LayoutStore::LayoutStore(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_path(filePath)
{
    m_pool.setMaxThreadCount(1); // as gravações nunca disputam o arquivo
    m_timer.setSingleShot(true);
    m_timer.setInterval(kSaveDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &LayoutStore::startWrite);
}

LayoutStore::~LayoutStore()
{
    m_timer.stop();
    m_pool.waitForDone();
    if (m_pending && !m_path.isEmpty()) {
        writeAtomic(m_path, serialize(m_pendingLayout, m_pendingPerspectives));
    }
}

QByteArray LayoutStore::serialize(const LayoutModel &layout, const QMap<QString, LayoutModel> &perspectives)
{
    QJsonObject root = layout.toJson();
    QJsonObject saved;
    for (auto it = perspectives.constBegin(); it != perspectives.constEnd(); ++it) {
        saved.insert(it.key(), it.value().toJson());
    }
    root.insert(QStringLiteral("perspectives"), saved);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

LayoutStore::Loaded LayoutStore::load(const LayoutModel::Validator &isKnown, const std::function<LayoutModel()> &legacy)
{
    Loaded result;
    result.layout = LayoutModel::defaultLayout();
    const auto fallback = [&]() {
        if (legacy) {
            result.layout = legacy();
            QStringList problems;
            result.layout.repair(isKnown, &problems);
        }
    };

    if (m_path.isEmpty() || !QFile::exists(m_path)) {
        fallback(); // primeira execução com layout flexível: migra o que as configurações antigas diziam
        return result;
    }
    result.fileExisted = true;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        utils::Logger::warning(kLogTag, QStringLiteral("Não foi possível abrir '%1': layout padrão.").arg(m_path));
        return result;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString backup = m_path + QStringLiteral(".corrupt-")
                               + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::rename(m_path, backup);
        utils::Logger::error(kLogTag, QStringLiteral("layout.json ilegível (%1); guardado em '%2'. Usando o layout padrão.")
                                          .arg(error.errorString(), backup));
        result.recovered = true;
        return result;
    }

    const QJsonObject root = doc.object();
    QStringList problems;
    result.layout = LayoutModel::fromJson(root, isKnown, &problems);
    const QJsonObject saved = root.value(QStringLiteral("perspectives")).toObject();
    for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
        if (!it.key().trimmed().isEmpty() && it.value().isObject()) {
            result.perspectives.insert(it.key(), LayoutModel::fromJson(it.value().toObject(), isKnown));
        }
    }
    for (const QString &problem : std::as_const(problems)) {
        utils::Logger::warning(kLogTag, QStringLiteral("layout.json consertado: %1").arg(problem));
    }
    return result;
}

void LayoutStore::scheduleSave(const LayoutModel &layout, const QMap<QString, LayoutModel> &perspectives)
{
    m_pendingLayout = layout;
    m_pendingPerspectives = perspectives;
    m_pending = true;
    if (!m_path.isEmpty()) {
        m_timer.start();
    }
}

void LayoutStore::startWrite()
{
    if (!m_pending || m_path.isEmpty()) {
        return;
    }
    const QByteArray bytes = serialize(m_pendingLayout, m_pendingPerspectives);
    m_pending = false;
    const QString path = m_path;
    m_pool.start([path, bytes]() {
        if (!writeAtomic(path, bytes)) {
            utils::Logger::error(kLogTag, QStringLiteral("Não foi possível gravar '%1'.").arg(path));
        }
    });
}

void LayoutStore::flush()
{
    m_timer.stop();
    m_pool.waitForDone();
    if (m_pending && !m_path.isEmpty()) {
        writeAtomic(m_path, serialize(m_pendingLayout, m_pendingPerspectives));
        m_pending = false;
    }
}

} // namespace kai::ui::layout
