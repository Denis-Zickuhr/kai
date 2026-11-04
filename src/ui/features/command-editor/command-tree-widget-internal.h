#pragma once

#include <QIcon>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;

namespace kai::ui::cmdtree {

inline constexpr const char *kLogTag = "CommandTree";
// Id da pasta PADRÃO sintética que hospeda comandos/coleções órfãos
// (folderId vazio ou apontando para pasta/comando inexistente). Não é
// persistida — existe só na árvore para nunca esconder órfãos.
inline constexpr const char *kDefaultFolderId = "__kai_default__";
inline constexpr int kCommandIdRole = Qt::UserRole + 1;
inline constexpr int kIsCommandRole = Qt::UserRole + 2;
// Marca um item de árvore como Coleção (não-executável). Distinto de
// comando (kIsCommandRole=true) e de pasta (kIsCommandRole=false sem este).
inline constexpr int kIsCollectionRole = Qt::UserRole + 3;
// Marca se o comando é do tipo HTTP (para exibir "Editar body" no menu).
inline constexpr int kIsHttpRole = Qt::UserRole + 4;
// Marca um item como NOTA. Notas também levam kIsCollectionRole (são "nem pasta nem comando", como a coleção: não
// executam, não aceitam filhos...), e este papel as distingue onde o comportamento muda (menu, abrir, arrastar).
inline constexpr int kIsNoteRole = Qt::UserRole + 5;
inline constexpr int kStatusColumn = 1;
// Fundo do item sempre transparente, aplicado como stylesheet LOCAL (prioridade garantida sobre o
// tema): o DraggableTreeWidget pinta normal/zebra/hover/seleção em linha inteira (ver paintEvent/setRowColors).
inline const QString kTreeItemTransparentQss = QStringLiteral(
    "QTreeWidget::item, QTreeWidget::item:hover, QTreeWidget::item:selected,"
    " QTreeWidget::item:selected:active, QTreeWidget::item:selected:!active,"
    " QTreeWidget::item:focus {"
    " background-color: transparent; border: none; outline: none; border-radius: 0px; }");

QIcon withDriftBadge(const QIcon &base);
QIcon withDraftBadge(const QIcon &base);
int statusColumnBaseWidth(const QTreeWidget *tree);
void applyRunningIndicator(QTreeWidgetItem *item, bool isRunning);

} // namespace kai::ui::cmdtree
