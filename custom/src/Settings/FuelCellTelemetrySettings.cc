#include "FuelCellTelemetrySettings.h"

#include "../FuelCellTelemetry/FuelCellTelemetry.h"

DECLARE_SETTINGGROUP(FuelCellTelemetry, "FuelCellTelemetry")
{
}

DECLARE_SETTINGSFACT(FuelCellTelemetrySettings, boxAddress)
DECLARE_SETTINGSFACT(FuelCellTelemetrySettings, boxPort)

bool FuelCellTelemetrySettings::saveBoxAddress(const QString &address)
{
    if (!FuelCellTelemetry::isValidBoxAddress(address)) {
        return false;
    }
    boxAddress()->setRawValue(address.trimmed());
    return true;
}
