#pragma once

#include <QString>
#include <QStringList>

namespace kai::engine {

// Os projetos de exemplo (sample/) que o agente consulta como referência de comandos reais.
class SampleLibrary {
public:
    // `directory` vazio = a pasta de samples do app (desenvolvimento: sample/; instalado: assets/samples).
    explicit SampleLibrary(const QString &directory = QString());

    // Caminhos relativos (com "/") de todos os arquivos de texto, em ordem alfabética.
    QStringList files() const;
    // Conteúdo de `relativePath`; vazio se não existir ou se sair da pasta de samples.
    QString fileText(const QString &relativePath) const;

private:
    QString root() const;

    QString m_directory;
};

} // namespace kai::engine
