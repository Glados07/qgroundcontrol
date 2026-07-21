/****************************************************************************
 *
 * (c) 2009-2019 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "CustomAutoPilotPlugin.h"

#include "Actuators.h"
#include "ParameterManager.h"
#include "QGCCorePlugin.h"
#include "Vehicle.h"

CustomAutoPilotPlugin::CustomAutoPilotPlugin(Vehicle *vehicle, QObject *parent)
    : PX4AutoPilotPlugin(vehicle, parent)
{
    // 高级模式切换后重新生成设置页列表，使页面显隐立即生效。
    connect(QGCCorePlugin::instance(), &QGCCorePlugin::showAdvancedUIChanged,
            this, &CustomAutoPilotPlugin::_advancedChanged);
}

void CustomAutoPilotPlugin::_advancedChanged(bool advanced)
{
    Q_UNUSED(advanced);

    _components.clear();
    emit vehicleComponentsChanged();
}

const QVariantList &CustomAutoPilotPlugin::vehicleComponents()
{
    if (_components.isEmpty() && !_incorrectParameterVersion) {
        if (!_vehicle) {
            qWarning() << "Internal error";
            return _components;
        }

        if (!_vehicle->parameterManager()->parametersReady()) {
            qWarning() << "Call to vehicleComponents prior to parametersReady";
            return _components;
        }

        const bool showAdvanced = QGCCorePlugin::instance()->showAdvancedUI();
        if (showAdvanced) {
            _airframeComponent = new AirframeComponent(_vehicle, this);
            _airframeComponent->setupTriggerSignals();
            _components.append(QVariant::fromValue(reinterpret_cast<VehicleComponent *>(_airframeComponent)));

            _sensorsComponent = new SensorsComponent(_vehicle, this);
            _sensorsComponent->setupTriggerSignals();
            _components.append(QVariant::fromValue(reinterpret_cast<VehicleComponent *>(_sensorsComponent)));

            _radioComponent = new PX4RadioComponent(_vehicle, this);
            _radioComponent->setupTriggerSignals();
            _components.append(QVariant::fromValue(reinterpret_cast<VehicleComponent *>(_radioComponent)));

            _flightModesComponent = new FlightModesComponent(_vehicle, this);
            _flightModesComponent->setupTriggerSignals();
            _components.append(QVariant::fromValue(reinterpret_cast<VehicleComponent *>(_flightModesComponent)));

            _powerComponent = new PowerComponent(_vehicle, this);
            _powerComponent->setupTriggerSignals();
            _components.append(QVariant::fromValue(reinterpret_cast<VehicleComponent *>(_powerComponent)));

            if (_vehicle->actuators()) {
                _vehicle->actuators()->init();
            }
            if (_vehicle->actuators() && _vehicle->actuators()->showUi()) {
                _actuatorComponent = new ActuatorComponent(_vehicle, this, this);
                _actuatorComponent->setupTriggerSignals();
                _components.append(QVariant::fromValue(static_cast<VehicleComponent *>(_actuatorComponent)));
            } else {
                _motorComponent = new MotorComponent(_vehicle, this, this);
                _motorComponent->setupTriggerSignals();
                _components.append(QVariant::fromValue(static_cast<VehicleComponent *>(_motorComponent)));
            }
        }

        // 安全设置始终保留，普通模式下只向用户开放该页面。
        _safetyComponent = new SafetyComponent(_vehicle, this);
        _safetyComponent->setupTriggerSignals();
        _components.append(QVariant::fromValue(reinterpret_cast<VehicleComponent *>(_safetyComponent)));

        if (showAdvanced) {
            _tuningComponent = new PX4TuningComponent(_vehicle, this);
            _tuningComponent->setupTriggerSignals();
            _components.append(QVariant::fromValue(reinterpret_cast<VehicleComponent *>(_tuningComponent)));
        }
    }

    return _components;
}
