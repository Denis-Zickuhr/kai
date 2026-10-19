#include "ui/layout/view-registry.h"

#include "utils/translation-manager.h"

namespace kai::ui::layout {

void ViewRegistry::registerView(const ViewSpec &spec, QWidget *widget)
{
    if (!m_entries.contains(spec.id)) {
        m_order.append(spec.id);
    }
    Entry entry;
    entry.spec = spec;
    entry.widget = widget;
    m_entries.insert(spec.id, entry);
}

void ViewRegistry::registerPrefix(const QString &prefix, PrefixResolver resolver)
{
    m_prefixes.append({prefix, std::move(resolver)});
}

std::optional<ViewSpec> ViewRegistry::spec(const QString &id) const
{
    // Os fixos vêm do registro; os dinâmicos são sempre resolvidos de novo (o comando de um painel `kip:<id>` pode ter
    // sido apagado depois de o painel nascer), nunca de um resultado guardado.
    if (m_order.contains(id)) {
        const auto it = m_entries.constFind(id);
        return it != m_entries.constEnd() ? std::optional<ViewSpec>(it->spec) : std::nullopt;
    }
    for (const auto &[prefix, resolver] : m_prefixes) {
        if (id.startsWith(prefix) && id.size() > prefix.size()) {
            return resolver(id);
        }
    }
    return std::nullopt;
}

bool ViewRegistry::isKnown(const QString &id) const
{
    const auto found = spec(id);
    return found && (!found->available || found->available());
}

QStringList ViewRegistry::staticIds() const
{
    QStringList ids;
    for (const QString &id : m_order) {
        if (isKnown(id)) {
            ids.append(id);
        }
    }
    return ids;
}

QWidget *ViewRegistry::existingWidget(const QString &id) const
{
    const auto it = m_entries.constFind(id);
    return it != m_entries.constEnd() ? it->widget.data() : nullptr;
}

QWidget *ViewRegistry::widget(const QString &id)
{
    if (QWidget *existing = existingWidget(id)) {
        return existing;
    }
    const std::optional<ViewSpec> found = spec(id);
    if (!found || !found->factory) {
        return nullptr;
    }
    QWidget *created = found->factory();
    if (!created) {
        return nullptr;
    }
    Entry &entry = m_entries[id];
    entry.spec = *found; // painéis dinâmicos entram no mapa na primeira construção
    entry.widget = created;
    return created;
}

void ViewRegistry::forget(const QString &id)
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end() || m_order.contains(id)) {
        return; // os fixos nunca somem
    }
    if (it->widget) {
        it->widget->deleteLater();
    }
    m_entries.erase(it);
}

QString ViewRegistry::title(const QString &id) const
{
    const auto found = spec(id);
    if (!found) {
        return id;
    }
    if (found->titleProvider) {
        return found->titleProvider();
    }
    return found->titleKey.isEmpty() ? id : utils::tr(found->titleKey);
}

QString ViewRegistry::icon(const QString &id) const
{
    const auto found = spec(id);
    return found ? found->icon : QString();
}

bool ViewRegistry::closable(const QString &id) const
{
    const auto found = spec(id);
    return found ? found->closable : true;
}

Home ViewRegistry::defaultHome(const QString &id) const
{
    const auto found = spec(id);
    return found ? found->home : Home::Secondary;
}

LayoutModel::Validator ViewRegistry::validator() const
{
    return [this](const QString &id) { return isKnown(id); };
}

} // namespace kai::ui::layout
