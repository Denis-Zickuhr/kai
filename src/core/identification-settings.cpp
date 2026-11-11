#include "core/identification-settings.h"

namespace kai::core {

namespace {
IdentificationSettings &current()
{
    static IdentificationSettings settings;
    return settings;
}
} // namespace

const IdentificationSettings &identificationSettings()
{
    return current();
}

void setIdentificationSettings(const IdentificationSettings &settings)
{
    current() = settings;
}

} // namespace kai::core
