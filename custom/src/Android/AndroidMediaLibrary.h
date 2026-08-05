/****************************************************************************
 *
 * Android media-library integration for custom local camera media.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

namespace AndroidMediaLibrary
{

/// Returns an app-private Android staging directory on the requested storage
/// volume. Finalized files must be published before the staging source is
/// removed because Android deletes this directory when QGC is uninstalled.
QString mediaStagingDirectory(const QString& preferredStoragePath,
                              const QString& applicationDirectory,
                              const QString& mediaDirectory);

/// Returns all mounted V2 staging and V1 Android/media directories which
/// already exist. Startup uses these paths to retry or migrate media across
/// storage-volume selection changes.
QStringList existingMediaSourceDirectories(
    const QString& applicationDirectory,
    const QString& mediaDirectory);

/// Queues publication of an already finalized staging/legacy file into the
/// public Pictures or Movies MediaStore collection. The source is retained
/// until the public copy is durable and visible to gallery applications.
bool publishMediaFile(const QString& filePath,
                      const QString& mimeType,
                      const QString& applicationDirectory,
                      const QString& mediaDirectory);

/// Queues cleanup of public videos published by the current installation.
/// Public media which survived a previous uninstall is deliberately outside
/// this installation-private cleanup registry.
bool cleanupPublishedVideos(quint64 maximumBytes,
                            const QString& applicationDirectory);

/// Waits for publication tasks which were already queued by this process.
/// Intended only for bounded graceful-shutdown finalization.
bool waitForPendingPublications(quint64 timeoutMilliseconds);

/// Removes a custom media file through MediaStore when possible so Android
/// does not retain a stale gallery entry.
bool removeMediaFile(const QString& filePath);

} // namespace AndroidMediaLibrary
