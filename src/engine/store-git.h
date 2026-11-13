#pragma once

#include <functional>

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace kai::engine {

// Kai Store: traz um repositório Git para uma pasta (checkout raso), sem bloquear a GUI. Serve ao provedor `repo`
// quando o "raw" não está disponível e à instalação de projetos por referência (`source.clone`).
// Só HTTPS, file:// e caminho local; nunca pergunta senha; ext::/ssh/git:// são recusados.
class StoreGitCheckout : public QObject {
    Q_OBJECT

public:
    explicit StoreGitCheckout(QObject *parent = nullptr);
    ~StoreGitCheckout() override;

    // `ref`: tag, branch ou commit (vazio = HEAD). `destDir` pode já conter um checkout anterior (é atualizado).
    // `error` vazio = sucesso. Códigos: "git_missing", "bad_url", "git_failed:<stderr>", "cancelled", "timeout".
    void checkout(const QString &url, const QString &ref, const QString &destDir,
                  std::function<void(const QString &error)> done);
    void cancel();

    static bool isAllowedRepoUrl(const QString &url);
    static QString gitExecutable();

private:
    void runNext();
    void finish(const QString &error);

    QList<QStringList> m_steps;
    int m_index = 0;
    QString m_dir;
    QProcess *m_process = nullptr;
    std::function<void(const QString &)> m_done;
    bool m_cancelled = false;
    bool m_finished = true;
};

} // namespace kai::engine
