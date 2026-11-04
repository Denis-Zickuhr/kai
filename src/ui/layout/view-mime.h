#pragma once

#include <QString>

namespace kai::ui::layout {

// Tipo MIME do arrasto de um painel entre zonas (o conteúdo é o id do painel). Vive à parte para quem arrasta (a faixa de
// abas de uma zona, a barra de guias das execuções) não precisar conhecer o resto do quadro.
inline const QString kViewMimeType = QStringLiteral("application/x-kai-view");

} // namespace kai::ui::layout
