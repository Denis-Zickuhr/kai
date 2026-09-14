#pragma once

#include "ui/features/docs/doc-text-tools.h"

#include <QMenu>
#include <QString>
#include <QVector>

#include <functional>

namespace kai::ui {

// O catálogo das ferramentas de texto do editor (formatar, validar, converter, utilitários) e o menu que as lista. O
// catálogo é dado puro (id, grupo, rótulo traduzido e a função), então os testes percorrem todas.
struct DocTool {
    QString id;        // estável: "json.pretty", "xml.minify"...
    QString group;     // "json", "xml", "convert", "text"
    QString labelKey;  // chave de tradução do rótulo
    std::function<texttools::Result(const QString &)> run;
    bool validateOnly = false; // não altera o texto: só mostra o veredito
};

const QVector<DocTool> &docTools();
const DocTool *findDocTool(const QString &id);
// A ferramenta de "Formatar documento" para a linguagem (JSON/XML embelezam; as outras não têm).
QString formatToolIdFor(texttools::Language language);

// Monta o menu agrupado. Escolher um item emite `toolChosen(id)`; a linguagem destaca o grupo que mais interessa.
class DocToolsMenu : public QMenu {
    Q_OBJECT

public:
    explicit DocToolsMenu(texttools::Language language, QWidget *parent = nullptr);

signals:
    void toolChosen(const QString &id);
};

} // namespace kai::ui
