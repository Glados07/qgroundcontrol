/****************************************************************************
 *
 * (c) 2009-2019 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "CustomFirmwarePluginFactory.h"

#include "CustomFirmwarePlugin.h"

CustomFirmwarePluginFactory CustomFirmwarePluginFactoryImp;

CustomFirmwarePluginFactory::CustomFirmwarePluginFactory() = default;

QList<QGCMAVLink::FirmwareClass_t> CustomFirmwarePluginFactory::supportedFirmwareClasses() const
{
    return {QGCMAVLink::FirmwareClassPX4};
}

QList<QGCMAVLink::VehicleClass_t> CustomFirmwarePluginFactory::supportedVehicleClasses() const
{
    return {QGCMAVLink::VehicleClassMultiRotor};
}

FirmwarePlugin *CustomFirmwarePluginFactory::firmwarePluginForAutopilot(MAV_AUTOPILOT autopilotType, MAV_TYPE vehicleType)
{
    Q_UNUSED(vehicleType);

    if (autopilotType != MAV_AUTOPILOT_PX4) {
        return nullptr;
    }

    if (!_pluginInstance) {
        _pluginInstance = new CustomFirmwarePlugin;
    }
    return _pluginInstance;
}
