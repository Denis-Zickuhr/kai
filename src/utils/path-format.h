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
QString toWindowsPath(const QString &path);

// Aplica o formato pedido em `format` ("native"/"posix"/"windows" — ver
// core::Parameter::filePathFormat). "native" retorna `path` sem
// alteração nenhuma (o que o QFileDialog/seletor nativo já devolveu).
QString convertFilePathFormat(const QString &path, const QString &format);

} // namespace kai::utils
