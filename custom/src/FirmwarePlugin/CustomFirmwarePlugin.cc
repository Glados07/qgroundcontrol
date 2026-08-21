/****************************************************************************
 *
 * (c) 2009-2019 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "CustomFirmwarePlugin.h"

#include "CustomAutoPilotPlugin.h"
#include "FactMetaData.h"
#include "Vehicle.h"
#include "px4_custom_mode.h"

namespace {

struct UavcanPowerParameter {
    const char *name;
    const char *severity;
    float defaultVoltage;
};

constexpr UavcanPowerParameter kUavcanPowerParameters[] = {
    {"UAVCAN_POW_LOW",   "low",       47.4f},
    {"UAVCAN_POW_CRITI", "critical",  45.6f},
    {"UAVCAN_POW_EMERG", "emergency", 44.4f},
};

const UavcanPowerParameter *findUavcanPowerParameter(const QString &name)
{
    for (const UavcanPowerParameter &parameter : kUavcanPowerParameters) {
        if (name == QLatin1StringView(parameter.name)) {
            return &parameter;
        }
    }

    return nullptr;
}

} // namespace

CustomFirmwarePlugin::CustomFirmwarePlugin()
{
    // feature 分支只允许暂停、返航和任务模式从常规模式列表中直接设置。
    for (auto &mode : _flightModeList) {
        if (mode.mode_name != pauseFlightMode()
            && mode.mode_name != rtlFlightMode()
            && mode.mode_name != missionFlightMode()) {
            mode.canBeSet = false;
        }
    }
}

AutoPilotPlugin *CustomFirmwarePlugin::autopilotPlugin(Vehicle *vehicle) const
{
    return new CustomAutoPilotPlugin(vehicle, vehicle);
}

FactMetaData *CustomFirmwarePlugin::_getMetaDataForFact(QObject *parameterMetaData, const QString &name,
                                                        FactMetaData::ValueType_t type, MAV_TYPE vehicleType) const
{
    FactMetaData *const metaData = PX4FirmwarePlugin::_getMetaDataForFact(parameterMetaData, name, type, vehicleType);
    const UavcanPowerParameter *const parameter = findUavcanPowerParameter(name);
    if (!metaData || !parameter) {
        return metaData;
    }

    const QString severity = QString::fromLatin1(parameter->severity);
    const QString description = QStringLiteral("Total voltage below which a UAVCAN battery reports a %1 warning. "
                                               "Thresholds must satisfy UAVCAN_POW_LOW > UAVCAN_POW_CRITI > "
                                               "UAVCAN_POW_EMERG > 0.")
                                    .arg(severity);

    metaData->setName(name);
    metaData->setGroup(QStringLiteral("UAVCAN"));
    metaData->setShortDescription(QStringLiteral("UAVCAN battery %1 voltage threshold").arg(severity));
    metaData->setLongDescription(description);
    metaData->setRawDefaultValue(parameter->defaultVoltage);
    metaData->setRawUnits(QStringLiteral("V"));
    metaData->setDecimalPlaces(2);
    metaData->setRawIncrement(0.1);
    metaData->setVehicleRebootRequired(true);

    return metaData;
}

const QVariantList &CustomFirmwarePlugin::toolIndicators(const Vehicle *vehicle)
{
    if (_toolIndicatorList.isEmpty()) {
        // 先继承 QGC 原生车辆指示器，再应用 feature 分支的 RC/Fuel 定制。
        _toolIndicatorList = FirmwarePlugin::toolIndicators(vehicle);
        // Then specifically remove the RC RSSI indicator.
        _toolIndicatorList.removeOne(QVariant::fromValue(QUrl::fromUserInput("qrc:/qml/QGroundControl/Toolbar/RCRSSIIndicator.qml")));

        const QVariant batteryIndicator = QVariant::fromValue(QUrl::fromUserInput("qrc:/qml/QGroundControl/Controls/BatteryIndicator.qml"));
        const QVariant fuelStatusIndicator = QVariant::fromValue(QUrl::fromUserInput("qrc:/Custom/qml/QGroundControl/Toolbar/FuelStatusIndicator.qml"));
        const auto batteryIndex = _toolIndicatorList.indexOf(batteryIndicator);

        // Keep Fuel Status immediately after Battery so conditional indicators cannot shift it.
        if (batteryIndex >= 0) {
            _toolIndicatorList.insert(batteryIndex + 1, fuelStatusIndicator);
        } else {
            _toolIndicatorList.append(fuelStatusIndicator);
        }

        const QVariant gpsIndicator = QVariant::fromValue(QUrl::fromUserInput("qrc:/qml/QGroundControl/Toolbar/VehicleGPSIndicator.qml"));
        const QVariant proximityRadarIndicator = QVariant::fromValue(QUrl::fromUserInput("qrc:/Custom/qml/QGroundControl/Toolbar/ProximityRadarIndicator.qml"));
        const auto gpsIndex = _toolIndicatorList.indexOf(gpsIndicator);

        // Keep Proximity Radar next to GPS with the other vehicle sensor indicators.
        if (gpsIndex >= 0) {
            _toolIndicatorList.insert(gpsIndex + 1, proximityRadarIndicator);
        } else {
            _toolIndicatorList.append(proximityRadarIndicator);
        }
    }
    return _toolIndicatorList;
}

bool CustomFirmwarePlugin::hasGimbal(Vehicle *vehicle, bool &rollSupported, bool &pitchSupported, bool &yawSupported) const
{
    Q_UNUSED(vehicle);

    rollSupported = false;
    pitchSupported = true;
    yawSupported = true;
    return true;
}

void CustomFirmwarePlugin::updateAvailableFlightModes(FlightModeList &modeList)
{
    for (auto &mode : modeList) {
        const auto customMode = static_cast<PX4CustomMode::Mode>(mode.custom_mode);

        switch (customMode) {
        case PX4CustomMode::MANUAL:
        case PX4CustomMode::STABILIZED:
        case PX4CustomMode::ACRO:
        case PX4CustomMode::RATTITUDE:
        case PX4CustomMode::ALTCTL:
        case PX4CustomMode::OFFBOARD:
        case PX4CustomMode::SIMPLE:
        case PX4CustomMode::POSCTL_POSCTL:
        case PX4CustomMode::AUTO_LOITER:
        case PX4CustomMode::AUTO_MISSION:
        case PX4CustomMode::AUTO_RTL:
        case PX4CustomMode::AUTO_FOLLOW_TARGET:
        case PX4CustomMode::AUTO_LAND:
        case PX4CustomMode::AUTO_PRECLAND:
        case PX4CustomMode::AUTO_READY:
        case PX4CustomMode::AUTO_RTGS:
        case PX4CustomMode::AUTO_TAKEOFF:
            mode.multiRotor = true;
            break;
        case PX4CustomMode::POSCTL_ORBIT:
            mode.multiRotor = false;
            break;
        }

        switch (customMode) {
        case PX4CustomMode::OFFBOARD:
        case PX4CustomMode::SIMPLE:
        case PX4CustomMode::POSCTL_ORBIT:
        case PX4CustomMode::AUTO_FOLLOW_TARGET:
        case PX4CustomMode::AUTO_PRECLAND:
            mode.fixedWing = false;
            break;
        case PX4CustomMode::MANUAL:
        case PX4CustomMode::STABILIZED:
        case PX4CustomMode::ACRO:
        case PX4CustomMode::RATTITUDE:
        case PX4CustomMode::ALTCTL:
        case PX4CustomMode::POSCTL_POSCTL:
        case PX4CustomMode::AUTO_LOITER:
        case PX4CustomMode::AUTO_MISSION:
        case PX4CustomMode::AUTO_RTL:
        case PX4CustomMode::AUTO_LAND:
        case PX4CustomMode::AUTO_READY:
        case PX4CustomMode::AUTO_RTGS:
        case PX4CustomMode::AUTO_TAKEOFF:
            mode.fixedWing = true;
            break;
        }

        switch (customMode) {
        case PX4CustomMode::AUTO_LOITER:
        case PX4CustomMode::AUTO_RTL:
        case PX4CustomMode::AUTO_MISSION:
            mode.canBeSet = true;
            break;
        case PX4CustomMode::OFFBOARD:
        case PX4CustomMode::SIMPLE:
        case PX4CustomMode::POSCTL_ORBIT:
        case PX4CustomMode::AUTO_FOLLOW_TARGET:
        case PX4CustomMode::AUTO_PRECLAND:
        case PX4CustomMode::MANUAL:
        case PX4CustomMode::STABILIZED:
        case PX4CustomMode::ACRO:
        case PX4CustomMode::RATTITUDE:
        case PX4CustomMode::ALTCTL:
        case PX4CustomMode::POSCTL_POSCTL:
        case PX4CustomMode::AUTO_LAND:
        case PX4CustomMode::AUTO_READY:
        case PX4CustomMode::AUTO_RTGS:
        case PX4CustomMode::AUTO_TAKEOFF:
            mode.canBeSet = false;
            break;
        }
    }

    _updateFlightModeList(modeList);
}
