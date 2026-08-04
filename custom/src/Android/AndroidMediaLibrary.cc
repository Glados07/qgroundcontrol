/****************************************************************************
 *
 * Android media-library integration for custom local camera media.
 *
 ****************************************************************************/

#include "AndroidMediaLibrary.h"

#include "QGCLoggingCategory.h"

#ifdef Q_OS_ANDROID
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#endif

QGC_LOGGING_CATEGORY(AndroidMediaLibraryLog,
                     "gcs.custom.android.medialibrary")

namespace {

#ifdef Q_OS_ANDROID
static constexpr const char* kJniMediaLibraryClassName =
    "org/mavlink/qgroundcontrol/QGCCustomMediaLibrary";
#endif

} // namespace

namespace AndroidMediaLibrary
{

QString mediaDirectory(const QString& preferredStoragePath,
                       const QString& applicationDirectory,
                       const QString& mediaDirectoryName)
{
#ifdef Q_OS_ANDROID
    if (!QJniObject::isClassAvailable(kJniMediaLibraryClassName)) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media library class is unavailable";
        return QString();
    }

    const QJniObject javaPreferredStoragePath =
        QJniObject::fromString(preferredStoragePath);
    const QJniObject javaApplicationDirectory =
        QJniObject::fromString(applicationDirectory);
    const QJniObject javaMediaDirectory =
        QJniObject::fromString(mediaDirectoryName);
    const QJniObject result = QJniObject::callStaticObjectMethod(
        kJniMediaLibraryClassName,
        "getMediaDirectory",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        javaPreferredStoragePath.object<jstring>(),
        javaApplicationDirectory.object<jstring>(),
        javaMediaDirectory.object<jstring>());

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions() || !result.isValid()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Failed to resolve the Android shared-media directory"
            << mediaDirectoryName;
        return QString();
    }

    return result.toString();
#else
    Q_UNUSED(preferredStoragePath);
    Q_UNUSED(applicationDirectory);
    Q_UNUSED(mediaDirectoryName);
    return QString();
#endif
}

bool registerMediaFile(const QString& filePath, const QString& mimeType)
{
#ifdef Q_OS_ANDROID
    if (filePath.isEmpty()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Refusing to scan an empty media path";
        return false;
    }

    if (!QJniObject::isClassAvailable(kJniMediaLibraryClassName)) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media library class is unavailable";
        return false;
    }

    const QJniObject javaPath = QJniObject::fromString(filePath);
    const QJniObject javaMimeType = QJniObject::fromString(mimeType);
    const jboolean accepted = QJniObject::callStaticMethod<jboolean>(
        kJniMediaLibraryClassName,
        "scanFile",
        "(Ljava/lang/String;Ljava/lang/String;)Z",
        javaPath.object<jstring>(),
        javaMimeType.object<jstring>());

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media scanner request raised a Java exception for"
            << filePath;
        return false;
    }

    if (!accepted) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media scanner rejected" << filePath;
        return false;
    }

    qCInfo(AndroidMediaLibraryLog)
        << "Queued Android media scan for" << filePath;
    return true;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(mimeType);
    return false;
#endif
}

bool migrateMediaFile(const QString& sourceFilePath,
                      const QString& destinationDirectory,
                      const QString& mimeType)
{
#ifdef Q_OS_ANDROID
    if (sourceFilePath.isEmpty() || destinationDirectory.isEmpty()) {
        return false;
    }

    if (!QJniObject::isClassAvailable(kJniMediaLibraryClassName)) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media library class is unavailable";
        return false;
    }

    const QJniObject javaSourcePath =
        QJniObject::fromString(sourceFilePath);
    const QJniObject javaDestinationDirectory =
        QJniObject::fromString(destinationDirectory);
    const QJniObject javaMimeType = QJniObject::fromString(mimeType);
    const jboolean accepted = QJniObject::callStaticMethod<jboolean>(
        kJniMediaLibraryClassName,
        "migrateFile",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
        javaSourcePath.object<jstring>(),
        javaDestinationDirectory.object<jstring>(),
        javaMimeType.object<jstring>());

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media migration raised a Java exception for"
            << sourceFilePath;
        return false;
    }

    return accepted;
#else
    Q_UNUSED(sourceFilePath);
    Q_UNUSED(destinationDirectory);
    Q_UNUSED(mimeType);
    return false;
#endif
}

bool removeMediaFile(const QString& filePath)
{
#ifdef Q_OS_ANDROID
    if (filePath.isEmpty()
        || !QJniObject::isClassAvailable(kJniMediaLibraryClassName)) {
        return false;
    }

    const QJniObject javaPath = QJniObject::fromString(filePath);
    const jboolean removed = QJniObject::callStaticMethod<jboolean>(
        kJniMediaLibraryClassName,
        "deleteFile",
        "(Ljava/lang/String;)Z",
        javaPath.object<jstring>());

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media deletion raised a Java exception for"
            << filePath;
        return false;
    }
    return removed;
#else
    Q_UNUSED(filePath);
    return false;
#endif
}

} // namespace AndroidMediaLibrary
