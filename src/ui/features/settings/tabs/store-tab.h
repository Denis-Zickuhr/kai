#pragma once

#include <QVector>
#include <QWidget>

#include "core/store-model.h"

class QTableWidget;

namespace kai::ui {

class CollapsibleSectionCard;

// Aba "Loja" (Kai Store): a lista de provedores. Nenhum vem de fábrica; sem provedor habilitado o app não tem menu da
// Loja e nunca contata a rede. Fonte de verdade: m_providers.
class StoreTab : public QWidget {
    Q_OBJECT

public:
    explicit StoreTab(const QVector<core::StoreProviderConfig> &providers, QWidget *parent = nullptr);

    QVector<core::StoreProviderConfig> providers() const { return m_providers; }

private:
    void rebuildTable();
    void addProvider();
    bool editProvider(int row);
    void removeProvider(int row);

    CollapsibleSectionCard *m_card = nullptr;
    QTableWidget *m_table = nullptr;
    QVector<core::StoreProviderConfig> m_providers;
};

} // namespace kai::ui
