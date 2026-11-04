#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace kai::core {

// Escopo de cada campo: o que pode ir pra um arquivo (sync, exportação, projeto versionado). Todo
// campo gravado por core/models.cpp declara aqui sua classe; um teste confere que nenhum fica sem classe.
//  Portable  conteúdo autorado: é do projeto e vai pro arquivo.
//  Reference aponta pra outra coisa (comando, coleção, perfil de terminal) por NOME; referência que
//            não resolve é avisada e preservada, nunca descartada em silêncio.
//  Identity  id gerado por instalação; nunca vai (a identidade no arquivo é caminho + nome).
//  Local     estado de uso desta instalação (últimos valores, histórico, origem): nunca vai.
//  Machine   depende da máquina (diretório absoluto, interpretador): vai, mas é o que mais difere.
//  Data      dado do usuário volumoso/pessoal (entradas de coleção): só por opt-in.
//  Secret    valor secreto: nunca vai; o arquivo leva só o nome.

enum class FieldScope { Portable, Reference, Identity, Local, Machine, Data, Secret };

// Quem declara o campo.
enum class FieldOwner { Command, Parameter, Folder, Collection, CollectionField, CollectionEntry };

// Classe da chave JSON `key` de `owner`; nullopt = chave sem classe (o teste de paridade falha).
std::optional<FieldScope> scopeOf(FieldOwner owner, const QString &key);

// Todas as chaves de `owner` com a classe `scope`.
QStringList keysWithScope(FieldOwner owner, FieldScope scope);

// Todas as chaves declaradas de `owner`.
QStringList declaredKeys(FieldOwner owner);

} // namespace kai::core
