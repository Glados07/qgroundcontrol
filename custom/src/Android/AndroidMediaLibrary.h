/****************************************************************************
 *
 * Android media-library integration for custom local camera media.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QString>

namespace AndroidMediaLibrary
{

/// Returns an Android shared-media directory on the requested storage volume.
/// The directory is created by the Java bridge before it is returned.
QString mediaDirectory(const QString& preferredStoragePath,
                       const QString& applicationDirectory,
                       const QString& mediaDirectory);

/// Requests Android to index an already finalized local media file.
/// Returns true when the request was accepted by the Java bridge.
bool registerMediaFile(const QString& filePath, const QString& mimeType);

/// Copies legacy custom media into the shared-media directory on a worker
/// thread. The source is removed only after the copied file is indexed.
bool migrateMediaFile(const QString& sourceFilePath,
                      const QString& destinationDirectory,
                      const QString& mimeType);

/// Removes a custom media file through MediaStore when possible so Android
/// does not retain a stale gallery entry.
bool removeMediaFile(const QString& filePath);

} // namespace AndroidMediaLibrary
