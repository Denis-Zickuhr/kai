#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "ui/layout/layout-model.h"

#include <functional>

class QMenu;
#include <optional>

namespace kai::ui::layout {

// O que o host precisa saber de um painel para mostrá-lo: título, ícone, onde nasce e como construí-lo.
struct ViewSpec {
    QString id;
    // Chave i18n do título (ou `titleProvider` para títulos dinâmicos, como o nome de um comando).
    QString titleKey;
    std::function<QString()> titleProvider;
    // Nome do ícone Lucide.
    QString icon;
    bool closable = true;
    // Onde nasce: a célula da lista de comandos (Primary) ou a da Saída (Secondary).
    Home home = Home::Secondary;
    // Cria o widget na primeira vez que o painel é mostrado (custo zero até lá). Ausente = o widget já foi dado.
    std::function<QWidget *()> factory;
    // Largura mínima exigida pelo conteúdo quando vive numa lateral.
    int minWidth = 0;
    // Lê o estado de "pode fechar agora?" (ex.: uma execução em andamento não impede, mas um aviso pode).
    std::function<bool()> canClose;
    // Põe o foco de teclado no painel (atalho Alt+N). Ausente = o próprio widget.
    std::function<void()> focus;
    // O painel só existe enquanto isto for verdade (ex.: o chat de IA só existe com um provedor configurado). Ausente =
    // sempre. Fora do ar, o id é desconhecido: some do menu Exibir e é descartado do layout.json.
    std::function<bool()> available;
    // Itens a mais no menu do painel (clique direito na aba), depois dos de posição. Ausente = nenhum.
    std::function<void(QMenu *menu)> menuExtras;
};

// Registro dos painéis conhecidos. Os fixos (commands, output, document, ...) são registrados pelo MainWindow; os
// dinâmicos (`kip:<id>`, `run:<id>`) entram por um resolvedor de prefixo — que também é quem diz se o comando ainda
// existe (validação do layout.json).
class ViewRegistry : public QObject {
    Q_OBJECT

public:
    using PrefixResolver = std::function<std::optional<ViewSpec>(const QString &id)>;

    explicit ViewRegistry(QObject *parent = nullptr) : QObject(parent) {}

    // `widget` já construído (Comandos, Saída); sem ele, `spec.factory` cria sob demanda.
    void registerView(const ViewSpec &spec, QWidget *widget = nullptr);
    void registerPrefix(const QString &prefix, PrefixResolver resolver);

    bool isKnown(const QString &id) const;
    std::optional<ViewSpec> spec(const QString &id) const;
    // Constrói se preciso. nullptr se o id é desconhecido ou a fábrica falhou.
    QWidget *widget(const QString &id);
    QWidget *existingWidget(const QString &id) const;
    // Libera um painel dinâmico (o comando sumiu): o host o descarta.
    void forget(const QString &id);

    QString title(const QString &id) const;
    QString icon(const QString &id) const;
    bool closable(const QString &id) const;
    Home defaultHome(const QString &id) const;
    // Os ids fixos disponíveis agora, na ordem em que foram registrados (menu Exibir).
    QStringList staticIds() const;
    // Todos os fixos registrados, disponíveis ou não.
    QStringList registeredIds() const { return m_order; }

    LayoutModel::Validator validator() const;

signals:
    // Um título dinâmico mudou (comando renomeado): o host refaz a aba.
    void titleChanged(const QString &id);
    // O painel precisa ser mostrado (ex.: a execução pediu a Saída).
    void showRequested(const QString &id);

public:
    void notifyTitleChanged(const QString &id) { emit titleChanged(id); }

private:
    struct Entry {
        ViewSpec spec;
        QPointer<QWidget> widget;
    };
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QList<std::pair<QString, PrefixResolver>> m_prefixes;
};

} // namespace kai::ui::layout
