/****************************************************************************
 *
 * QGroundControl custom build plugin implementation.
 *
 ****************************************************************************/

#include "CustomPlugin.h"

#include "AppSettings.h"
#include "AutoConnectSettings.h"
#include "Comms/DefaultCommunicationLinkInstaller.h"
#include "FactMetaData.h"
#include "Gimbal/GimbalControlManager.h"
#include "Gimbal/GimbalControlSettings.h"
#include "Gimbal/GimbalVideoStreamSupport.h"
#include "Gimbal/Mt11ControlManager.h"
#include "QGCLoggingCategory.h"
#include "Settings/FlyViewCustomSettings.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"
#include "VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.h"
#include "VideoManager/VideoReceiver/GStreamer/PulledVideoResolutionProbe.h"
#include "VideoManager/DualVideoManager.h"
#include "Viewer3D/External3DMapManager.h"
#include "Viewer3D/CustomViewer3DManager.h"
#include "Viewer3D/Viewer3DSettings.h"

#include <QtCore/qapplicationstatic.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QLocale>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QtMath>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuick/QQuickItem>

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
    // Install the missing project default before LinkManager reads settings.
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
    _ensureMt11ControlManager();
    _ensureDualVideoManager();
    if (!_aboutToQuitConnection) {
        _aboutToQuitConnection =
            connect(QCoreApplication::instance(),
                    &QCoreApplication::aboutToQuit,
                    this,
                    [this]() {
                        _shutdownMt11Video();
                        if (_gimbalControlManager) {
                            _gimbalControlManager->shutdownLocalMedia(true);
                        }
                    },
                    Qt::DirectConnection);
    }
    AndroidVideoDecoderPolicy::apply(
        _gimbalControlSettings->forceAndroidH265HardwareDecoder()->rawValue().toBool());
    GimbalVideoStreamSupport::installA8MiniDefaults();
}

void CustomPlugin::cleanup()
{
    _shutdownMt11Video();
    if (_gimbalControlManager) {
        _gimbalControlManager->shutdownLocalMedia(true);
    }

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

QObject *CustomPlugin::mt11ControlManager()
{
    return mt11ControlManagerObject();
}

Mt11ControlManager *CustomPlugin::mt11ControlManagerObject()
{
    _ensureMt11ControlManager();
    return _mt11ControlManager;
}

QObject *CustomPlugin::dualVideoManager()
{
    return dualVideoManagerObject();
}

DualVideoManager *CustomPlugin::dualVideoManagerObject()
{
    _ensureDualVideoManager();
    return _dualVideoManager;
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

    if (settingsGroup == AutoConnectSettings::settingsGroup
        && metaData.name() == AutoConnectSettings::autoConnectUDPName) {
        // Keep the native setting visible and user-controlled, but default it
        // to off when no saved value exists.
        metaData.setRawDefaultValue(false);
    }

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

void CustomPlugin::_ensureMt11ControlManager()
{
    _ensureGimbalControlSettings();
    if (!_mt11ControlManager) {
        _mt11ControlManager = new Mt11ControlManager(_gimbalControlSettings, this);
    }
}

void CustomPlugin::_ensureDualVideoManager()
{
    _ensureGimbalControlSettings();
    _ensureMt11ControlManager();
    if (_dualVideoManager) {
        return;
    }

    _dualVideoManager = new DualVideoManager(_gimbalControlSettings, this);
    connect(_dualVideoManager,
            &DualVideoManager::videoObjectsAboutToBeReleased,
            _mt11ControlManager,
            [manager = QPointer<Mt11ControlManager>(_mt11ControlManager)]() {
                if (!manager) {
                    return;
                }
                manager->shutdownLocalMedia(true);
                manager->setVideoReceiver(nullptr);
                manager->setVideoItem(nullptr);
            },
            Qt::DirectConnection);
    connect(_dualVideoManager,
            &DualVideoManager::videoObjectsReleased,
            _mt11ControlManager,
            [manager = QPointer<Mt11ControlManager>(_mt11ControlManager)]() {
                if (manager) {
                    manager->finalizeDetachedLocalMedia();
                }
            },
            Qt::DirectConnection);
    const auto syncVideoObjects =
        [manager = QPointer<Mt11ControlManager>(_mt11ControlManager),
         dual = QPointer<DualVideoManager>(_dualVideoManager)]() {
            if (!manager || !dual) {
                return;
            }
            manager->setVideoItem(dual->videoItem());
            manager->setVideoReceiver(dual->videoReceiver());
        };
    connect(_dualVideoManager,
            &DualVideoManager::videoReceiverChanged,
            _mt11ControlManager,
            syncVideoObjects);
    connect(_dualVideoManager,
            &DualVideoManager::videoItemChanged,
            _mt11ControlManager,
            syncVideoObjects);
}

void CustomPlugin::_shutdownMt11Video()
{
    // Receiver release has the authoritative stop/detach/finalize sequence.
    // If no receiver exists, run the manager fallback exactly once so a local
    // photo worker or Android publication is still drained on application exit.
    const bool hasReceiver = _dualVideoManager
        && _dualVideoManager->videoReceiver();
    if (_dualVideoManager) {
        _dualVideoManager->cleanup();
    }
    if (!hasReceiver && _mt11ControlManager) {
        _mt11ControlManager->shutdownLocalMedia(true);
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

void *CustomPlugin::createVideoSink(QQuickItem *widget, QObject *parent)
{
    void *sink = QGCCorePlugin::createVideoSink(widget, parent);

    auto *receiver = qobject_cast<VideoReceiver *>(parent);
    const bool isMt11VideoReceiver = receiver
        && _dualVideoManager
        && receiver->parent() == _dualVideoManager;
    const bool isMainVideoReceiver = receiver
        && !receiver->isThermal()
        && !isMt11VideoReceiver;
    GimbalControlManager *manager =
        isMainVideoReceiver ? gimbalControlManagerObject() : nullptr;
    Mt11ControlManager *mt11Manager =
        isMt11VideoReceiver ? mt11ControlManagerObject() : nullptr;

    if (widget && manager) {
        manager->setMainVideoItem(widget);
    }
    if (receiver && manager) {
        manager->setMainVideoReceiver(receiver);
    }
    if (widget && mt11Manager) {
        mt11Manager->setVideoItem(widget);
    }
    if (receiver && mt11Manager) {
        mt11Manager->setVideoReceiver(receiver);
    }

    static constexpr const char* kLocalMediaSignalsConnected =
        "customLocalMediaSignalsConnected";
    if (receiver
        && manager
        && !receiver->property(kLocalMediaSignalsConnected).toBool()) {
        receiver->setProperty(kLocalMediaSignalsConnected, true);
        QPointer<VideoReceiver> guardedReceiver(receiver);
        connect(receiver,
                &VideoReceiver::onStartRecordingComplete,
                manager,
                [manager, guardedReceiver](VideoReceiver::STATUS status) {
                    manager->handleMainVideoRecordingStartResult(
                        status == VideoReceiver::STATUS_OK,
                        guardedReceiver
                            ? guardedReceiver->recordingOutput()
                            : QString());
                });
    }

    static constexpr const char* kMt11LocalMediaSignalsConnected =
        "customMt11LocalMediaSignalsConnected";
    if (receiver
        && mt11Manager
        && !receiver->property(kMt11LocalMediaSignalsConnected).toBool()) {
        receiver->setProperty(kMt11LocalMediaSignalsConnected, true);
        QPointer<VideoReceiver> guardedReceiver(receiver);
        QPointer<Mt11ControlManager> guardedManager(mt11Manager);
        connect(receiver,
                &VideoReceiver::onStartRecordingComplete,
                mt11Manager,
                [guardedManager, guardedReceiver](VideoReceiver::STATUS status) {
                    if (!guardedManager) {
                        return;
                    }
                    guardedManager->handleVideoRecordingStartResult(
                        status == VideoReceiver::STATUS_OK,
                        guardedReceiver
                            ? guardedReceiver->recordingOutput()
                            : QString());
                });
    }

#ifdef QGC_GST_STREAMING
    if (isMainVideoReceiver && manager) {
        QPointer<GimbalControlManager> guardedManager(manager);
        const auto queueNegotiatedResolution =
            [guardedManager](const QSize& videoSize) {
                if (!guardedManager) {
                    return;
                }
                QMetaObject::invokeMethod(
                    guardedManager.data(),
                    [guardedManager, videoSize]() {
                        if (guardedManager) {
                            guardedManager->setNegotiatedPulledVideoResolution(
                                videoSize);
                        }
                    },
                    Qt::QueuedConnection);
            };
        const bool padProbeInstalled =
            PulledVideoResolutionProbe::install(
                sink,
                parent,
                queueNegotiatedResolution);

        bool videoItemObserverInstalled = false;
        if (widget) {
            QPointer<QQuickItem> guardedWidget(widget);
            const auto reportVideoItemResolution =
                [guardedWidget, guardedManager]() {
                    if (!guardedWidget || !guardedManager) {
                        return;
                    }

                    const QSize videoSize(
                        qRound(guardedWidget->implicitWidth()),
                        qRound(guardedWidget->implicitHeight()));
                    guardedManager->setNegotiatedPulledVideoResolution(videoSize);
                };

            connect(widget,
                    &QQuickItem::implicitWidthChanged,
                    manager,
                    reportVideoItemResolution);
            connect(widget,
                    &QQuickItem::implicitHeightChanged,
                    manager,
                    reportVideoItemResolution);
            QMetaObject::invokeMethod(
                manager,
                reportVideoItemResolution,
                Qt::QueuedConnection);
            videoItemObserverInstalled = true;
        }

        const QSize initialImplicitSize(
            widget ? qRound(widget->implicitWidth()) : 0,
            widget ? qRound(widget->implicitHeight()) : 0);
        qCInfo(CustomLog)
            << "Installed main pulled-video resolution observers:"
            << "receiver" << receiver->name()
            << "widgetClass"
            << (widget ? widget->metaObject()->className() : "<null>")
            << "widgetName"
            << (widget ? widget->objectName() : QString())
            << "initialImplicitSize" << initialImplicitSize
            << "videoItem" << videoItemObserverInstalled
            << "padProbe" << padProbeInstalled;
    }
#endif

    return sink;
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
