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

    virtual QString label() const = 0;

    virtual QString iconName() const = 0;

    virtual void clear() = 0;

    // true se há algo para exibir (OutputPanel mostra/oculta a aba automaticamente).
    virtual bool hasContent() const = 0;

    virtual void setViewOptions(const ViewOptions & /*opts*/) {}

    // Aplica o tema ativo (fontes, cores).
    virtual void applyTheme() {}
};

} // namespace kai::ui
