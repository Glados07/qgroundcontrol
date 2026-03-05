/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "VehicleFuelStatusFactGroup.h"
#include "Vehicle.h"

VehicleFuelStatusFactGroup::VehicleFuelStatusFactGroup(QObject *parent)
    : FactGroup(1000, QStringLiteral(":/json/Vehicle/FuelStatusFact.json"), parent)
{
    _addFact(&_idFact);
    _addFact(&_percentRemainingFact);
    _addFact(&_maximumFuelFact);
    _addFact(&_consumedFuelFact);
    _addFact(&_remainingFuelFact);
    _addFact(&_flowRateFact);
    _addFact(&_temperatureFact);
    _addFact(&_fuelTypeFact);

    _idFact.setRawValue(0);
    _percentRemainingFact.setRawValue(qQNaN());
    _maximumFuelFact.setRawValue(qQNaN());
    _consumedFuelFact.setRawValue(qQNaN());
    _remainingFuelFact.setRawValue(qQNaN());
    _flowRateFact.setRawValue(qQNaN());
    _temperatureFact.setRawValue(qQNaN());
    _fuelTypeFact.setRawValue(0);
}

void VehicleFuelStatusFactGroup::handleMessage(Vehicle *vehicle, const mavlink_message_t &message)
{
    Q_UNUSED(vehicle);

    switch (message.msgid) {
    case MAVLINK_MSG_ID_FUEL_STATUS:
        _handleFuelStatus(message);
        break;
    default:
        break;
    }
}

void VehicleFuelStatusFactGroup::_handleFuelStatus(const mavlink_message_t &message)
{
    mavlink_fuel_status_t fuelStatus{};
    mavlink_msg_fuel_status_decode(&message, &fuelStatus);

    fuelId()->setRawValue(fuelStatus.id);
    percentRemaining()->setRawValue((fuelStatus.percent_remaining == UINT8_MAX) ? qQNaN() : fuelStatus.percent_remaining);
    maximumFuel()->setRawValue(fuelStatus.maximum_fuel);
    consumedFuel()->setRawValue(qIsNaN(fuelStatus.consumed_fuel) ? qQNaN() : fuelStatus.consumed_fuel);
    remainingFuel()->setRawValue(qIsNaN(fuelStatus.remaining_fuel) ? qQNaN() : fuelStatus.remaining_fuel);
    flowRate()->setRawValue(qIsNaN(fuelStatus.flow_rate) ? qQNaN() : fuelStatus.flow_rate);
    temperature()->setRawValue(qIsNaN(fuelStatus.temperature) ? qQNaN() : fuelStatus.temperature);
    fuelType()->setRawValue(fuelStatus.fuel_type);

    _setTelemetryAvailable(true);
}
