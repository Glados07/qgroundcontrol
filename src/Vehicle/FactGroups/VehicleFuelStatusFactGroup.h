/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "FactGroup.h"

class VehicleFuelStatusFactGroup : public FactGroup
{
    Q_OBJECT
    Q_PROPERTY(Fact *fuelId             READ fuelId             CONSTANT)
    Q_PROPERTY(Fact *percentRemaining   READ percentRemaining   CONSTANT)
    Q_PROPERTY(Fact *maximumFuel        READ maximumFuel        CONSTANT)
    Q_PROPERTY(Fact *consumedFuel       READ consumedFuel       CONSTANT)
    Q_PROPERTY(Fact *remainingFuel      READ remainingFuel      CONSTANT)
    Q_PROPERTY(Fact *flowRate           READ flowRate           CONSTANT)
    Q_PROPERTY(Fact *temperature        READ temperature        CONSTANT)
    Q_PROPERTY(Fact *fuelType           READ fuelType           CONSTANT)

public:
    explicit VehicleFuelStatusFactGroup(QObject *parent = nullptr);

    Fact *fuelId()           { return &_idFact; }
    Fact *percentRemaining() { return &_percentRemainingFact; }
    Fact *maximumFuel()      { return &_maximumFuelFact; }
    Fact *consumedFuel()     { return &_consumedFuelFact; }
    Fact *remainingFuel()    { return &_remainingFuelFact; }
    Fact *flowRate()         { return &_flowRateFact; }
    Fact *temperature()      { return &_temperatureFact; }
    Fact *fuelType()         { return &_fuelTypeFact; }

    // Overrides from FactGroup
    void handleMessage(Vehicle *vehicle, const mavlink_message_t &message) final;

private:
    void _handleFuelStatus(const mavlink_message_t &message);

    Fact _idFact =                Fact(0, QStringLiteral("id"),                FactMetaData::valueTypeUint8);
    Fact _percentRemainingFact =  Fact(0, QStringLiteral("percentRemaining"),  FactMetaData::valueTypeUint8);
    Fact _maximumFuelFact =       Fact(0, QStringLiteral("maximumFuel"),       FactMetaData::valueTypeFloat);
    Fact _consumedFuelFact =      Fact(0, QStringLiteral("consumedFuel"),      FactMetaData::valueTypeFloat);
    Fact _remainingFuelFact =     Fact(0, QStringLiteral("remainingFuel"),     FactMetaData::valueTypeFloat);
    Fact _flowRateFact =          Fact(0, QStringLiteral("flowRate"),          FactMetaData::valueTypeFloat);
    Fact _temperatureFact =       Fact(0, QStringLiteral("temperature"),       FactMetaData::valueTypeFloat);
    Fact _fuelTypeFact =          Fact(0, QStringLiteral("fuelType"),          FactMetaData::valueTypeUint32);
};
