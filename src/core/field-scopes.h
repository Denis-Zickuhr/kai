#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace kai::core {

// ============================================================================
// ESCOPO DE CADA CAMPO: o que pode ir pra um arquivo (sync, exportação, projeto versionado) e o que não.
// Todo campo que um modelo grava (core/models.cpp) declara aqui a que classe pertence; um teste confere que
// nenhum campo novo fica sem classe, pra "esqueci de levar X" e "levei X que não devia" não acontecerem calados.
//
//  Portable  conteúdo autorado: é do projeto e vai pro arquivo.
//  Reference aponta pra outra coisa (comando, coleção, perfil de terminal) por NOME; quem carrega resolve com o
//            que tem, e uma referência que não resolve é avisada e preservada, nunca descartada em silêncio.
//  Identity  id gerado por instalação; nunca vai pro arquivo (a identidade lá é caminho + nome).
//  Local     estado de uso desta instalação (últimos valores, histórico, caminho de origem): nunca vai.
//  Machine   depende desta máquina (diretório absoluto, interpretador): vai, mas é a primeira coisa que difere
//            entre máquinas — candidato a uma camada local no futuro.
//  Data      dado do usuário, volumoso e às vezes pessoal (entradas de coleção): só por opt-in explícito.
//  Secret    valor secreto: nunca vai pra arquivo nenhum (o arquivo leva só o nome).
// ============================================================================

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
