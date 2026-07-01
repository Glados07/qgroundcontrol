/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "External3DMapManager.h"
#include "Fact.h"
#include "Viewer3DSettings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <QtCore/QLibraryInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QtGlobal>

External3DMapManager::External3DMapManager(Viewer3DSettings *settings, QObject *parent)
    : QObject(parent)
    , _settings(settings)
{
}

void External3DMapManager::importModelFile(const QString &sourcePath)
{
    const QString localPath = _localFilePath(sourcePath);
    if (localPath.isEmpty()) {
        _setLastImportStatus(tr("Please select an external 3D model file."));
        return;
    }

    const QFileInfo sourceInfo(localPath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        _setLastImportStatus(tr("External 3D model file does not exist: %1").arg(localPath));
        return;
    }

    const QString extension = _extension(localPath);
    if (_isDirectLoadableExtension(extension)) {
        // OBJ/glTF/GLB/QML 已经是 Viewer3D 可以直接加载的格式，直接写入设置项。
        if (_settings) {
            _settings->external3DMapFilePath()->setRawValue(QDir::toNativeSeparators(sourceInfo.absoluteFilePath()));
        }
        _setLastImportStatus(tr("External 3D model map selected: %1").arg(sourceInfo.fileName()));
        return;
    }

    if (_isConversionExtension(extension)) {
        _startBalsamConversion(sourceInfo);
        return;
    }

    _setLastImportStatus(tr("Unsupported 3D model format '%1'. %2").arg(extension, supportedFormatsText()));
}

void External3DMapManager::clearStatus()
{
    _setLastImportStatus(QString());
}

bool External3DMapManager::isDirectLoadableFormat(const QString &sourcePath) const
{
    return _isDirectLoadableExtension(_extension(sourcePath));
}

bool External3DMapManager::isConversionFormat(const QString &sourcePath) const
{
    return _isConversionExtension(_extension(sourcePath));
}

QString External3DMapManager::balsamExecutable() const
{
    return _findBalsamExecutable();
}

QString External3DMapManager::supportedFormatsText() const
{
    return tr("Direct loading supports OBJ, glTF, GLB and Balsam QML. FBX, DAE, STL and PLY are imported through Qt Balsam conversion.");
}

void External3DMapManager::_setLastImportStatus(const QString &statusText)
{
    if (_lastImportStatus == statusText) {
        return;
    }

    _lastImportStatus = statusText;
    emit lastImportStatusChanged();
}

void External3DMapManager::_setImporting(bool importing)
{
    if (_importing == importing) {
        return;
    }

    _importing = importing;
    emit importingChanged();
}

void External3DMapManager::_startBalsamConversion(const QFileInfo &sourceInfo)
{
    if (_balsamProcess) {
        _setLastImportStatus(tr("Another external 3D model import is already running."));
        return;
    }

    const QString balsamPath = _findBalsamExecutable();
    if (balsamPath.isEmpty()) {
        _setLastImportStatus(tr("Qt Balsam was not found. Install the Qt Quick3D tools for the active Qt Kit, or set QGC_VIEWER3D_BALSAM to the balsam executable path."));
        return;
    }

    const QString outputDirectory = _conversionOutputDirectory(sourceInfo);
    QDir outputDir(outputDirectory);
    if (outputDir.exists() && !outputDir.removeRecursively()) {
        _setLastImportStatus(tr("Unable to clean previous external 3D map import directory: %1").arg(outputDirectory));
        return;
    }
    if (!QDir().mkpath(outputDirectory)) {
        _setLastImportStatus(tr("Unable to create external 3D map import directory: %1").arg(outputDirectory));
        return;
    }

    _pendingSourcePath = sourceInfo.absoluteFilePath();
    _pendingOutputDirectory = outputDirectory;

    _balsamProcess = new QProcess(this);
    _balsamProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(_balsamProcess, &QProcess::errorOccurred, this, &External3DMapManager::_handleBalsamError);
    connect(_balsamProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &External3DMapManager::_completeBalsamConversion);

    // Balsam 负责把 FBX/DAE/STL/PLY 等创作格式转成 Quick3D QML + mesh，之后 Viewer3D 统一加载生成的 QML。
    const QStringList arguments = { QStringLiteral("-o"), outputDirectory, sourceInfo.absoluteFilePath() };
    _setImporting(true);
    _setLastImportStatus(tr("Converting external 3D model with Qt Balsam: %1").arg(sourceInfo.fileName()));
    _balsamProcess->start(balsamPath, arguments);
}

void External3DMapManager::_completeBalsamConversion(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!_balsamProcess) {
        return;
    }

    QProcess *process = _balsamProcess;
    const QString processOutput = QString::fromLocal8Bit(process->readAll());
    _balsamProcess = nullptr;
    process->deleteLater();
    _setImporting(false);

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        _setLastImportStatus(tr("Qt Balsam conversion failed: %1").arg(_shortProcessOutput(processOutput)));
        return;
    }

    const QString qmlFile = _findConvertedQml(_pendingOutputDirectory, QFileInfo(_pendingSourcePath).completeBaseName());
    if (qmlFile.isEmpty()) {
        _setLastImportStatus(tr("Qt Balsam finished, but no generated QML file was found in: %1").arg(_pendingOutputDirectory));
        return;
    }

    if (_settings) {
        _settings->external3DMapFilePath()->setRawValue(QDir::toNativeSeparators(qmlFile));
    }
    _setLastImportStatus(tr("External 3D model map imported: %1").arg(QDir::toNativeSeparators(qmlFile)));
}

void External3DMapManager::_handleBalsamError(QProcess::ProcessError error)
{
    if (!_balsamProcess || error != QProcess::FailedToStart) {
        return;
    }

    const QString errorText = _balsamProcess->errorString();
    _balsamProcess->deleteLater();
    _balsamProcess = nullptr;
    _setImporting(false);
    _setLastImportStatus(tr("Unable to start Qt Balsam: %1").arg(errorText));
}

QString External3DMapManager::_localFilePath(const QString &sourcePath) const
{
    QString path = sourcePath.trimmed();
    if (path.isEmpty()) {
        return QString();
    }

    const QUrl url(path);
    if (url.isValid() && url.isLocalFile()) {
        path = url.toLocalFile();
    }

    return QDir::toNativeSeparators(path);
}

QString External3DMapManager::_extension(const QString &sourcePath) const
{
    return QFileInfo(_localFilePath(sourcePath)).suffix().toLower();
}

bool External3DMapManager::_isDirectLoadableExtension(const QString &extension) const
{
    static const QStringList directExtensions = {
        QStringLiteral("obj"),
        QStringLiteral("gltf"),
        QStringLiteral("glb"),
        QStringLiteral("qml")
    };
    return directExtensions.contains(extension.toLower());
}

bool External3DMapManager::_isConversionExtension(const QString &extension) const
{
    static const QStringList conversionExtensions = {
        QStringLiteral("fbx"),
        QStringLiteral("dae"),
        QStringLiteral("stl"),
        QStringLiteral("ply")
    };
    return conversionExtensions.contains(extension.toLower());
}

QString External3DMapManager::_findBalsamExecutable() const
{
#if defined(Q_OS_WIN)
    const QString balsamFileName = QStringLiteral("balsam.exe");
#else
    const QString balsamFileName = QStringLiteral("balsam");
#endif

    QStringList candidates;
    const QString envPath = qEnvironmentVariable("QGC_VIEWER3D_BALSAM");
    if (!envPath.isEmpty()) {
        candidates << envPath;
    }

    candidates << QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(balsamFileName);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    candidates << QDir(QLibraryInfo::path(QLibraryInfo::BinariesPath)).absoluteFilePath(balsamFileName);
#else
    candidates << QDir(QLibraryInfo::location(QLibraryInfo::BinariesPath)).absoluteFilePath(balsamFileName);
#endif

    for (const QString &candidate : candidates) {
        const QFileInfo candidateInfo(candidate);
        if (candidateInfo.exists() && candidateInfo.isFile() && candidateInfo.isExecutable()) {
            return candidateInfo.absoluteFilePath();
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("balsam"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

#if defined(Q_OS_WIN)
    return QStandardPaths::findExecutable(QStringLiteral("balsam.exe"));
#else
    return QString();
#endif
}

QString External3DMapManager::_conversionOutputDirectory(const QFileInfo &sourceInfo) const
{
    QString rootDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (rootDirectory.isEmpty()) {
        rootDirectory = QDir::tempPath();
    }

    QString baseName = sourceInfo.completeBaseName();
    baseName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("external_3d_map");
    }

    const QByteArray hash = QCryptographicHash::hash(sourceInfo.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(10);
    return QDir(rootDirectory).absoluteFilePath(QStringLiteral("Viewer3DExternalMaps/%1_%2").arg(baseName, QString::fromLatin1(hash)));
}

QString External3DMapManager::_findConvertedQml(const QString &outputDirectory, const QString &preferredBaseName) const
{
    QStringList qmlFiles;
    QDirIterator iterator(outputDirectory, { QStringLiteral("*.qml") }, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        qmlFiles << iterator.next();
    }

    if (qmlFiles.isEmpty()) {
        return QString();
    }

    const QString preferredFileName = preferredBaseName + QStringLiteral(".qml");
    for (const QString &qmlFile : qmlFiles) {
        if (QFileInfo(qmlFile).fileName().compare(preferredFileName, Qt::CaseInsensitive) == 0) {
            return QFileInfo(qmlFile).absoluteFilePath();
        }
    }

    for (const QString &qmlFile : qmlFiles) {
        if (QFileInfo(qmlFile).fileName().compare(QStringLiteral("main.qml"), Qt::CaseInsensitive) == 0) {
            return QFileInfo(qmlFile).absoluteFilePath();
        }
    }

    return QFileInfo(qmlFiles.first()).absoluteFilePath();
}

QString External3DMapManager::_shortProcessOutput(const QString &processOutput) const
{
    QString text = processOutput.simplified();
    if (text.isEmpty()) {
        text = tr("no diagnostic output");
    }
    return text.left(700);
}
