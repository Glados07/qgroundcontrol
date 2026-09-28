pragma Singleton

import QtQuick

QtObject {
    property QtObject videoManager: QtObject {
        property bool hasVideo: true
    }
    property QtObject multiVehicleManager: QtObject {
        property QtObject activeVehicle: QtObject {
            property QtObject heading: QtObject { property real rawValue: 90 }
            property QtObject gimbalController: QtObject {
                property QtObject activeGimbal: QtObject {}
            }
            property QtObject vehicleLinkManager: QtObject {
                property bool communicationLost: false
            }
            property QtObject parameterManager: QtObject {
                property bool parametersReady: false
            }
        }
    }
    property QtObject corePlugin: QtObject {
        property QtObject gimbalAzimuthProvider: QtObject {
            property bool valid: true
            property real absoluteYaw: 0
        }
        property QtObject flyViewCustomSettings: QtObject {
            property QtObject showHeadingCompassBar: QtObject { property bool rawValue: true }
            property QtObject showGimbalHeadingCompassBar: QtObject { property bool rawValue: true }
        }
    }
    function loadGlobalSetting(key, fallback) { return fallback }
    function loadBoolGlobalSetting(key, fallback) { return fallback }
    function saveGlobalSetting(key, value) {}
    function saveBoolGlobalSetting(key, value) {}
}
