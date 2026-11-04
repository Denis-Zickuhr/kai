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

// Visualizador de JSON em texto livre para respostas HTTP: FoldableJsonView com o JSON pretty-printed, realce
// (JsonSyntaxHighlighter), gutter de dobras, busca, "Copiar" e "Destacar" (janela própria). Se o texto não for JSON válido, exibe cru.
// Tudo nasce expandido. É um QWidget para ser embutido (painel de saída) ou aberto solto (detach).
class JsonViewerWidget : public QWidget {
    Q_OBJECT

public:
    explicit JsonViewerWidget(QWidget *parent = nullptr);

    // Exibe o texto bruto da resposta: JSON válido é formatado e ganha dobras; senão, cru.
    void setJsonText(const QString &rawText);

    // Extrai o último bloco JSON válido de um texto misto (log), de forma limitada: a varredura tem teto (evita O(n²)
    // na GUI) e a resposta mais recente vence; "{{VAR}}" não interpolada não conta. Vazio se não houver JSON válido.
    static QString extractJsonBlock(const QString &text);

    // JSON atual formatado (pretty-print) — usado para copiar/detach.
    QString formattedJson() const;

    // Modo embutido (painel aninhado na saída): sem o título "Resposta JSON". Padrão: false.
    void setEmbeddedMode(bool embedded);

    // Modo destacado (janela própria): esconde o botão "Destacar".
    void setDetachedMode(bool detached);

    // Corpo "colado" na moldura do painel de Saída: sem borda própria, topo reto e canto de baixo com o raio dado
    // (concêntrico à moldura). `background` é o valor da propriedade QSS.
    void setFlushBody(const QString &background, int bottomRadius);

    // Expande (se preciso) e foca o campo de busca (Ctrl+F, ver MainWindow::setupActionShortcuts e OutputPanel::focusSearch).
    void focusSearch() { setSearchExpanded(true); }

private slots:
    void handleCopy();
    void handleDetach();
    // Destaca todas as ocorrências de chave/valor que casam com o termo e pula para a primeira.
    void applyFilter(const QString &needle);
    // "N/M": Enter ou ↑/↓ navegam entre ocorrências.
    void goToNextMatch();
    void goToPreviousMatch();

private:
    void setupUi();
    void applyViewStyle();
    // Chip de lupa: colapsado mostra só o chip, expandido mostra o campo. Reposiciona o overlay.
    void setSearchExpanded(bool expanded);
    // Reposiciona o overlay no canto superior-direito do corpo (resizeEvent e ao mostrar).
    void repositionOverlay();
    // Vai para a ocorrência `index` de m_matches (com wrap-around), reaplica os realces e atualiza o contador.
    void goToMatch(int index);
    void updateMatchCounterLabel();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:

    FoldableJsonView *m_view = nullptr;
    bool m_flush = false;
    QString m_flushBackground;
    int m_flushBottomRadius = 0;
    QLabel *m_titleLabel = nullptr;
    QLineEdit *m_filterField = nullptr;
    QToolButton *m_detachButton = nullptr;
    QToolButton *m_searchToggle = nullptr; // chip lupa: abre/fecha a busca
    bool m_searchExpanded = false;
    QWidget *m_overlayBar = nullptr; // controles flutuantes sobre o corpo
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
