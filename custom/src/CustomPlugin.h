/****************************************************************************
 *
 * QGroundControl custom build plugin.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QTranslator>
#include <QtCore/QUrl>
#include <QtQml/QQmlAbstractUrlInterceptor>

#include "QGCCorePlugin.h"

class External3DMapManager;
class FlyViewCustomSettings;
class GimbalControlManager;
class GimbalControlSettings;
class QQmlApplicationEngine;
class Viewer3DSettings;

Q_DECLARE_LOGGING_CATEGORY(CustomLog)

class CustomPlugin : public QGCCorePlugin
{
    Q_OBJECT
    Q_MOC_INCLUDE("custom/src/Viewer3D/External3DMapManager.h")
    Q_MOC_INCLUDE("custom/src/Viewer3D/Viewer3DSettings.h")
    Q_MOC_INCLUDE("custom/src/Settings/FlyViewCustomSettings.h")
    Q_MOC_INCLUDE("custom/src/Gimbal/GimbalControlManager.h")
    Q_MOC_INCLUDE("custom/src/Gimbal/GimbalControlSettings.h")

    Q_PROPERTY(QObject *viewer3DSettings READ viewer3DSettings CONSTANT)
    Q_PROPERTY(QObject *external3DMapManager READ external3DMapManager CONSTANT)
    Q_PROPERTY(QObject *flyViewCustomSettings READ flyViewCustomSettings CONSTANT)
    Q_PROPERTY(QObject *gimbalControlSettings READ gimbalControlSettings CONSTANT)
    Q_PROPERTY(QObject *gimbalControlManager READ gimbalControlManager CONSTANT)
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

    QObject *flyViewCustomSettings();
    FlyViewCustomSettings *flyViewCustomSettingsFactGroup();

    QObject *gimbalControlSettings();
    GimbalControlSettings *gimbalControlSettingsFactGroup();
    QObject *gimbalControlManager();
    GimbalControlManager *gimbalControlManagerObject();

private:
    void _ensureViewer3DSettings();
    void _ensureExternal3DMapManager();
    void _ensureFlyViewCustomSettings();
    void _ensureGimbalControlSettings();
    void _ensureGimbalControlManager();

    QQmlApplicationEngine *_qmlEngine = nullptr;
    class CustomOverrideInterceptor *_selector = nullptr;
    QTranslator _customTranslator;

    Viewer3DSettings *_viewer3DSettings = nullptr;
    External3DMapManager *_external3DMapManager = nullptr;
    FlyViewCustomSettings *_flyViewCustomSettings = nullptr;
    GimbalControlSettings *_gimbalControlSettings = nullptr;
    GimbalControlManager *_gimbalControlManager = nullptr;
};

class CustomOverrideInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    CustomOverrideInterceptor();

    QUrl intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type) final;
};
