#pragma once

#include "SettingsGroup.h"

class FuelCellTelemetrySettings : public SettingsGroup
{
    Q_OBJECT

public:
    explicit FuelCellTelemetrySettings(QObject *parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()
    DEFINE_SETTINGFACT(boxAddress)
    DEFINE_SETTINGFACT(boxPort)

    Q_INVOKABLE bool saveBoxAddress(const QString &address);
};
