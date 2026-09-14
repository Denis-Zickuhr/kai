#pragma once

#include <QComboBox>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QStyledItemDelegate>

namespace kai::core {
struct Folder;
}

namespace kai::ui {

// Delegate que desenha o caminho da pasta como pills coloridas, uma por segmento (tom do accent, mais claro
// quanto mais fundo; raio via utils::tokens::radiusSm()). Os segmentos vêm de Qt::UserRole+1 (QStringList),
// preenchido por FolderPickerWidget::populateItems.
class FolderPickerPillDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit FolderPickerPillDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
              const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

// Seletor de pastas com busca e pills por profundidade. Mantém a API mínima do antigo QComboBox
// (selectedFolderId/setSelectedFolderId/selectionChanged) para os callers existentes.
class FolderPickerWidget : public QComboBox {
    Q_OBJECT

public:
    explicit FolderPickerWidget(QWidget *parent = nullptr);

    void setFolders(const QVector<kai::core::Folder> &folders);

    QString selectedFolderId() const;

    void setSelectedFolderId(const QString &folderId);

    // Adiciona uma opção "None" (pasta raiz/sem pasta) no topo.
    void enableNoneOption(const QString &noneText = QString());

signals:
    void selectionChanged(const QString &folderId);

private:
    void populateItems();
    // Segmentos de nome do caminho até `folderId`: desenham as pills e, juntos com espaço, formam o texto de busca
    // (o QCompleter casa qualquer segmento).
    QStringList pathSegments(const QString &folderId) const;

    QVector<kai::core::Folder> m_folders;
    QMap<int, QString> m_indexToFolderId;
    bool m_noneOptionEnabled = false;
};

} // namespace kai::ui
