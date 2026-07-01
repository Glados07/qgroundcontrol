/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QString>

class QFileInfo;
class Viewer3DSettings;

// 外部 3D 地图导入管理器。
// 作用：把设置页选择的 OBJ/glTF/GLB/QML 或 FBX/DAE/STL/PLY 源文件，统一转成 Viewer3D 可以加载的地图文件路径。
class External3DMapManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString lastImportStatus READ lastImportStatus NOTIFY lastImportStatusChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)

public:
    explicit External3DMapManager(Viewer3DSettings *settings, QObject *parent = nullptr);

    QString lastImportStatus() const { return _lastImportStatus; }
    bool importing() const { return _importing; }

    Q_INVOKABLE void importModelFile(const QString &sourcePath);
    Q_INVOKABLE void clearStatus();
    Q_INVOKABLE bool isDirectLoadableFormat(const QString &sourcePath) const;
    Q_INVOKABLE bool isConversionFormat(const QString &sourcePath) const;
    Q_INVOKABLE QString balsamExecutable() const;
    Q_INVOKABLE QString supportedFormatsText() const;

signals:
    void lastImportStatusChanged();
    void importingChanged();

private:
    void _setLastImportStatus(const QString &statusText);
    void _setImporting(bool importing);
    void _startBalsamConversion(const QFileInfo &sourceInfo);
    void _completeBalsamConversion(int exitCode, QProcess::ExitStatus exitStatus);
    void _handleBalsamError(QProcess::ProcessError error);

    QString _localFilePath(const QString &sourcePath) const;
    QString _extension(const QString &sourcePath) const;
    bool _isDirectLoadableExtension(const QString &extension) const;
    bool _isConversionExtension(const QString &extension) const;
    QString _findBalsamExecutable() const;
    QString _conversionOutputDirectory(const QFileInfo &sourceInfo) const;
    QString _findConvertedQml(const QString &outputDirectory, const QString &preferredBaseName) const;
    QString _shortProcessOutput(const QString &processOutput) const;

private:
    Viewer3DSettings *_settings = nullptr;
    QProcess *_balsamProcess = nullptr;
    QString _pendingSourcePath;
    QString _pendingOutputDirectory;
    QString _lastImportStatus;
    bool _importing = false;
};
