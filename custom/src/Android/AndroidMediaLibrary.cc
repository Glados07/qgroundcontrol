/****************************************************************************
 *
 * Android media-library integration for custom local camera media.
 *
 ****************************************************************************/

#include "AndroidMediaLibrary.h"

#include "QGCLoggingCategory.h"

#include <limits>

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

QString callMediaDirectoryResolver(const char* methodName,
                                   const QString& preferredStoragePath,
                                   const QString& applicationDirectory,
                                   const QString& mediaDirectoryName)
{
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
        methodName,
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        javaPreferredStoragePath.object<jstring>(),
        javaApplicationDirectory.object<jstring>(),
        javaMediaDirectory.object<jstring>());

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions() || !result.isValid()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Failed to resolve Android media directory through"
            << methodName << "for" << mediaDirectoryName;
        return QString();
    }

    return result.toString();
}
#endif

} // namespace

namespace AndroidMediaLibrary
{

QString mediaStagingDirectory(const QString& preferredStoragePath,
                              const QString& applicationDirectory,
                              const QString& mediaDirectoryName)
{
#ifdef Q_OS_ANDROID
    return callMediaDirectoryResolver(
        "getMediaStagingDirectory",
        preferredStoragePath,
        applicationDirectory,
        mediaDirectoryName);
#else
    Q_UNUSED(preferredStoragePath);
    Q_UNUSED(applicationDirectory);
    Q_UNUSED(mediaDirectoryName);
    return QString();
#endif
}

QStringList existingMediaSourceDirectories(
    const QString& applicationDirectory,
    const QString& mediaDirectoryName)
{
#ifdef Q_OS_ANDROID
    if (!QJniObject::isClassAvailable(kJniMediaLibraryClassName)) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media library class is unavailable";
        return QStringList();
    }

    const QJniObject javaApplicationDirectory =
        QJniObject::fromString(applicationDirectory);
    const QJniObject javaMediaDirectory =
        QJniObject::fromString(mediaDirectoryName);
    const QJniObject result = QJniObject::callStaticObjectMethod(
        kJniMediaLibraryClassName,
        "getExistingMediaSourceDirectories",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        javaApplicationDirectory.object<jstring>(),
        javaMediaDirectory.object<jstring>());

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions() || !result.isValid()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Failed to enumerate existing Android media sources for"
            << mediaDirectoryName;
        return QStringList();
    }

    return result.toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    Q_UNUSED(applicationDirectory);
    Q_UNUSED(mediaDirectoryName);
    return QStringList();
#endif
}

bool publishMediaFile(const QString& filePath,
                      const QString& mimeType,
                      const QString& applicationDirectory,
                      const QString& mediaDirectoryName)
{
#ifdef Q_OS_ANDROID
    if (filePath.isEmpty()
        || mimeType.isEmpty()
        || applicationDirectory.isEmpty()
        || mediaDirectoryName.isEmpty()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Refusing to publish incomplete Android media parameters";
        return false;
    }

    if (!QJniObject::isClassAvailable(kJniMediaLibraryClassName)) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media library class is unavailable";
        return false;
    }

    const QJniObject javaPath = QJniObject::fromString(filePath);
    const QJniObject javaMimeType = QJniObject::fromString(mimeType);
    const QJniObject javaApplicationDirectory =
        QJniObject::fromString(applicationDirectory);
    const QJniObject javaMediaDirectory =
        QJniObject::fromString(mediaDirectoryName);
    const jboolean accepted = QJniObject::callStaticMethod<jboolean>(
        kJniMediaLibraryClassName,
        "publishFile",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
        javaPath.object<jstring>(),
        javaMimeType.object<jstring>(),
        javaApplicationDirectory.object<jstring>(),
        javaMediaDirectory.object<jstring>());

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media publication raised a Java exception for"
            << filePath;
        return false;
    }

    if (!accepted) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media publisher rejected" << filePath;
        return false;
    }

    qCInfo(AndroidMediaLibraryLog)
        << "Queued durable Android public-media publication for" << filePath;
    return true;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(mimeType);
    Q_UNUSED(applicationDirectory);
    Q_UNUSED(mediaDirectoryName);
    return false;
#endif
}

bool cleanupPublishedVideos(quint64 maximumBytes,
                            const QString& applicationDirectory)
{
#ifdef Q_OS_ANDROID
    if (applicationDirectory.isEmpty()
        || !QJniObject::isClassAvailable(kJniMediaLibraryClassName)) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media library class is unavailable";
        return false;
    }

    const quint64 maximumJniBytes = static_cast<quint64>(
        std::numeric_limits<jlong>::max());
    const jlong javaMaximumBytes = static_cast<jlong>(
        qMin(maximumBytes, maximumJniBytes));
    const QJniObject javaApplicationDirectory =
        QJniObject::fromString(applicationDirectory);
    const jboolean accepted = QJniObject::callStaticMethod<jboolean>(
        kJniMediaLibraryClassName,
        "cleanupPublishedVideos",
        "(JLjava/lang/String;)Z",
        javaMaximumBytes,
        javaApplicationDirectory.object<jstring>());

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android public-video cleanup raised a Java exception";
        return false;
    }

    return accepted;
#else
    Q_UNUSED(maximumBytes);
    Q_UNUSED(applicationDirectory);
    return false;
#endif
}

bool waitForPendingPublications(quint64 timeoutMilliseconds)
{
#ifdef Q_OS_ANDROID
    if (!QJniObject::isClassAvailable(kJniMediaLibraryClassName)) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android media library class is unavailable";
        return false;
    }

    const quint64 maximumJniTimeout = static_cast<quint64>(
        std::numeric_limits<jlong>::max());
    const jlong javaTimeout = static_cast<jlong>(
        qMin(timeoutMilliseconds, maximumJniTimeout));
    const jboolean completed = QJniObject::callStaticMethod<jboolean>(
        kJniMediaLibraryClassName,
        "waitForPendingPublications",
        "(J)Z",
        javaTimeout);

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions()) {
        qCWarning(AndroidMediaLibraryLog)
            << "Android publication shutdown wait raised a Java exception";
        return false;
    }
    return completed;
#else
    Q_UNUSED(timeoutMilliseconds);
    return true;
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
