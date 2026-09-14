#pragma once

#include <QString>

namespace kai::ui {

// Uma nota do Kai mostrada pelo leitor de documentos (ver core::Note): vive no Kai, não em arquivo. `type`: markdown, text,
// json, yaml ou xml. `local`: só deste Kai (não vai para export/kai.yml).
struct DocNote {
    QString id;
    QString title;
    QString icon;
    QString type = QStringLiteral("markdown");
    QString content;
    bool local = true;
};

} // namespace kai::ui
