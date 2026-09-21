#pragma once

#include "core/models.h"

#include <QJsonObject>
#include <QMap>
#include <QString>

#include <optional>

namespace kai::cli {

// Execução GLOBAL (`kai -g <path>`, ou `kai <path>` fora de pasta com
// kai.json/kai.yml) delegada ao APP: o comando roda na instância do Kai —
// registrado na lista de processos, histórico e painel de saída, igual a um
// clique na árvore — e este processo só espelha saída/entrada no terminal
// (ver ipc::StreamChannel). Pedido do usuário: "comandos chamados em
// contexto global PRECISAM ser registrados pelo app".
//
// Com o app fechado, sobe o Kai oculto na bandeja e espera o IPC — com
// trava de arquivo pra dois `kai -g` simultâneos nunca abrirem duas
// instâncias (a própria GUI também recusa uma segunda instância).
//
// `detached` (-d): o app só confirma que aceitou (registrado e disparado) e
// o terminal volta na hora — sem espelhar saída nem repassar teclado.
//
// Devolve o código de saída, ou nullopt quando não deu pra falar com o app
// (quem chama decide o fallback).
// `notify` (-n, só com detached): o app avisa na bandeja quando terminar.
// `window` (-w): o app também abre a saída do comando numa janela desacoplada.
std::optional<int> runViaApp(const core::Command &command, const QMap<QString, QString> &paramValues,
                             bool detached = false, bool notify = false, bool window = false);

// `kai attach <nome|pid>`: acompanha ao vivo um processo que JÁ roda no app
// (reenvia o fim do log já impresso), com o teclado repassado; Ctrl+] solta
// o terminal sem parar o processo. nullopt se o app não está aberto.
std::optional<int> attachViaApp(const QString &target);

// Pedido simples ao app (uma linha de ida, uma de volta — ex: import, raise),
// subindo o Kai na bandeja se estiver fechado (mesma regra de runViaApp).
// nullopt se não deu pra falar com o app ou ele não respondeu a tempo.
std::optional<QJsonObject> requestApp(const QJsonObject &request, int timeoutMs = 60000);

} // namespace kai::cli
