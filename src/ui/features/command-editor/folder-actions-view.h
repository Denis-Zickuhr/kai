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

// Desenha os ícones das ações na linha das PASTAS de uma árvore e trata o
// mouse sobre eles (clique, menu de contexto, dica). É o delegate da coluna de
// AÇÕES (a última, depois da coluna de status/tempo) E um filtro de eventos do
// viewport: o clique num ícone é consumido aqui, então nem seleciona a linha nem
// inicia um arrasto.
//
// As ações de EXPANSÃO (FolderActionIcon::expansion) ficam escondidas atrás de UM
// símbolo de expansão (chevron) no fim da linha, e as de um GRUPO atrás de UM ícone do
// grupo (um por grupo, antes do chevron): clicar abre um menu com elas, e cada item se
// comporta como o ícone equivalente (mesmo `activated`, mesmo menu de contexto, mesmas
// cores/dicas de estado).
//
// Os ícones ficam no FIM da linha, da esquerda pra direita na ordem das ações,
// centralizados na altura da linha. Cor: verde quando a ação roda, accent quando
// parada; o ícone "focado" (o que mostra a saída) ganha um destaque, e ao passar
// o mouse num ícone focado e parado ele vira um "play": clicar nele roda de novo.
class FolderActionsView : public QStyledItemDelegate {
    Q_OBJECT
public:
    struct Provider {
        // id da pasta de uma linha (vazio = a linha não é uma pasta)
        std::function<QString(const QTreeWidgetItem *)> folderIdOf;
        std::function<QVector<FolderActionIcon>(const QString &folderId)> iconsFor;
        std::function<bool(const QString &virtualId)> isRunning;
        // A ação já rodou (há saída guardada pra carregar)?
        std::function<bool(const QString &virtualId)> hasOutput;
        std::function<QString()> focusedId;
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QRect cellRectFor(const QTreeWidgetItem *item) const;
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
    QString m_hoverId;
    QPointer<QMenu> m_menu; // o menu de expansão aberto (pra tratar o botão direito nos itens)
};

} // namespace kai::ui
