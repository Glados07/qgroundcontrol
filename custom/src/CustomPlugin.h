/****************************************************************************
 *
 * (c) 2009-2019 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 *   @brief Custom QGCCorePlugin Declaration
 *   @author Gus Grubba <gus@auterion.com>
 */

#pragma once

#include <QtCore/QTranslator>
#include <QtCore/QUrl>
#include <QtQml/QQmlAbstractUrlInterceptor>

#include "QGCCorePlugin.h"
#include "QGCOptions.h"

class CustomOptions;
class CustomPlugin;
class CustomSettings;
class External3DMapManager;
class GimbalControlManager;
class GimbalControlSettings;
class QQmlApplicationEngine;
class Viewer3DSettings;

Q_DECLARE_LOGGING_CATEGORY(CustomLog)

class CustomFlyViewOptions : public QGCFlyViewOptions
{
public:
    CustomFlyViewOptions(CustomOptions* options, QObject* parent = nullptr);

    // Overrides from CustomFlyViewOptions
    bool                    showInstrumentPanel         (void) const final;
    bool                    showMultiVehicleList        (void) const final;
};

/*===========================================================================*/

class CustomOptions : public QGCOptions
{
public:
    CustomOptions(CustomPlugin *plugin, QObject* parent = nullptr);

    // Overrides from QGCOptions
    bool                    wifiReliableForCalibration  (void) const final;
    bool                    showFirmwareUpgrade         (void) const final;
    QGCFlyViewOptions*      flyViewOptions(void) const final;

private:
    QGCCorePlugin *_plugin = nullptr;
    CustomFlyViewOptions *_flyViewOptions = nullptr;
};

/*===========================================================================*/

class CustomPlugin : public QGCCorePlugin
{
    Q_OBJECT
    Q_MOC_INCLUDE("custom/src/Viewer3D/External3DMapManager.h")
    Q_MOC_INCLUDE("custom/src/Viewer3D/Viewer3DSettings.h")
    Q_MOC_INCLUDE("custom/src/Gimbalcontrol/GimbalControlManager.h")
    Q_MOC_INCLUDE("custom/src/Gimbalcontrol/GimbalControlSettings.h")
    Q_PROPERTY(QObject *viewer3DSettings READ viewer3DSettings CONSTANT)
    Q_PROPERTY(QObject *external3DMapManager READ external3DMapManager CONSTANT)
    Q_PROPERTY(QObject *gimbalControlSettings READ gimbalControlSettings CONSTANT)
    Q_PROPERTY(QObject *gimbalControlManager READ gimbalControlManager CONSTANT)
    Q_PROPERTY(bool google3DMapsAvailable READ google3DMapsAvailable CONSTANT)
public:
    explicit CustomPlugin(QObject *parent = nullptr);
    ~CustomPlugin();

    static QGCCorePlugin *instance();
    static CustomPlugin *customInstance();

    // Overrides from QGCCorePlugin
    void init() final;
    void cleanup() final;
    QGCOptions*             options                         (void) final;
    QString                 brandImageIndoor                (void) const final;
    QString                 brandImageOutdoor               (void) const final;
    bool                    overrideSettingsGroupVisibility (const QString &name) final;
    bool                    adjustSettingMetaData           (const QString& settingsGroup, FactMetaData& metaData) final;
    void                    paletteOverride                 (const QString &colorName, QGCPalette::PaletteColorInfo_t& colorInfo) final;
    QQmlApplicationEngine*  createQmlApplicationEngine      (QObject* parent) final;

    // Viewer3D 对 QML 暴露的独立接口：设置、外部模型导入管理器和 Google 3D 能力。
    QObject*                viewer3DSettings                ();
    Viewer3DSettings*       viewer3DSettingsFactGroup       ();
    QObject*                external3DMapManager            ();
    External3DMapManager*   external3DMapManagerObject      ();
    bool                    google3DMapsAvailable           () const;

    // 思翼云台缩放控制模块：独立于 QGC 原生 MAVLink GimbalController。
    QObject*                gimbalControlSettings           ();
    GimbalControlSettings*  gimbalControlSettingsFactGroup  ();
    QObject*                gimbalControlManager            ();
    GimbalControlManager*   gimbalControlManagerObject      ();

private slots:
    void _advancedChanged(bool advanced);

private:
    void _addSettingsEntry(const QString& title, const char* qmlFile, const char* iconFile = nullptr);
    void _ensureViewer3DSettings();
    void _ensureExternal3DMapManager();
    void _ensureGimbalControlSettings();
    void _ensureGimbalControlManager();

private:
    CustomOptions*  _options = nullptr;
    QQmlApplicationEngine *_qmlEngine = nullptr;
    class CustomOverrideInterceptor *_selector = nullptr;
    QTranslator     _customTranslator;
    QVariantList    _customSettingsList; // Not to be mixed up with QGCCorePlugin implementation

    // Viewer3D 保持为独立模块，生命周期挂在 CustomPlugin 下，避免和项目原有飞控插件逻辑耦合。
    Viewer3DSettings *_viewer3DSettings = nullptr;
    External3DMapManager *_external3DMapManager = nullptr;

    // 思翼云台缩放控制保持在 Gimbalcontrol 模块中，避免和 Viewer3D/燃料监测/QGC 原生云台控制混合。
    GimbalControlSettings *_gimbalControlSettings = nullptr;
    GimbalControlManager *_gimbalControlManager = nullptr;
};

/*===========================================================================*/

class CustomOverrideInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    CustomOverrideInterceptor();

    QUrl intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type) final;
};
