#pragma once

#include <QString>
#include <QStringList>

namespace kai::utils {

// Converte para o estilo POSIX/WSL (ex.: "/mnt/c/Users/..."). Reconhece paths Windows (C:\..., C:/...) e UNC do
// WSL (\\wsl.localhost\<distro>\..., \\wsl$\<distro>\...); um path já POSIX (começa com / ou ~) passa intacto.
// Compartilhado com ExecutionPipeline e o file-picker de parâmetros (Parameter::filePathFormat).
QString toPosixPath(const QString &path);

// Converte para o estilo Windows (ex.: "C:\Users\..."). Um path /mnt/<letra>/... volta para "<LETRA>:\...";
// qualquer outro POSIX só troca as barras. Com `wslDistro` (ver wslDistroFromTemplate), um POSIX que não é
// /mnt/<letra>/ vive dentro do WSL e vira o UNC que o Windows enxerga: "/home/u/x" -> \\wsl.localhost\<distro>\home\u\x.
QString toWindowsPath(const QString &path, const QString &wslDistro = QString());

// Aplica `format` ("native"/"posix"/"windows", ver core::Parameter::filePathFormat); "native" devolve `path` intocado.
QString convertFilePathFormat(const QString &path, const QString &format,
                              const QString &wslDistro = QString());

// Formato de PROJECT_PATH que já vem selecionado ao importar a pasta
// `pickedPath` ("native"/"posix"/"windows"). No Windows o Kai só enxerga o
// disco de um projeto do WSL pelo UNC (\\wsl.localhost\<distro>\...): "/home/..."
// ali dá "diretório não encontrado", então o padrão é "windows". Em Linux/WSL o
// padrão é "posix" quando o seletor devolveu um path Windows/UNC, senão "native".
QString defaultProjectPathFormat(const QString &pickedPath, bool windowsHost);

// Distro WSL que executa o template de um alvo de terminal. Vazio quando o
// template não chama o wsl ou segue para um container/máquina remota depois
// dele (`wsl ... docker exec`, `wsl ... ssh`): aí o comando não enxerga o disco
// da distro nem o do Windows. Sem `-d/--distribution` usa `defaultDistro` (a
// distro padrão do Windows — ver defaultWslDistro).
QString wslDistroFromTemplate(const QString &commandTemplate, const QString &defaultDistro);

// Distro WSL padrão da máquina (Windows: registro do Lxss; demais: vazio).
// Leitura síncrona e barata, mas só no Windows.
QString defaultWslDistro();

// Distros WSL registradas (Windows: Lxss), a padrão primeiro; vazio nos demais sistemas.
QStringList installedWslDistros();

// Outras grafias de `path` que podem ser o mesmo lugar neste sistema, da mais provável à menos (sem repetir `path`).
// No Windows, "/home/u/p" vira o UNC de cada distro de `wslDistros` e "/mnt/c/x" vira "C:\x"; em Linux/WSL, um path
// Windows/UNC vira o POSIX. Só texto: quem chama confere quais existem.
QStringList equivalentPathCandidates(const QString &path, const QStringList &wslDistros, bool windowsHost);

} // namespace kai::utils
