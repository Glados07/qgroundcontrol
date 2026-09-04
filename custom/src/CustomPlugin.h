/****************************************************************************
 *
 * QGroundControl custom build plugin.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QTranslator>
#include <QtCore/QUrl>
#include <QtCore/QMetaObject>
#include <QtQml/QQmlAbstractUrlInterceptor>

#include "QGCCorePlugin.h"

class External3DMapManager;
class UniRcChannelController;
class DualVideoManager;
class FlyViewCustomSettings;
class GimbalAzimuthProvider;
class GimbalCenterCoordinator;
class GimbalControlManager;
class GimbalControlSettings;
class Mt11ControlManager;
class QQmlApplicationEngine;
class VideoCustomSettings;
class Viewer3DSettings;

Q_DECLARE_LOGGING_CATEGORY(CustomLog)

class CustomPlugin : public QGCCorePlugin
{
    Q_OBJECT
    Q_MOC_INCLUDE("custom/src/Viewer3D/External3DMapManager.h")
    Q_MOC_INCLUDE("custom/src/Android/UniRcChannelController.h")
    Q_MOC_INCLUDE("custom/src/Viewer3D/Viewer3DSettings.h")
    Q_MOC_INCLUDE("custom/src/Settings/FlyViewCustomSettings.h")
    Q_MOC_INCLUDE("custom/src/Gimbal/GimbalAzimuthProvider.h")
    Q_MOC_INCLUDE("custom/src/Gimbal/GimbalControlManager.h")
    Q_MOC_INCLUDE("custom/src/Gimbal/GimbalCenterCoordinator.h")
    Q_MOC_INCLUDE("custom/src/Gimbal/GimbalControlSettings.h")
    Q_MOC_INCLUDE("custom/src/Gimbal/Mt11ControlManager.h")
    Q_MOC_INCLUDE("custom/src/Settings/VideoCustomSettings.h")
    Q_MOC_INCLUDE("custom/src/VideoManager/DualVideoManager.h")

    Q_PROPERTY(QObject *viewer3DSettings READ viewer3DSettings CONSTANT)
    Q_PROPERTY(QObject *uniRcChannelController READ uniRcChannelController CONSTANT)
    Q_PROPERTY(QObject *external3DMapManager READ external3DMapManager CONSTANT)
    Q_PROPERTY(QObject *flyViewCustomSettings READ flyViewCustomSettings CONSTANT)
    Q_PROPERTY(QObject *gimbalAzimuthProvider READ gimbalAzimuthProvider CONSTANT)
    Q_PROPERTY(QObject *gimbalControlSettings READ gimbalControlSettings CONSTANT)
    Q_PROPERTY(QObject *gimbalControlManager READ gimbalControlManager CONSTANT)
    Q_PROPERTY(QObject *gimbalCenterCoordinator READ gimbalCenterCoordinator CONSTANT)
    Q_PROPERTY(QObject *mt11ControlManager READ mt11ControlManager CONSTANT)
    Q_PROPERTY(QObject *videoCustomSettings READ videoCustomSettings CONSTANT)
    Q_PROPERTY(QObject *dualVideoManager READ dualVideoManager CONSTANT)
    Q_PROPERTY(bool google3DMapsAvailable READ google3DMapsAvailable CONSTANT)

public:
    explicit CustomPlugin(QObject *parent = nullptr);
    ~CustomPlugin() override;

    static QGCCorePlugin *instance();
    static CustomPlugin *customInstance();

    void init() final;
    void cleanup() final;
    bool adjustSettingMetaData(const QString &settingsGroup, FactMetaData &metaData) final;
    QQmlApplicationEngine *createQmlApplicationEngine(QObject *parent) final;
    void *createVideoSink(QQuickItem *widget, QObject *parent) final;
    bool mavlinkMessage(Vehicle *vehicle, LinkInterface *link, const mavlink_message_t &message) final;

    QObject *viewer3DSettings();
    Viewer3DSettings *viewer3DSettingsFactGroup();
    QObject *external3DMapManager();
    External3DMapManager *external3DMapManagerObject();
    bool google3DMapsAvailable() const;

    QObject *uniRcChannelController();
    UniRcChannelController *uniRcChannelControllerObject();

    QObject *flyViewCustomSettings();
    FlyViewCustomSettings *flyViewCustomSettingsFactGroup();

    QObject *gimbalAzimuthProvider();
    GimbalAzimuthProvider *gimbalAzimuthProviderObject();
    QObject *gimbalControlSettings();
    GimbalControlSettings *gimbalControlSettingsFactGroup();
    QObject *gimbalControlManager();
    GimbalControlManager *gimbalControlManagerObject();
    QObject *gimbalCenterCoordinator();
    GimbalCenterCoordinator *gimbalCenterCoordinatorObject();
    QObject *mt11ControlManager();
    Mt11ControlManager *mt11ControlManagerObject();
    QObject *videoCustomSettings();
    VideoCustomSettings *videoCustomSettingsFactGroup();
    QObject *dualVideoManager();
    DualVideoManager *dualVideoManagerObject();

private:
    void _ensureViewer3DSettings();
    void _ensureUniRcChannelController();
    void _ensureExternal3DMapManager();
    void _ensureFlyViewCustomSettings();
    void _ensureGimbalAzimuthProvider();
    void _ensureGimbalControlSettings();
    void _ensureGimbalControlManager();
    void _ensureGimbalCenterCoordinator();
    void _ensureMt11ControlManager();
    void _ensureVideoCustomSettings();
    void _ensureDualVideoManager();
    void _shutdownMt11Video();

    QQmlApplicationEngine *_qmlEngine = nullptr;
    class CustomOverrideInterceptor *_selector = nullptr;
    QTranslator _customTranslator;

    Viewer3DSettings *_viewer3DSettings = nullptr;
    UniRcChannelController *_uniRcChannelController = nullptr;
    External3DMapManager *_external3DMapManager = nullptr;
    FlyViewCustomSettings *_flyViewCustomSettings = nullptr;
    GimbalAzimuthProvider *_gimbalAzimuthProvider = nullptr;
    GimbalControlSettings *_gimbalControlSettings = nullptr;
    GimbalControlManager *_gimbalControlManager = nullptr;
    GimbalCenterCoordinator *_gimbalCenterCoordinator = nullptr;
    Mt11ControlManager *_mt11ControlManager = nullptr;
    VideoCustomSettings *_videoCustomSettings = nullptr;
    DualVideoManager *_dualVideoManager = nullptr;
    QMetaObject::Connection _aboutToQuitConnection;
};

class CustomOverrideInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    CustomOverrideInterceptor();

    QUrl intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type) final;
};
