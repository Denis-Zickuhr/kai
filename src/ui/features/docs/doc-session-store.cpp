#include "ui/features/docs/doc-session-store.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <utility>

namespace kai::ui {

namespace {
constexpr int kVersion = 1;
constexpr int kSaveDelayMs = 1200;

bool writeAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(bytes) == bytes.size() && file.commit();
}
} // namespace

DocSessionStore::DocSessionStore(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_path(filePath)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &DocSessionStore::startSave);
}

DocSessionStore::~DocSessionStore()
{
    m_saveTimer.stop();
    // Espera uma gravação em andamento (senão duas escritas disputariam o arquivo) e grava o que ficou pendente.
    for (QThread *thread : findChildren<QThread *>()) {
        thread->wait();
    }
    if (m_dirty && m_loaded && !m_path.isEmpty()) {
        writeAtomic(m_path, serialize());
    }
}

// --------------------------------------------------------------------------------------------------------- carregar

void DocSessionStore::load()
{
    if (m_loaded || m_loading) {
        return;
    }
    if (m_path.isEmpty()) {
        m_loaded = true;
        QTimer::singleShot(0, this, [this]() {
            const auto waiting = std::exchange(m_waiting, {});
            emit loaded();
            for (const auto &action : waiting) {
                action();
            }
        });
        return;
    }
    m_loading = true;
    QPointer<DocSessionStore> guard(this);
    const QString path = m_path;
    auto *thread = QThread::create([guard, path]() {
        QByteArray bytes;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            bytes = file.readAll();
        }
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, bytes]() {
            if (!guard) {
                return;
            }
            guard->merge(bytes);
            guard->m_loading = false;
            guard->m_loaded = true;
            const auto waiting = std::exchange(guard->m_waiting, {});
            emit guard->loaded();
            emit guard->draftsChanged();
            for (const auto &action : waiting) {
                action();
            }
        }, Qt::QueuedConnection);
    });
    thread->setParent(this);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocSessionStore::whenLoaded(std::function<void()> action)
{
    if (m_loaded) {
        action();
        return;
    }
    m_waiting.append(std::move(action));
    load();
}

void DocSessionStore::merge(const QByteArray &bytes)
{
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    if (root.value(QStringLiteral("version")).toInt() != kVersion) {
        return; // ausente, corrompido ou de outra versão: começa limpo (a memória do leitor nunca vale um erro)
    }
    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (m_files.contains(it.key())) {
            continue; // o que já foi mexido nesta sessão vale mais que o arquivo
        }
        const QJsonObject o = it.value().toObject();
        FileState s;
        s.scroll = o.value(QStringLiteral("scroll")).toInt();
        s.cursor = o.value(QStringLiteral("cursor")).toInt();
        s.editing = o.value(QStringLiteral("editing")).toBool();
        s.hasDraft = o.contains(QStringLiteral("draft"));
        s.draft = o.value(QStringLiteral("draft")).toString();
        s.baseHash = o.value(QStringLiteral("base")).toInt();
        s.scope = o.value(QStringLiteral("scope")).toString();
        s.touched = static_cast<qint64>(o.value(QStringLiteral("touched")).toDouble());
        m_files.insert(it.key(), s);
    }
    const QJsonObject last = root.value(QStringLiteral("last")).toObject();
    for (auto it = last.constBegin(); it != last.constEnd(); ++it) {
        if (!m_last.contains(it.key())) {
            m_last.insert(it.key(), it.value().toString());
        }
    }
    const QJsonObject recent = root.value(QStringLiteral("recent")).toObject();
    for (auto it = recent.constBegin(); it != recent.constEnd(); ++it) {
        if (m_recent.contains(it.key())) {
            continue;
        }
        QStringList list;
        for (const QJsonValue &v : it.value().toArray()) {
            list << v.toString();
        }
        m_recent.insert(it.key(), list);
    }
}

// ---------------------------------------------------------------------------------------------------------- estado

DocSessionStore::FileState DocSessionStore::state(const QString &key) const
{
    return m_files.value(key);
}

void DocSessionStore::setState(const QString &key, const FileState &state)
{
    if (key.isEmpty()) {
        return;
    }
    const bool hadDraft = hasDraft(key);
    FileState next = state;
    if (next.hasDraft && next.draft.size() > kMaxDraftChars) {
        next.draft.clear(); // grande demais para guardar: o editor continua com o texto, mas ele não sobrevive a trocar
        next.hasDraft = false;
    }
    if (!next.hasDraft) {
        next.draft.clear();
        next.baseHash = 0;
    }
    next.touched = QDateTime::currentMSecsSinceEpoch();
    const auto existing = m_files.constFind(key);
    const bool changed = existing == m_files.constEnd() || existing->scroll != next.scroll || existing->cursor != next.cursor
        || existing->editing != next.editing || existing->hasDraft != next.hasDraft || existing->draft != next.draft
        || existing->scope != next.scope;
    m_files.insert(key, next);
    prune();
    if (changed) {
        scheduleSave();
    }
    if (hadDraft != next.hasDraft) {
        emit draftsChanged();
    }
}

void DocSessionStore::clearDraft(const QString &key)
{
    auto it = m_files.find(key);
    if (it == m_files.end() || !it->hasDraft) {
        return;
    }
    it->hasDraft = false;
    it->draft.clear();
    it->baseHash = 0;
    scheduleSave();
    emit draftsChanged();
}

bool DocSessionStore::hasDraft(const QString &key) const
{
    const auto it = m_files.constFind(key);
    return it != m_files.constEnd() && it->hasDraft;
}

QStringList DocSessionStore::draftKeys() const
{
    QStringList keys;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (it->hasDraft) {
            keys << it.key();
        }
    }
    keys.sort();
    return keys;
}

QSet<QString> DocSessionStore::draftScopes() const
{
    QSet<QString> scopes;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (it->hasDraft && !it->scope.isEmpty()) {
            scopes.insert(it->scope);
        }
    }
    return scopes;
}

QSet<QString> DocSessionStore::draftNoteIds() const
{
    QSet<QString> ids;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (it->hasDraft && it.key().startsWith(QLatin1String("note:"))) {
            ids.insert(it.key().mid(5));
        }
    }
    return ids;
}

void DocSessionStore::noteOpened(const QString &scope, const QString &key)
{
    if (scope.isEmpty() || key.isEmpty()) {
        return;
    }
    QStringList &list = m_recent[scope];
    if (m_last.value(scope) == key && !list.isEmpty() && list.first() == key) {
        return;
    }
    m_last[scope] = key;
    list.removeAll(key);
    list.prepend(key);
    while (list.size() > kMaxRecent) {
        list.removeLast();
    }
    scheduleSave();
}

QString DocSessionStore::lastOpened(const QString &scope) const
{
    return m_last.value(scope);
}

QStringList DocSessionStore::recent(const QString &scope) const
{
    return m_recent.value(scope);
}

void DocSessionStore::forget(const QString &key)
{
    const bool hadDraft = hasDraft(key);
    bool changed = m_files.remove(key) > 0;
    for (auto it = m_recent.begin(); it != m_recent.end(); ++it) {
        changed = it->removeAll(key) > 0 || changed;
    }
    for (auto it = m_last.begin(); it != m_last.end();) {
        if (it.value() == key) {
            it = m_last.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        scheduleSave();
    }
    if (hadDraft) {
        emit draftsChanged();
    }
}

// Mantém o arquivo pequeno: passando do teto, saem os mais antigos que NÃO têm rascunho.
void DocSessionStore::prune()
{
    if (m_files.size() <= kMaxEntries) {
        return;
    }
    QVector<QPair<qint64, QString>> candidates;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (!it->hasDraft) {
            candidates.append({it->touched, it.key()});
        }
    }
    std::sort(candidates.begin(), candidates.end());
    int excess = int(m_files.size()) - kMaxEntries;
    for (const auto &entry : std::as_const(candidates)) {
        if (excess-- <= 0) {
            break;
        }
        m_files.remove(entry.second);
    }
}

// --------------------------------------------------------------------------------------------------------- gravar

void DocSessionStore::scheduleSave()
{
    m_dirty = true;
    if (!m_path.isEmpty()) {
        m_saveTimer.start();
    }
}

QByteArray DocSessionStore::serialize() const
{
    QJsonObject files;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("scroll"), it->scroll);
        o.insert(QStringLiteral("cursor"), it->cursor);
        o.insert(QStringLiteral("editing"), it->editing);
        o.insert(QStringLiteral("scope"), it->scope);
        o.insert(QStringLiteral("touched"), static_cast<double>(it->touched));
        if (it->hasDraft) {
            o.insert(QStringLiteral("draft"), it->draft);
            o.insert(QStringLiteral("base"), it->baseHash);
        }
        files.insert(it.key(), o);
    }
    QJsonObject last;
    for (auto it = m_last.constBegin(); it != m_last.constEnd(); ++it) {
        last.insert(it.key(), it.value());
    }
    QJsonObject recent;
    for (auto it = m_recent.constBegin(); it != m_recent.constEnd(); ++it) {
        recent.insert(it.key(), QJsonArray::fromStringList(it.value()));
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("last"), last);
    root.insert(QStringLiteral("recent"), recent);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void DocSessionStore::startSave()
{
    if (m_path.isEmpty() || !m_dirty) {
        return;
    }
    if (m_saving) {
        m_saveTimer.start(); // uma gravação ainda corre: tenta de novo depois
        return;
    }
    // Ainda carregando: gravar agora sobrescreveria o arquivo com o que ainda não foi lido.
    if (!m_loaded) {
        m_saveTimer.start();
        return;
    }
    m_dirty = false;
    m_saving = true;
    const QByteArray bytes = serialize();
    const QString path = m_path;
    QPointer<DocSessionStore> guard(this);
    auto *thread = QThread::create([guard, path, bytes]() {
        const bool ok = writeAtomic(path, bytes);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, ok]() {
            if (!guard) {
                return;
            }
            guard->m_saving = false;
            if (!ok) {
                guard->m_dirty = true; // tenta de novo no próximo ciclo de gravação
            }
        }, Qt::QueuedConnection);
    });
    thread->setParent(this);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DocSessionStore::flush()
{
    m_saveTimer.stop();
    if (m_path.isEmpty()) {
        return;
    }
    for (QThread *thread : findChildren<QThread *>()) {
        thread->wait();
    }
    if (!m_loaded) {
        return; // nada foi lido ainda: não sobrescreve o arquivo com um estado vazio
    }
    if (writeAtomic(m_path, serialize())) {
        m_dirty = false;
    }
}

} // namespace kai::ui
