#pragma once

#include "ui/features/docs/doc-note.h"

#include <QString>
#include <QVector>

namespace kai::ui {

// Referências entre notas, dentro do markdown (de uma nota ou de um arquivo): peça PURA, sem widgets nem I/O.
//   [[Título]]            link para a nota (por título, sem diferenciar maiúsculas, ou por id)
//   [[Título|texto]]      o mesmo, com outro texto no link
//   ![[Título]]           (sozinho numa linha) traz o conteúdo da nota para dentro deste documento
// Links viram `[texto](kai:note/<id>)`; o leitor abre a nota ao clicar. A nota embutida entra entre duas linhas, com o
// título (um link) em cima; as que não são markdown entram como bloco de código. Uma nota que se inclui de volta (ciclo)
// ou além de 3 níveis vira só o link. Referência a uma nota que não existe fica como está, em texto. Blocos de código e
// código em linha do markdown ficam intactos.
QString expandNoteReferences(const QString &markdown, const QVector<DocNote> &notes, const QString &selfId = QString());

} // namespace kai::ui
