#pragma once

#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStyledItemDelegate>
#include <QVector>
#include <functional>

class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

namespace kai::ui {

// Uma ação como aparece na linha de uma pasta: o comando mapeado, no contexto
// daquela pasta (ver core/folder-actions.h).
struct FolderActionIcon {
    QString commandId;
    QString virtualId; // id da execução (comando + pasta): chave de estado/saída
    QString folderId;
    QString name;
    QString iconName;
    // Ação de EXPANSÃO: não ganha ícone na linha, vai pro menu do símbolo de expansão.
    bool expansion = false;
    // GRUPO (tema) da ação: as do mesmo grupo viram UM ícone na linha (`groupIcon`, vazio =
    // o padrão), que abre o menu delas. Vazio = sem grupo.
    QString group;
    QString groupIcon;
};

// Delegate da coluna de AÇÕES (a última) das linhas de PASTA e filtro de eventos do viewport:
// desenha os ícones das ações no fim da linha (esq. -> dir., centralizados) e trata o mouse
// (clique, menu de contexto, dica). O clique num ícone é consumido aqui (não seleciona nem arrasta).
// As ações de EXPANSÃO ficam atrás de UM chevron e as de um GRUPO atrás de UM ícone do grupo (antes
// do chevron); clicar abre um menu cujos itens se comportam como o ícone equivalente.
// Cor: verde quando roda, accent quando parada; o ícone "focado" (mostra a saída) ganha destaque
// e, parado sob o mouse, vira "play" (clicar roda de novo).
class FolderActionsView : public QStyledItemDelegate {
    Q_OBJECT
public:
    // Comando com saída para mostrar (Available) e o que está na Saída agora (Shown).
    enum class OutputMark { None, Available, Shown };

    struct Provider {
        // id da pasta de uma linha (vazio = a linha não é uma pasta)
        std::function<QString(const QTreeWidgetItem *)> folderIdOf;
        std::function<QVector<FolderActionIcon>(const QString &folderId)> iconsFor;
        std::function<bool(const QString &virtualId)> isRunning;
        // A ação já rodou (há saída guardada pra carregar)?
        std::function<bool(const QString &virtualId)> hasOutput;
        std::function<QString()> focusedId;
        // Linhas de COMANDO: a marca da saída no fim da coluna (só uma indicação, não um botão).
        std::function<OutputMark(const QTreeWidgetItem *)> outputMarkOf;
    };

    FolderActionsView(QTreeWidget *tree, Provider provider);

    // O que o ícone mostra enquanto o mouse está sobre ele: o olho quando o PRÓXIMO clique só mostra a saída
    // (rodando, ou saída guardada ainda não carregada), o play quando o clique vai executar de novo (já
    // carregado e parado) e nada de especial no resto (nunca rodou: o clique executa, o ícone fica o da ação).
    enum class HoverGlyph { None, Eye, Play };
    static HoverGlyph hoverGlyph(bool running, bool focused, bool hasOutput);

    // Mesma coluna do status/tempo (1): a linha é de comando (play + tempo) OU de pasta (ações).
    static constexpr int kActionsColumn = 1;
    static constexpr int kBox = 22;          // lado da área clicável de cada ícone
    static constexpr int kGap = 2;
    static constexpr int kMargin = 8;        // respiro nas duas bordas da coluna

    // Quantas posições a linha ocupa: um ícone por ação comum, um por grupo e um pro símbolo
    // de expansão, se houver alguma ação de expansão sem grupo.
    static int slotCount(const QVector<FolderActionIcon> &icons);
    // Largura da coluna pra caber `count` posições (0 = sem coluna).
    static int columnWidthFor(int count);
    // Retângulos das posições (esq. -> dir.) dentro de `cell`, centralizados na altura
    // dela. Só as que cabem na largura da célula.
    static QVector<QRect> iconRects(const QRect &cell, int count);

    struct Hit {
        bool valid = false;
        bool expander = false; // o símbolo de expansão (icon vazio)
        QString group;         // não vazio = o ícone desse grupo (icon vazio)
        FolderActionIcon icon;
        QRect rect;
    };
    Hit hitTest(const QPoint &viewportPos) const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

signals:
    void activated(const QString &commandId, const QString &folderId);
    void contextRequested(const QString &commandId, const QString &folderId, const QPoint &globalPos);
    // Clique numa linha de pasta FORA dos ícones.
    void rowClicked(const QString &folderId);
    // Clique no monitor de uma linha de comando: focar a saída dele.
    void outputMarkClicked(const QTreeWidgetItem *item);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QRect cellRectFor(const QTreeWidgetItem *item) const;
    static QRect outputMarkRect(const QRect &cell);
    const QTreeWidgetItem *outputMarkAt(const QPoint &viewportPos) const;
    static void paintOutputMark(QPainter *painter, const QRect &cell, OutputMark mark, bool hovered);
    void paintIcon(QPainter *painter, const QRect &box, const FolderActionIcon &icon, bool running,
                   bool focused, bool hovered, bool hasOutput) const;
    // Posição que abre um menu (o grupo ou o expansor): `iconName` com `fallbackName` se não existir.
    void paintMenuSlot(QPainter *painter, const QRect &box, const QString &iconName, const QString &fallbackName,
                       bool anyRunning, bool focused, bool hovered) const;
    // O que o PRÓXIMO clique na ação faz (rodando / já carregada / com saída guardada / nova).
    QString tooltipFor(const FolderActionIcon &icon) const;
    // Menu das escondidas: as do `group`, ou (vazio) as de expansão sem grupo.
    void showSlotMenu(const QString &folderId, const QString &group, const QRect &anchor);

    QTreeWidget *m_tree;
    Provider m_provider;
    QString m_pressedId;
    const QTreeWidgetItem *m_pressedMark = nullptr;
    const QTreeWidgetItem *m_hoverMark = nullptr;
    QString m_hoverId;
    QPointer<QMenu> m_menu; // o menu de expansão aberto (pra tratar o botão direito nos itens)
};

} // namespace kai::ui
