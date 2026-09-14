#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace kai::ui {

// A memória do leitor de documentos: onde o usuário estava em cada arquivo (rolagem, cursor, se estava editando), o texto
// ainda não salvo de cada um (rascunho), o último arquivo aberto de cada pasta e os recentes. Vive FORA do leitor, que é
// esvaziado a cada troca de seleção — por isso trocar de arquivo, de pasta ou rodar um comando não faz o Kai esquecer.
//
// Chaves: o caminho limpo do arquivo, ou `note:<id>` para uma nota. `scope` é o id da pasta do Kai onde o documento foi
// aberto (agrupa os recentes e diz onde mostrar a marca de "alterações não salvas" na árvore).
//
// Persistido em doc-state.json: a leitura e a escrita rodam em thread de trabalho (nunca na interface). Só o destrutor
// grava de forma síncrona, para não perder o rascunho ao fechar o app.
class DocSessionStore : public QObject {
    Q_OBJECT

public:
    struct FileState {
        int scroll = 0;       // rolagem vertical (leitura ou edição)
        int cursor = 0;       // posição do cursor no editor
        bool editing = false; // estava no modo de edição
        bool hasDraft = false;
        QString draft;        // texto do editor com alterações não salvas
        int baseHash = 0;     // hash do texto do arquivo sobre o qual o rascunho foi escrito
        QString scope;
        qint64 touched = 0;   // ms desde a época do último uso
    };

    // `filePath` vazio = só em memória (testes).
    explicit DocSessionStore(const QString &filePath, QObject *parent = nullptr);
    ~DocSessionStore() override;

    // Lê o arquivo em segundo plano; `loaded()` ao terminar. Sem arquivo, `loaded()` sai no próximo ciclo.
    void load();
    bool isLoaded() const { return m_loaded; }
    // Roda `action` já (se carregado) ou assim que o carregamento terminar.
    void whenLoaded(std::function<void()> action);

    FileState state(const QString &key) const;
    bool hasState(const QString &key) const { return m_files.contains(key); }
    void setState(const QString &key, const FileState &state);
    void clearDraft(const QString &key);
    bool hasDraft(const QString &key) const;
    QStringList draftKeys() const;
    // Pastas (scope) que têm ao menos um documento com alterações não salvas, e as notas nessa situação.
    QSet<QString> draftScopes() const;
    QSet<QString> draftNoteIds() const;

    // Registra que `key` foi mostrado em `scope`: vira o último da pasta e sobe nos recentes.
    void noteOpened(const QString &scope, const QString &key);
    QString lastOpened(const QString &scope) const;
    QStringList recent(const QString &scope) const;
    // O arquivo/nota deixou de existir.
    void forget(const QString &key);

    // Grava agora, de forma síncrona (fim do app).
    void flush();

    static constexpr int kMaxRecent = 12;
    static constexpr int kMaxEntries = 300;
    static constexpr int kMaxDraftChars = 2 * 1024 * 1024;

signals:
    void loaded();
    // Mudou o conjunto de documentos com alterações não salvas.
    void draftsChanged();

private:
    void scheduleSave();
    void startSave();
    QByteArray serialize() const;
    void merge(const QByteArray &bytes);
    void prune();

    QString m_path;
    QHash<QString, FileState> m_files;
    QHash<QString, QString> m_last;
    QHash<QString, QStringList> m_recent;
    QTimer m_saveTimer;
    bool m_loaded = false;
    bool m_loading = false;
    bool m_saving = false;
    bool m_dirty = false;
    QVector<std::function<void()>> m_waiting;
};

} // namespace kai::ui
