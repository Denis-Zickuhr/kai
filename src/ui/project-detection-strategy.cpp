#include "ui/project-detection-strategy.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <optional>

namespace kai::ui {

namespace {

// Helper comum às strategies baseadas em "scripts" de um JSON simples
// (package.json/composer.json): 1 Command Shell por script declarado.
QVector<DetectedCommand> commandsFromScriptsObject(const QJsonObject &scripts,
                                                    const QString &groupName,
                                                    const QString &runnerPrefix)
{
    QVector<DetectedCommand> out;
    for (auto it = scripts.constBegin(); it != scripts.constEnd(); ++it) {
        if (it.key().trimmed().isEmpty()) {
            continue;
        }
        core::Command cmd;
        cmd.name = it.key();
        cmd.type = core::CommandType::Shell;
        cmd.command = QStringLiteral("%1 %2").arg(runnerPrefix, it.key());
        out.append(DetectedCommand{groupName, cmd});
    }
    return out;
}

QJsonObject readJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray raw = file.readAll();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

// --- npm/Node (package.json) ---
// "scripts" vira 1 comando por entrada. Gerenciador detectado pelo lockfile
// presente (pnpm-lock.yaml/yarn.lock) — sem lockfile, assume npm (o mais
// comum/universal). Best-effort: um "scripts" ausente ou malformado
// simplesmente não gera nenhum comando, nunca quebra a importação.
class NpmDetectionStrategy : public ProjectDetectionStrategy {
public:
    QString name() const override { return QStringLiteral("npm"); }

    bool appliesTo(const QDir &projectDir) const override
    {
        return projectDir.exists(QStringLiteral("package.json"));
    }

    QVector<DetectedCommand> detect(const QDir &projectDir) const override
    {
        const QJsonObject pkg = readJsonObject(projectDir.filePath(QStringLiteral("package.json")));
        const QJsonObject scripts = pkg.value(QStringLiteral("scripts")).toObject();
        if (scripts.isEmpty()) {
            return {};
        }
        QString runner = QStringLiteral("npm run");
        if (projectDir.exists(QStringLiteral("pnpm-lock.yaml"))) {
            runner = QStringLiteral("pnpm run");
        } else if (projectDir.exists(QStringLiteral("yarn.lock"))) {
            runner = QStringLiteral("yarn run");
        }
        return commandsFromScriptsObject(scripts, name(), runner);
    }
};

// --- Docker Compose ---
// Parser TEXTUAL mínimo (não é um parser YAML completo — Kai não tem
// dependência de YAML): reconhece a forma comum de docker-compose.yml
//   services:
//     web:
//       ...
//     db:
//       ...
// pela indentação do primeiro nome logo abaixo de "services:". Arquivos com
// âncoras/merge keys/estruturas incomuns simplesmente não são reconhecidos
// (lista de serviços vazia) em vez de tentar adivinhar errado.
class DockerComposeDetectionStrategy : public ProjectDetectionStrategy {
public:
    QString name() const override { return QStringLiteral("Docker Compose"); }

    bool appliesTo(const QDir &projectDir) const override
    {
        return composeFilePath(projectDir).has_value();
    }

    QVector<DetectedCommand> detect(const QDir &projectDir) const override
    {
        const auto path = composeFilePath(projectDir);
        if (!path.has_value()) {
            return {};
        }
        const QStringList services = parseServiceNames(readTextFile(path.value()));
        if (services.isEmpty()) {
            return {};
        }

        QVector<DetectedCommand> out;
        core::Command up;
        up.name = QStringLiteral("Subir tudo (docker compose up)");
        up.type = core::CommandType::Shell;
        up.command = QStringLiteral("docker compose up -d");
        up.isBackground = true; // sobe serviços de longa duração, não bloqueia
        out.append(DetectedCommand{name(), up});

        core::Command down;
        down.name = QStringLiteral("Parar tudo (docker compose down)");
        down.type = core::CommandType::Shell;
        down.command = QStringLiteral("docker compose down");
        out.append(DetectedCommand{name(), down});

        for (const QString &service : services) {
            core::Command logs;
            logs.name = QStringLiteral("Ver logs — %1").arg(service);
            logs.type = core::CommandType::Shell;
            logs.command = QStringLiteral("docker compose logs -f %1").arg(service);
            logs.interactiveTerminal = true; // stream contínuo de logs
            out.append(DetectedCommand{name(), logs});
        }
        return out;
    }

private:
    static std::optional<QString> composeFilePath(const QDir &projectDir)
    {
        static const QStringList kCandidates = {
            QStringLiteral("docker-compose.yml"), QStringLiteral("docker-compose.yaml"),
            QStringLiteral("compose.yml"), QStringLiteral("compose.yaml"),
        };
        for (const QString &candidate : kCandidates) {
            if (projectDir.exists(candidate)) {
                return projectDir.filePath(candidate);
            }
        }
        return std::nullopt;
    }

    static QStringList parseServiceNames(const QString &content)
    {
        QStringList services;
        const QStringList lines = content.split(QLatin1Char('\n'));
        int serviceIndent = -1; // indentação (nº de espaços) do 1º serviço encontrado
        bool inServices = false;
        static const QRegularExpression servicesHeader(QStringLiteral(R"(^services:\s*$)"));
        static const QRegularExpression serviceEntry(QStringLiteral(R"(^(\s+)([A-Za-z0-9_.-]+):(\s*(#.*)?)$)"));

        for (const QString &rawLine : lines) {
            if (!inServices) {
                if (servicesHeader.match(rawLine).hasMatch()) {
                    inServices = true;
                }
                continue;
            }
            if (rawLine.trimmed().isEmpty() || rawLine.trimmed().startsWith(QLatin1Char('#'))) {
                continue;
            }
            const QRegularExpressionMatch m = serviceEntry.match(rawLine);
            if (!m.hasMatch()) {
                // Linha sem a indentação/forma esperada de nome de serviço:
                // se veio numa indentação MENOR que a do 1º serviço (ou 0),
                // o bloco "services:" acabou.
                static const QRegularExpression firstNonSpace(QStringLiteral("\\S"));
                const int found = rawLine.indexOf(firstNonSpace);
                const int indent = found >= 0 ? found : 0;
                if (serviceIndent >= 0 && indent < serviceIndent) {
                    break;
                }
                continue; // linha de configuração DENTRO de um serviço (ex: "image: ...")
            }
            const int indent = static_cast<int>(m.captured(1).size());
            if (serviceIndent < 0) {
                serviceIndent = indent;
            } else if (indent > serviceIndent) {
                continue; // chave aninhada dentro do serviço (ex: "environment:"), não é serviço
            } else if (indent < serviceIndent) {
                break; // saiu do bloco services
            }
            services << m.captured(2);
        }
        return services;
    }
};

// --- Python ---
// Sem dependência de parser TOML: pyproject.toml/Pipfile são detectados só
// pela EXISTÊNCIA (não pelo conteúdo) — o suficiente para saber "instale as
// dependências com a ferramenta X", sem arriscar adivinhar errado um
// entrypoint que só um parser TOML de verdade revelaria com segurança.
class PythonDetectionStrategy : public ProjectDetectionStrategy {
public:
    QString name() const override { return QStringLiteral("Python"); }

    bool appliesTo(const QDir &projectDir) const override
    {
        return projectDir.exists(QStringLiteral("requirements.txt"))
            || projectDir.exists(QStringLiteral("pyproject.toml"))
            || projectDir.exists(QStringLiteral("Pipfile"))
            || projectDir.exists(QStringLiteral("manage.py"))
            || projectDir.exists(QStringLiteral("main.py"))
            || projectDir.exists(QStringLiteral("app.py"));
    }

    QVector<DetectedCommand> detect(const QDir &projectDir) const override
    {
        QVector<DetectedCommand> out;
        auto add = [&](const QString &cmdName, const QString &script) {
            core::Command cmd;
            cmd.name = cmdName;
            cmd.type = core::CommandType::Shell;
            cmd.command = script;
            out.append(DetectedCommand{name(), cmd});
        };

        if (projectDir.exists(QStringLiteral("requirements.txt"))) {
            add(QStringLiteral("Instalar dependências (pip)"), QStringLiteral("pip install -r requirements.txt"));
        }
        if (projectDir.exists(QStringLiteral("Pipfile"))) {
            add(QStringLiteral("Instalar dependências (Pipenv)"), QStringLiteral("pipenv install"));
        }
        if (projectDir.exists(QStringLiteral("pyproject.toml"))) {
            if (projectDir.exists(QStringLiteral("poetry.lock"))) {
                add(QStringLiteral("Instalar dependências (Poetry)"), QStringLiteral("poetry install"));
            } else {
                add(QStringLiteral("Instalar dependências (pip)"), QStringLiteral("pip install ."));
            }
        }
        if (projectDir.exists(QStringLiteral("manage.py"))) {
            add(QStringLiteral("Rodar servidor (Django)"), QStringLiteral("python manage.py runserver"));
            add(QStringLiteral("Migrar banco (Django)"), QStringLiteral("python manage.py migrate"));
        } else if (projectDir.exists(QStringLiteral("main.py"))) {
            add(QStringLiteral("Rodar (main.py)"), QStringLiteral("python main.py"));
        } else if (projectDir.exists(QStringLiteral("app.py"))) {
            add(QStringLiteral("Rodar (app.py)"), QStringLiteral("python app.py"));
        }
        return out;
    }
};

// --- PHP / Composer ---
// "scripts" de composer.json, mesmo espírito do npm — mais um comando fixo
// de "composer install" (útil mesmo sem nenhum script customizado).
class ComposerDetectionStrategy : public ProjectDetectionStrategy {
public:
    QString name() const override { return QStringLiteral("Composer"); }

    bool appliesTo(const QDir &projectDir) const override
    {
        return projectDir.exists(QStringLiteral("composer.json"));
    }

    QVector<DetectedCommand> detect(const QDir &projectDir) const override
    {
        QVector<DetectedCommand> out;
        core::Command install;
        install.name = QStringLiteral("Instalar dependências (Composer)");
        install.type = core::CommandType::Shell;
        install.command = QStringLiteral("composer install");
        out.append(DetectedCommand{name(), install});

        const QJsonObject pkg = readJsonObject(projectDir.filePath(QStringLiteral("composer.json")));
        const QJsonObject scripts = pkg.value(QStringLiteral("scripts")).toObject();
        out += commandsFromScriptsObject(scripts, name(), QStringLiteral("composer run-script"));
        return out;
    }
};

// --- Makefile ---
// Reconhece targets pela forma "nome:" no início da linha (sem indentação —
// receitas de comando vêm indentadas com TAB, então já ficam de fora
// naturalmente). Exclui variáveis ("NOME := valor"/"NOME = valor", onde o
// ':' vem seguido de '=') e targets especiais que começam com '.'
// (.PHONY, .DEFAULT_GOAL...) e padrões com '%' (regras implícitas).
class MakefileDetectionStrategy : public ProjectDetectionStrategy {
public:
    QString name() const override { return QStringLiteral("Makefile"); }

    bool appliesTo(const QDir &projectDir) const override
    {
        return makefilePath(projectDir).has_value();
    }

    QVector<DetectedCommand> detect(const QDir &projectDir) const override
    {
        const auto path = makefilePath(projectDir);
        if (!path.has_value()) {
            return {};
        }
        const QString content = readTextFile(path.value());
        static const QRegularExpression targetLine(QStringLiteral(R"(^([A-Za-z0-9_-]+)\s*:(?!=))"));

        QSet<QString> seen;
        QVector<DetectedCommand> out;
        for (const QString &line : content.split(QLatin1Char('\n'))) {
            const QRegularExpressionMatch m = targetLine.match(line);
            if (!m.hasMatch()) {
                continue;
            }
            const QString target = m.captured(1);
            if (target.contains(QLatin1Char('%')) || seen.contains(target)) {
                continue;
            }
            seen.insert(target);
            core::Command cmd;
            cmd.name = target;
            cmd.type = core::CommandType::Shell;
            cmd.command = QStringLiteral("make %1").arg(target);
            out.append(DetectedCommand{name(), cmd});
        }
        return out;
    }

private:
    static std::optional<QString> makefilePath(const QDir &projectDir)
    {
        static const QStringList kCandidates = {
            QStringLiteral("Makefile"), QStringLiteral("makefile"), QStringLiteral("GNUmakefile"),
        };
        for (const QString &candidate : kCandidates) {
            if (projectDir.exists(candidate)) {
                return projectDir.filePath(candidate);
            }
        }
        return std::nullopt;
    }
};

std::vector<std::unique_ptr<ProjectDetectionStrategy>> allDetectionStrategies()
{
    std::vector<std::unique_ptr<ProjectDetectionStrategy>> strategies;
    strategies.push_back(std::make_unique<NpmDetectionStrategy>());
    strategies.push_back(std::make_unique<DockerComposeDetectionStrategy>());
    strategies.push_back(std::make_unique<PythonDetectionStrategy>());
    strategies.push_back(std::make_unique<ComposerDetectionStrategy>());
    strategies.push_back(std::make_unique<MakefileDetectionStrategy>());
    return strategies;
}

} // namespace kai::ui
