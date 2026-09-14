#pragma once

#include <QList>
#include <QString>
#include <QTextCursor>
#include <QWidget>

class QLineEdit;
class QLabel;
class QToolButton;

namespace kai::ui {

class FoldableJsonView;

// Visualizador de JSON em TEXTO LIVRE com colapso inline para respostas HTTP
// (feature "JSON viewer", feedback do usuário). Antes era uma árvore
// (QTreeWidget) com uma linha por chave/valor — o usuário achou "feio" e com
// "campos estranhos"; pediu algo "mais texto livre, estilo Insomnia/Postman"
// com dobras inline "tipo o do JetBrains". Agora é um QPlainTextEdit
// (FoldableJsonView) com o JSON pretty-printed, realce de sintaxe
// (JsonSyntaxHighlighter) e um gutter com marcadores clicáveis que colapsam
// pares de chave/colchete — ver foldable-json-view.h pra mecânica.
// Recursos:
//  - parse do texto de resposta; se não for JSON válido, exibe o texto cru
//    (sem quebrar, sem dobras);
//  - por padrão tudo fica EXPANDIDO (só blocos muito grandes nascem
//    colapsados — ver FoldableJsonView::setFoldableJsonText);
//  - botão "Formatar"/campo de busca, "Copiar" (JSON formatado para a área
//    de transferência) e "Destacar" (abre a mesma visualização numa janela
//    separada).
//
// É um QWidget para poder ser embutido (ex: no painel de saída) e também
// aberto solto numa janela (detach).
class JsonViewerWidget : public QWidget {
    Q_OBJECT

public:
    explicit JsonViewerWidget(QWidget *parent = nullptr);

    // Define o conteúdo a exibir a partir do texto bruto da resposta.
    // Se `rawText` for um JSON válido, formata e calcula as dobras; senão,
    // mostra o texto cru como está (sem quebrar).
    void setJsonText(const QString &rawText);

    // Extrai o ÚLTIMO bloco JSON válido de um texto misto (log), de forma
    // LIMITADA. Motivos (achados de auditoria): (1) a detecção antiga era
    // só "contém '{'", gerando falsos positivos (log de shell, {{VAR}} não
    // interpolada) — o botão aparecia e a árvore abria vazia; (2) o scan
    // original era O(n²) (para cada '{' varria até o fim e parseava), o que
    // podia CONGELAR a GUI com log grande; (3) com várias respostas no log,
    // o correto é a MAIS RECENTE (varremos de trás para frente).
    // Retorna string vazia se não houver JSON válido.
    static QString extractJsonBlock(const QString &text);

    // JSON atual formatado (pretty-print) — usado para copiar/detach.
    QString formattedJson() const;

    // Modo embutido (painel aninhado na saída): esconde o título
    // "Resposta JSON" redundante, deixando só os botões Copiar/Destacar
    // (feedback do usuário). Padrão: false (modo janela/standalone mostra
    // o título).
    void setEmbeddedMode(bool embedded);

    // Modo destacado (janela própria): esconde o botão "Destacar" (não faz
    // sentido destacar o que já está destacado) — feedback do usuário.
    void setDetachedMode(bool detached);

    // Expande (se preciso) e foca o campo de busca — usado pelo atalho
    // "Ctrl+F"/ação de pesquisa quando esta aba está em foco (ver
    // MainWindow::setupActionShortcuts e OutputPanel::focusSearch).
    void focusSearch() { setSearchExpanded(true); }

private slots:
    void handleCopy();
    void handleDetach();
    // Busca/realce no texto (substitui o filtro-por-árvore de antes, que
    // não faz mais sentido numa vista de texto livre): destaca todas as
    // ocorrências de chave/valor que casam com o termo e pula pra primeira.
    void applyFilter(const QString &needle);
    // Contador/jump estilo Notepad (pedido do usuário): "N/M" + Enter ou os
    // botões ↑/↓ navegam entre ocorrências sem precisar reeditar o campo.
    void goToNextMatch();
    void goToPreviousMatch();

private:
    void setupUi();
    // Alterna o campo de busca do overlay (chip lupa): colapsado mostra só
    // o chip; expandido mostra o campo. Reposiciona o overlay ao mudar.
    void setSearchExpanded(bool expanded);
    // Reposiciona o overlay flutuante de controles no canto superior-direito
    // do corpo (chamado em resizeEvent e ao mostrar).
    void repositionOverlay();
    // Vai pra ocorrência de índice `index` em m_matches (com wrap-around),
    // reaplica os realces (a atual num tom diferente das demais) e atualiza
    // o contador "N/M".
    void goToMatch(int index);
    void updateMatchCounterLabel();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:

    FoldableJsonView *m_view = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLineEdit *m_filterField = nullptr;
    QToolButton *m_detachButton = nullptr;
    QToolButton *m_searchToggle = nullptr; // chip lupa: abre/fecha a busca
    bool m_searchExpanded = false;
    QWidget *m_overlayBar = nullptr; // controles flutuantes sobre o corpo
    // Contador/jump estilo Notepad (ver applyFilter/goToMatch).
    QLabel *m_matchCounterLabel = nullptr;
    QToolButton *m_prevMatchButton = nullptr;
    QToolButton *m_nextMatchButton = nullptr;
    QList<QTextCursor> m_matches;
    int m_currentMatchIndex = -1;
    QString m_rawText;
    QString m_formatted;
    bool m_isValidJson = false;
    QWidget *m_detachedWindow = nullptr;
};

} // namespace kai::ui
