#pragma once

#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/models.h"

namespace kai::core {

// Lógica PURA do parâmetro Grid (uma tabela de linhas com colunas tipadas), sem widgets: o formulário só mostra e
// edita as linhas; aqui moram o tipo de cada célula, o JSON e o texto que vai para o comando.

// A coluna cabe numa célula? (text, number, bool, select, date; o resto vira text.)
ParameterType gridColumnType(const GridColumn &column);

// Texto digitado/escolhido -> valor JSON do tipo da coluna (número sem aspas, bool, texto). Número vazio ou
// inválido vira null; bool aceita true/1/yes/sim.
QJsonValue gridCellValue(const GridColumn &column, const QString &text);
// O caminho de volta: o texto que a célula mostra e edita.
QString gridCellText(const GridColumn &column, const QJsonValue &value);

// Uma linha com o valor padrão de cada coluna.
QJsonObject gridDefaultRow(const QVector<GridColumn> &columns);

// As linhas como JSON (só as colunas atuais, NA ORDEM das colunas) e de volta. Texto que não é um array de
// objetos dá lista vazia.
QString gridRowsToJson(const QVector<GridColumn> &columns, const QJsonArray &rows);
QJsonArray gridRowsFromJson(const QString &json);

// O que {{nome}} recebe: o JSON das linhas, ou — com modelo — cada linha pelo modelo, unidas pelo separador. No
// modelo, {{coluna}} é o texto da célula e {{coluna|json}} o literal JSON (texto entre aspas e escapado, número
// e bool soltos). Outros {{...}} ficam como estão.
QString renderGrid(const Parameter &param, const QJsonArray &rows);

// Tudo o que o parâmetro injeta: `nome` (renderGrid), `nome__json` (sempre o JSON) e `nome__count`.
QMap<QString, QString> gridValues(const Parameter &param, const QJsonArray &rows);

// "\n" e "\t" escritos como texto viram o caractere.
QString gridUnescapeSeparator(const QString &separator);

// Linhas de um texto colado (tabulação ou ponto e vírgula/vírgula; uma linha por registro, colunas na ordem).
QJsonArray gridRowsFromDelimitedText(const QVector<GridColumn> &columns, const QString &text);

// Linhas vindas da linha de comando/arquivo: um array JSON de objetos; as colunas ausentes ficam com o padrão.
QJsonArray gridRowsFromInput(const QVector<GridColumn> &columns, const QString &text);

} // namespace kai::core
