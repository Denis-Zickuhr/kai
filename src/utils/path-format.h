#pragma once

#include <QString>

namespace kai::utils {

// Converte um path pro estilo POSIX/WSL (ex: "/mnt/c/Users/..." ou
// "/home/..."). Reconhece paths Windows (C:\..., C:/...) e UNC do WSL
// (\\wsl.localhost\<distro>\..., \\wsl$\<distro>\...); um path que já é
// POSIX (começa com / ou ~) passa intacto. Extraído da lógica que já
// existia em ExecutionPipeline (usada pra injetar `cd` num alvo Posix a
// partir de um working_dir Windows) — mesma implementação, agora
// compartilhada com o file-picker de parâmetros (ver Parameter::
// filePathFormat).
QString toPosixPath(const QString &path);

// Converte um path pro estilo Windows (ex: "C:\Users\..."). Reconhece
// paths POSIX/WSL no formato /mnt/<letra>/... (produzido pelo próprio
// toPosixPath, ou digitado à mão) convertendo de volta pra "<LETRA>:\...";
// qualquer outro path POSIX (sem prefixo /mnt/<letra>/, ex: "/home/...")
// só troca as barras (`/` -> `\`), já que não há uma unidade Windows
// correspondente conhecida.
//
// Com `wslDistro` (nome da distro que executa o comando — ver
// wslDistroFromTemplate), um path POSIX que NÃO é /mnt/<letra>/ vive dentro do
// sistema de arquivos do WSL e vira o UNC que o Windows enxerga:
// "/home/u/x" -> \\wsl.localhost\<distro>\home\u\x.
QString toWindowsPath(const QString &path, const QString &wslDistro = QString());

// Aplica o formato pedido em `format` ("native"/"posix"/"windows" — ver
// core::Parameter::filePathFormat). "native" retorna `path` sem
// alteração nenhuma (o que o QFileDialog/seletor nativo já devolveu).
QString convertFilePathFormat(const QString &path, const QString &format,
                              const QString &wslDistro = QString());

// Distro WSL que executa o template de um alvo de terminal. Vazio quando o
// template não chama o wsl; sem `-d/--distribution` usa `defaultDistro` (a
// distro padrão do Windows — ver defaultWslDistro).
QString wslDistroFromTemplate(const QString &commandTemplate, const QString &defaultDistro);

// Distro WSL padrão da máquina (Windows: registro do Lxss; demais: vazio).
// Leitura síncrona e barata, mas só no Windows.
QString defaultWslDistro();

} // namespace kai::utils
