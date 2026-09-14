#pragma once

#include "core/models.h"

#include <QJsonObject>
#include <QMap>
#include <QString>

#include <optional>

namespace kai::cli {

// Executa o comando no app (registrado em processos/histórico/painel); este processo só espelha
// saída/entrada no terminal. Com o app fechado, sobe o Kai na bandeja sob trava de arquivo.
// `detached` (-d): volta assim que o app aceita. `notify` (-n, só com detached): aviso na bandeja
// ao terminar. `window` (-w): abre a saída numa janela desacoplada.
// Devolve o código de saída, ou nullopt se não deu pra falar com o app (quem chama decide o fallback).
std::optional<int> runViaApp(const core::Command &command, const QMap<QString, QString> &paramValues,
                             bool detached = false, bool notify = false, bool window = false);

// `kai attach <nome|pid>`: acompanha ao vivo um processo que já roda no app; Ctrl+] solta o
// terminal sem parar o processo. nullopt se o app não está aberto.
std::optional<int> attachViaApp(const QString &target);

// Pedido simples ao app (uma linha de ida, uma de volta), subindo-o na bandeja se fechado.
// nullopt se não deu pra falar com o app ou ele não respondeu a tempo.
std::optional<QJsonObject> requestApp(const QJsonObject &request, int timeoutMs = 60000);

} // namespace kai::cli
