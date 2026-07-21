/****************************************************************************
 *
 * QGroundControl custom build plugin implementation.
 *
 ****************************************************************************/

#include "CustomPlugin.h"

#include "AppSettings.h"
#include "Comms/DefaultCommunicationLinkInstaller.h"
#include "FactMetaData.h"
#include "Gimbal/GimbalControlManager.h"
#include "Gimbal/GimbalControlSettings.h"
#include "Gimbal/GimbalVideoStreamSupport.h"
#include "QGCLoggingCategory.h"
#include "Settings/FlyViewCustomSettings.h"
#include "VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.h"
#include "Viewer3D/External3DMapManager.h"
#include "Viewer3D/CustomViewer3DManager.h"
#include "Viewer3D/Viewer3DSettings.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QtCore/QApplicationStatic>
#endif
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QLocale>
#include <QtCore/QStringList>
#include <QtQml/QQmlApplicationEngine>

QGC_LOGGING_CATEGORY(CustomLog, "gcs.custom.customplugin")

Q_APPLICATION_STATIC(CustomPlugin, _customPluginInstance);

CustomPlugin::CustomPlugin(QObject *parent)
    : QGCCorePlugin(parent)
{
}

CustomPlugin::~CustomPlugin() = default;

QGCCorePlugin *CustomPlugin::instance()
{
    return _customPluginInstance();
}

CustomPlugin *CustomPlugin::customInstance()
{
    return _customPluginInstance();
}

void CustomPlugin::init()
{
    // 在 LinkManager 读取 QSettings 前补充缺失的项目默认 UDP 链路。
    DefaultCommunicationLinkInstaller::ensureInstalled();

    const QLocale locale;
    if (locale.name() != QStringLiteral("en_US")) {
        if (_customTranslator.load(locale, QStringLiteral("custom_"), QString(), QStringLiteral(":/i18n"))) {
            QCoreApplication::installTranslator(&_customTranslator);
            qCDebug(CustomLog) << "Loaded custom translation for" << locale.name();
        } else {
            qCDebug(CustomLog) << "No custom translation found for" << locale.name();
        }
    }

    // Viewer3D 与云台由 CustomPlugin 管理；PX4 车辆行为由 CustomFirmwarePlugin 接管。
    _ensureViewer3DSettings();
    _ensureExternal3DMapManager();
    CustomViewer3DManager::registerQmlTypes();

    _ensureFlyViewCustomSettings();

    _ensureGimbalControlSettings();
    _ensureGimbalControlManager();
    AndroidVideoDecoderPolicy::apply(
        _gimbalControlSettings->forceAndroidH265HardwareDecoder()->rawValue().toBool());
    GimbalVideoStreamSupport::installA8MiniDefaults();
}

void CustomPlugin::cleanup()
{
    if (_qmlEngine && _selector) {
        _qmlEngine->removeUrlInterceptor(_selector);
    }

    QCoreApplication::removeTranslator(&_customTranslator);
    delete _selector;
    _selector = nullptr;
}

QObject *CustomPlugin::viewer3DSettings()
{
    return viewer3DSettingsFactGroup();
}

Viewer3DSettings *CustomPlugin::viewer3DSettingsFactGroup()
{
    _ensureViewer3DSettings();
    return _viewer3DSettings;
}

QObject *CustomPlugin::external3DMapManager()
{
    return external3DMapManagerObject();
}

External3DMapManager *CustomPlugin::external3DMapManagerObject()
{
    _ensureExternal3DMapManager();
    return _external3DMapManager;
}

bool CustomPlugin::google3DMapsAvailable() const
{
#if defined(QGC_VIEWER3D_GOOGLE_WEBENGINE)
    return true;
#else
    return false;
#endif
}

QObject *CustomPlugin::flyViewCustomSettings()
{
    return flyViewCustomSettingsFactGroup();
}

FlyViewCustomSettings *CustomPlugin::flyViewCustomSettingsFactGroup()
{
    _ensureFlyViewCustomSettings();
    return _flyViewCustomSettings;
}

QObject *CustomPlugin::gimbalControlSettings()
{
    return gimbalControlSettingsFactGroup();
}

GimbalControlSettings *CustomPlugin::gimbalControlSettingsFactGroup()
{
    _ensureGimbalControlSettings();
    return _gimbalControlSettings;
}

QObject *CustomPlugin::gimbalControlManager()
{
    return gimbalControlManagerObject();
}

GimbalControlManager *CustomPlugin::gimbalControlManagerObject()
{
    _ensureGimbalControlManager();
    return _gimbalControlManager;
}

bool CustomPlugin::mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message)
{
    Q_UNUSED(vehicle);
    Q_UNUSED(link);

    return !GimbalVideoStreamSupport::shouldFilterMavlinkMessage(_gimbalControlSettings, message);
}

bool CustomPlugin::adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData)
{
    const bool visible = QGCCorePlugin::adjustSettingMetaData(settingsGroup, metaData);

#ifdef Q_OS_ANDROID
    if (settingsGroup == AppSettings::settingsGroup &&
        metaData.name() == AppSettings::appFontPointSizeName) {
        // QGC's regular Android baseline is 14 pt; the native integer setting displays 12/14 as 86%.
        metaData.setRawDefaultValue(12U); //设置默认缩放字号12pt
    }
#endif

    return visible;
}

void CustomPlugin::_ensureViewer3DSettings()
{
    if (!_viewer3DSettings) {
        _viewer3DSettings = new Viewer3DSettings(this);
    }
}

void CustomPlugin::_ensureExternal3DMapManager()
{
    _ensureViewer3DSettings();
    if (!_external3DMapManager) {
        _external3DMapManager = new External3DMapManager(_viewer3DSettings, this);
    }
}

void CustomPlugin::_ensureFlyViewCustomSettings()
{
    if (!_flyViewCustomSettings) {
        _flyViewCustomSettings = new FlyViewCustomSettings(this);
    }
}

void CustomPlugin::_ensureGimbalControlSettings()
{
    if (!_gimbalControlSettings) {
        _gimbalControlSettings = new GimbalControlSettings(this);
    }
}

void CustomPlugin::_ensureGimbalControlManager()
{
    _ensureGimbalControlSettings();
    if (!_gimbalControlManager) {
        _gimbalControlManager = new GimbalControlManager(_gimbalControlSettings, this);
    }
}

QQmlApplicationEngine *CustomPlugin::createQmlApplicationEngine(QObject *parent)
{
    _qmlEngine = QGCCorePlugin::createQmlApplicationEngine(parent);
    connect(_qmlEngine, &QObject::destroyed, this, [this]() { _qmlEngine = nullptr; });

    if (!_selector) {
        _selector = new CustomOverrideInterceptor();
    }
    _qmlEngine->addUrlInterceptor(_selector);

    return _qmlEngine;
}

CustomOverrideInterceptor::CustomOverrideInterceptor()
    : QQmlAbstractUrlInterceptor()
{
}

QUrl CustomOverrideInterceptor::intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type)
{
    using DataType = QQmlAbstractUrlInterceptor::DataType;

    switch (type) {
    case DataType::QmlFile:
    case DataType::JavaScriptFile:
    case DataType::QmldirFile:
    case DataType::UrlString:
        if (url.scheme() == QStringLiteral("qrc")) {
            const QString origPath = url.path();
            QStringList overrideCandidates;
            overrideCandidates << QStringLiteral(":/Custom%1").arg(origPath);

            if (origPath.startsWith(QStringLiteral("/qt/qml/"))) {
                overrideCandidates << QStringLiteral(":/Custom/qml/%1").arg(origPath.mid(QStringLiteral("/qt/qml/").size()));
            } else if (origPath.startsWith(QStringLiteral("/QGroundControl/"))) {
                overrideCandidates << QStringLiteral(":/Custom/qml%1").arg(origPath);
            }

            // 只替换 custom.qrc 中实际存在的文件，其他 QML 继续使用 QGC 原生资源。
            for (const QString &overrideResource : overrideCandidates) {
                if (QFile::exists(overrideResource)) {
                    QUrl result;
                    result.setScheme(QStringLiteral("qrc"));
                    result.setPath('/' + overrideResource.mid(2));
                    return result;
                }
            }
        }
        break;
    default:
        break;
    }

    return url;
}
