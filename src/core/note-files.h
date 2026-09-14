#pragma once

#include "core/models.h"

#include <QString>

namespace kai::core {

// Notas <-> arquivos do sistema. A nota mora no Kai (notes.json); estas funções só a levam para fora
// (salvar como, abrir uma cópia no editor do sistema). Bloqueiam em I/O: rode numa thread.

// O que o usuário pode fazer com o conteúdo de uma nota fora do Kai.
enum class NoteFileAction {
    SaveAs,      // escolher onde salvar um arquivo
    OpenCopy,    // gravar uma cópia temporária e abrir no editor do sistema (edições lá não voltam para a nota)
    CopyContent  // área de transferência
};

// Nome de arquivo seguro para a nota: caracteres proibidos (/ \ : * ? " < > |) e de controle viram "_", sem espaços ou
// pontos nas pontas, sem nome reservado do Windows (CON, NUL...), e com a extensão do tipo (md, txt, json, yml, xml).
QString noteFileName(const Note &note);

// Grava o texto em UTF-8, de forma atômica (QSaveFile). false se não conseguiu.
bool writeNoteFile(const QString &path, const QString &content);

} // namespace kai::core
