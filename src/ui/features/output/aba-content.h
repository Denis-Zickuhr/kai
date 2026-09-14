#pragma once

#include <QWidget>

namespace kai::ui {

// Interface base para conteúdo de uma aba de saída.
// Padroniza layout, tema, visibilidade e limpeza de dados.
// Subclasses: OutputStdoutContent, OutputJsonContent, OutputHttpRequestContent, etc.
class AbaContent : public QWidget {
    Q_OBJECT

public:
    struct ViewOptions {
        bool lineNumbers = true;
        bool wrap = false;
        bool timestamps = false;
        bool autoScroll = true;
        bool compact = false;
        int fontSize = 10;
    };

    explicit AbaContent(QWidget *parent = nullptr);
    virtual ~AbaContent() = default;

    // Retorna o rótulo da aba (ex: "Saída", "JSON").
    virtual QString label() const = 0;

    // Retorna o ícone da aba (ex: "terminal", "braces").
    virtual QString iconName() const = 0;

    // Limpa todo conteúdo e reseta para estado vazio.
    virtual void clear() = 0;

    // Retorna true se há algo pra exibir (OutputPanel usa isto
    // para mostrar/ocultar a aba automaticamente).
    virtual bool hasContent() const = 0;

    // Define as ViewOptions globais (números de linha, quebra, timestamp, etc).
    virtual void setViewOptions(const ViewOptions & /*opts*/) {}

    // Aplica o tema atualmente ativo (fontes, cores). Chamado ao
    // mudar de tema.
    virtual void applyTheme() {}
};

} // namespace kai::ui
