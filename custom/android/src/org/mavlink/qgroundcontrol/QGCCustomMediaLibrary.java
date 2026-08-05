package org.mavlink.qgroundcontrol;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.MediaStore;
import android.util.Log;

/** QGC_CUSTOM_ANDROID_MEDIA_LIBRARY_V2
 * Publishes finalized custom camera files into durable public MediaStore
 * collections. App-specific directories are staging only and may be removed
 * by Android when QGC is uninstalled.
 */
public final class QGCCustomMediaLibrary {
    private static final String TAG = "QGCCustomMedia-Custom";
    private static final int COPY_BUFFER_SIZE = 1024 * 1024;
    private static final long MEDIA_SCAN_TIMEOUT_SECONDS = 30L;
    private static final String PHOTO_DIRECTORY = "Photo";
    private static final String VIDEO_DIRECTORY = "Video";
    private static final String PUBLICATION_PREFERENCES =
        "QGCCustomPublicMediaV2";
    private static final String PENDING_URI_SET = "pendingUris";
    private static final String VIDEO_URI_SET = "publishedVideoUris";
    private static final String SOURCE_CLEANUP_URI_SET = "sourceCleanupUris";
    private static final String INSTALLATION_MARKER_FILE =
        "qgc_custom_public_media_v2.install";
    private static final Pattern CUSTOM_VIDEO_PATTERN = Pattern.compile(
        ".*_local_\\d{3,}[^/\\\\]*\\.(?:mkv|mov|mp4)$",
        Pattern.CASE_INSENSITIVE);
    private static final Pattern LOCAL_MEDIA_STEM_PATTERN = Pattern.compile(
        "^(.*)(_local_\\d{3,})$",
        Pattern.CASE_INSENSITIVE);

    private static final Object PUBLICATION_LOCK = new Object();
    private static final Set<String> QUEUED_SOURCE_PATHS = new HashSet<>();
    private static final Set<String> FAILED_SOURCE_PATHS = new HashSet<>();
    private static boolean stalePendingRecovered = false;
    private static boolean pendingRecoveryQueued = false;
    private static boolean installationRegistryValidated = false;
    private static final ExecutorService PUBLICATION_EXECUTOR =
        Executors.newSingleThreadExecutor(new ThreadFactory() {
            @Override
            public Thread newThread(final Runnable runnable) {
                final Thread thread =
                    new Thread(runnable, "QGCCustomMediaPublisher");
                thread.setDaemon(true);
                return thread;
            }
        });

    private static final class PublicationTarget {
        final boolean photo;
        final String relativePath;
        final Uri collectionUri;

        PublicationTarget(final boolean photo,
                          final String relativePath,
                          final Uri collectionUri) {
            this.photo = photo;
            this.relativePath = relativePath;
            this.collectionUri = collectionUri;
        }
    }

    private static final class ExistingMedia {
        final Uri uri;
        final long size;
        final boolean pending;

        ExistingMedia(final Uri uri, final long size, final boolean pending) {
            this.uri = uri;
            this.size = size;
            this.pending = pending;
        }
    }

    private static final class PublishedVideo {
        final Uri uri;
        final long size;
        final long dateAdded;

        PublishedVideo(final Uri uri, final long size, final long dateAdded) {
            this.uri = uri;
            this.size = size;
            this.dateAdded = dateAdded;
        }
    }

    private static final class PendingMediaState {
        final boolean exists;
        final boolean pending;
        final boolean video;

        PendingMediaState(final boolean exists,
                          final boolean pending,
                          final boolean video) {
            this.exists = exists;
            this.pending = pending;
            this.video = video;
        }
    }

    private QGCCustomMediaLibrary() {
    }

    /**
     * Returns an app-specific filesystem directory used only while encoding a
     * photo or while GStreamer is finalizing a video container. Final media is
     * copied into public Pictures/Movies before this staging file is removed.
     */
    public static String getMediaStagingDirectory(
            final String preferredStoragePath,
            final String applicationDirectory,
            final String mediaDirectory) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null) {
            Log.e(TAG, "Cannot resolve media staging directory: QGCActivity is unavailable");
            return "";
        }
        if (!isSafeDirectoryName(applicationDirectory)
            || !isSupportedMediaDirectory(mediaDirectory)) {
            Log.e(TAG, "Cannot resolve media staging directory: invalid directory name");
            return "";
        }

        queuePendingRecovery(activity.getApplicationContext());
        final File selectedRoot = selectAppSpecificDirectory(
            activity,
            preferredStoragePath,
            activity.getExternalFilesDirs(null));
        if (selectedRoot == null) {
            Log.e(TAG, "No mounted Android media staging directory is available");
            return "";
        }

        final File result = new File(
            new File(new File(selectedRoot, applicationDirectory), "Staging"),
            mediaDirectory);
        if (!result.isDirectory() && !result.mkdirs()) {
            Log.e(TAG, "Failed to create media staging directory: " + result);
            return "";
        }

        Log.i(TAG, "Using app-private media staging directory: "
                   + result.getAbsolutePath());
        return result.getAbsolutePath();
    }

    /**
     * Enumerates every currently mounted V2 staging directory and V1
     * Android/media directory without creating them. This prevents a storage
     * selection change from stranding media on a previously selected volume.
     */
    public static String getExistingMediaSourceDirectories(
            final String applicationDirectory,
            final String mediaDirectory) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null
            || !isSafeDirectoryName(applicationDirectory)
            || !isSupportedMediaDirectory(mediaDirectory)) {
            return "";
        }

        final List<String> sourceDirectories = new ArrayList<>();
        appendExistingSourceDirectories(
            activity.getExternalFilesDirs(null),
            applicationDirectory,
            mediaDirectory,
            true,
            sourceDirectories);
        appendExistingSourceDirectories(
            activity.getExternalMediaDirs(),
            applicationDirectory,
            mediaDirectory,
            false,
            sourceDirectories);

        final StringBuilder result = new StringBuilder();
        for (final String sourceDirectory : sourceDirectories) {
            if (result.length() > 0) {
                result.append('\n');
            }
            result.append(sourceDirectory);
        }
        return result.toString();
    }

    /**
     * Queues an idempotent publication. A successful Android 10+ publication
     * is visible only after IS_PENDING is cleared. Any failure leaves the
     * staging source intact so application startup can retry it.
     */
    public static boolean publishFile(final String sourceFilePath,
                                      final String mimeType,
                                      final String applicationDirectory,
                                      final String mediaDirectory) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null
            || sourceFilePath == null
            || sourceFilePath.isEmpty()
            || mimeType == null
            || mimeType.isEmpty()
            || !isSafeDirectoryName(applicationDirectory)
            || !isSupportedMediaDirectory(mediaDirectory)) {
            return false;
        }

        final File sourceFile = new File(sourceFilePath).getAbsoluteFile();
        if (!sourceFile.isFile() || sourceFile.length() <= 0L) {
            Log.e(TAG, "Cannot publish missing or empty media: " + sourceFile);
            return false;
        }

        final String sourceKey = sourceFile.getAbsolutePath();
        synchronized (PUBLICATION_LOCK) {
            if (!QUEUED_SOURCE_PATHS.add(sourceKey)) {
                Log.i(TAG, "Media publication is already queued: " + sourceKey);
                return true;
            }
        }

        final Context context = activity.getApplicationContext();
        try {
            PUBLICATION_EXECUTOR.execute(new Runnable() {
                @Override
                public void run() {
                    boolean published = false;
                    try {
                        recoverStalePendingPublicationsOnce(context);
                        published =
                            Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                                ? publishThroughMediaStore(
                                      context,
                                      sourceFile,
                                      mimeType,
                                      applicationDirectory,
                                      mediaDirectory)
                                : publishThroughLegacyPublicDirectory(
                                      context,
                                      sourceFile,
                                      mimeType,
                                      applicationDirectory,
                                      mediaDirectory);
                        if (!published) {
                            Log.e(TAG,
                                  "Public media publication failed; preserving staging source: "
                                      + sourceFile);
                        }
                    } catch (final RuntimeException exception) {
                        Log.e(TAG, "Unexpected public-media publication failure: "
                                   + sourceFile, exception);
                    } finally {
                        synchronized (PUBLICATION_LOCK) {
                            QUEUED_SOURCE_PATHS.remove(sourceKey);
                            if (published) {
                                FAILED_SOURCE_PATHS.remove(sourceKey);
                            } else {
                                FAILED_SOURCE_PATHS.add(sourceKey);
                            }
                        }
                    }
                }
            });
            Log.i(TAG, "Queued durable public-media publication: " + sourceFile);
            return true;
        } catch (final RuntimeException exception) {
            synchronized (PUBLICATION_LOCK) {
                QUEUED_SOURCE_PATHS.remove(sourceKey);
                FAILED_SOURCE_PATHS.add(sourceKey);
            }
            Log.e(TAG, "Failed to queue public-media publication: " + sourceFile,
                  exception);
            return false;
        }
    }

    private static void queuePendingRecovery(final Context context) {
        synchronized (PUBLICATION_LOCK) {
            if (stalePendingRecovered || pendingRecoveryQueued) {
                return;
            }
            pendingRecoveryQueued = true;
        }
        try {
            PUBLICATION_EXECUTOR.execute(new Runnable() {
                @Override
                public void run() {
                    try {
                        recoverStalePendingPublicationsOnce(context);
                    } finally {
                        synchronized (PUBLICATION_LOCK) {
                            pendingRecoveryQueued = false;
                        }
                    }
                }
            });
        } catch (final RuntimeException exception) {
            synchronized (PUBLICATION_LOCK) {
                pendingRecoveryQueued = false;
            }
            Log.w(TAG, "Failed to queue pending-media recovery", exception);
        }
    }

    /**
     * Applies the configured video limit only to public videos recorded by the
     * current installation. The URI registry is intentionally app-private: an
     * uninstall drops management ownership but leaves the public user media.
     */
    public static boolean cleanupPublishedVideos(
            final long maximumBytes,
            final String applicationDirectory) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null
            || maximumBytes < 0L
            || !isSafeDirectoryName(applicationDirectory)) {
            return false;
        }
        final Context context = activity.getApplicationContext();
        try {
            PUBLICATION_EXECUTOR.execute(new Runnable() {
                @Override
                public void run() {
                    recoverStalePendingPublicationsOnce(context);
                    cleanupRegisteredVideos(
                        context,
                        maximumBytes,
                        applicationDirectory);
                }
            });
            return true;
        } catch (final RuntimeException exception) {
            Log.e(TAG, "Failed to queue public-video cleanup", exception);
            return false;
        }
    }

    /** Waits for work already queued by this process to reach public storage. */
    public static boolean waitForPendingPublications(final long timeoutMillis) {
        if (timeoutMillis < 0L) {
            return false;
        }
        final Future<?> barrier;
        try {
            barrier = PUBLICATION_EXECUTOR.submit(new Runnable() {
                @Override
                public void run() {
                    // Reaching this barrier means every earlier single-thread
                    // publication and cleanup task has completed.
                }
            });
        } catch (final RuntimeException exception) {
            Log.e(TAG, "Failed to queue the public-media shutdown barrier",
                  exception);
            return false;
        }

        try {
            barrier.get(timeoutMillis, TimeUnit.MILLISECONDS);
            synchronized (PUBLICATION_LOCK) {
                return QUEUED_SOURCE_PATHS.isEmpty()
                    && FAILED_SOURCE_PATHS.isEmpty();
            }
        } catch (final InterruptedException exception) {
            Thread.currentThread().interrupt();
            Log.w(TAG, "Interrupted while waiting for public-media publication",
                  exception);
        } catch (final ExecutionException | TimeoutException exception) {
            Log.w(TAG, "Public-media publication did not finish before shutdown",
                  exception);
        }
        return false;
    }

    /** Removes an app-specific staging/legacy file and any stale old index. */
    public static boolean deleteFile(final String filePath) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null || filePath == null || filePath.isEmpty()) {
            return false;
        }
        final File sourceFile = new File(filePath).getAbsoluteFile();
        synchronized (PUBLICATION_LOCK) {
            if (QUEUED_SOURCE_PATHS.contains(sourceFile.getAbsolutePath())) {
                Log.i(TAG, "Preserving media while public publication is queued: "
                           + sourceFile);
                return false;
            }
        }
        return deleteSourceAndLegacyIndex(
            activity.getApplicationContext(),
            sourceFile);
    }

    private static boolean publishThroughMediaStore(
            final Context context,
            final File sourceFile,
            final String mimeType,
            final String applicationDirectory,
            final String mediaDirectory) {
        final PublicationTarget target = resolvePublicationTarget(
            context,
            sourceFile,
            applicationDirectory,
            mediaDirectory);
        if (target == null) {
            return false;
        }

        final ContentResolver resolver = context.getContentResolver();
        final Uri sourceCleanupUri = findSourceCleanupPublication(
            context,
            resolver,
            target,
            sourceFile);
        if (sourceCleanupUri != null) {
            final boolean sourceRemoved =
                isFileInPublicTargetDirectory(sourceFile, target.relativePath)
                || deleteSourceAndLegacyIndex(context, sourceFile);
            if (sourceRemoved) {
                completeSourceCleanup(context, sourceCleanupUri);
            }
            Log.i(TAG, "Recovered committed public media independent of its "
                       + "provider display name: " + sourceFile
                       + " -> " + sourceCleanupUri);
            return true;
        }

        final List<ExistingMedia> existingItems = findExistingMedia(
            resolver,
            target,
            sourceFile.getName());
        for (final ExistingMedia existing : existingItems) {
            if (existing.pending) {
                if (!isKnownCurrentPublication(context, existing.uri)) {
                    continue;
                }
                try {
                    if (resolver.delete(existing.uri, null, null) > 0) {
                        removePendingUri(context, existing.uri);
                    } else {
                        Log.w(TAG, "MediaStore retained stale pending media: "
                                   + existing.uri);
                    }
                } catch (final RuntimeException exception) {
                    Log.w(TAG, "Failed to remove stale pending media: "
                               + existing.uri, exception);
                }
            } else if (isKnownCurrentPublication(context, existing.uri)
                       && existing.size == sourceFile.length()
                       && mediaStoreItemMatchesSource(
                           resolver, existing.uri, sourceFile)) {
                final boolean sourceRemoved =
                    isFileInPublicTargetDirectory(
                        sourceFile, target.relativePath)
                    || deleteSourceAndLegacyIndex(context, sourceFile);
                if (sourceRemoved) {
                    completeSourceCleanup(context, existing.uri);
                }
                Log.i(TAG, "Durable public media already exists: "
                           + sourceFile + " -> " + existing.uri);
                return true;
            } else if (!existing.pending
                       && isFileInPublicTargetDirectory(
                           sourceFile, target.relativePath)
                       && existing.size == sourceFile.length()
                       && mediaStoreItemMatchesSource(
                           resolver, existing.uri, sourceFile)) {
                // The source itself is already durable user media. Keep it
                // outside this installation's automatic cleanup registry.
                Log.i(TAG, "Public source media needs no republishing: "
                           + sourceFile + " -> " + existing.uri);
                return true;
            }
        }

        final ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, sourceFile.getName());
        values.put(MediaStore.MediaColumns.MIME_TYPE, mimeType);
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, target.relativePath);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        values.put(MediaStore.MediaColumns.DATE_MODIFIED,
                   sourceFile.lastModified() / 1000L);
        if (target.photo) {
            values.put(MediaStore.Images.ImageColumns.DATE_TAKEN,
                       sourceFile.lastModified());
        } else {
            values.put(MediaStore.Video.VideoColumns.DATE_TAKEN,
                       sourceFile.lastModified());
        }

        Uri mediaUri = null;
        boolean publicMediaCommitted = false;
        try {
            mediaUri = resolver.insert(target.collectionUri, values);
            if (mediaUri == null) {
                Log.e(TAG, "MediaStore insert returned no URI for " + sourceFile);
                return false;
            }
            if (!addPendingUri(context, mediaUri)) {
                resolver.delete(mediaUri, null, null);
                Log.e(TAG, "Failed to journal pending media URI: " + mediaUri);
                return false;
            }

            final long copiedBytes = copyIntoMediaStore(
                resolver,
                sourceFile,
                mediaUri);
            if (copiedBytes != sourceFile.length()) {
                throw new IOException(
                    "Published byte count mismatch: "
                        + copiedBytes + " != " + sourceFile.length());
            }

            values.clear();
            values.put(MediaStore.MediaColumns.IS_PENDING, 0);
            if (resolver.update(mediaUri, values, null, null) <= 0) {
                throw new IOException("Failed to clear MediaStore pending state");
            }
            publicMediaCommitted = true;

            if (!completePublicationJournal(context, mediaUri, !target.photo)) {
                // The public file is already valid. Retain the staging source
                // so a later launch can rediscover the row and repair state.
                Log.e(TAG, "Failed to commit public-media journal: " + mediaUri);
                return false;
            }

            final boolean sourceRemoved =
                isFileInPublicTargetDirectory(sourceFile, target.relativePath)
                || deleteSourceAndLegacyIndex(context, sourceFile);
            if (!sourceRemoved) {
                Log.w(TAG, "Public media is durable but staging cleanup failed: "
                           + sourceFile);
            } else {
                completeSourceCleanup(context, mediaUri);
            }
            Log.i(TAG, "Published durable public media: "
                       + sourceFile + " -> " + mediaUri
                       + " relativePath=" + target.relativePath);
            return true;
        } catch (final IOException | RuntimeException exception) {
            Log.e(TAG, "Failed to publish media through MediaStore: "
                       + sourceFile, exception);
            if (mediaUri != null && !publicMediaCommitted) {
                boolean incompleteRowRemoved = false;
                try {
                    incompleteRowRemoved =
                        resolver.delete(mediaUri, null, null) > 0;
                    if (!incompleteRowRemoved) {
                        final PendingMediaState state =
                            queryPendingMediaState(resolver, mediaUri);
                        incompleteRowRemoved =
                            state != null && !state.exists;
                    }
                } catch (final RuntimeException deleteException) {
                    Log.w(TAG, "Failed to remove incomplete MediaStore row: "
                               + mediaUri, deleteException);
                }
                if (incompleteRowRemoved) {
                    removePendingUri(context, mediaUri);
                } else {
                    Log.w(TAG, "Retaining pending-media recovery journal for "
                               + mediaUri);
                }
            } else if (mediaUri != null) {
                Log.w(TAG, "Public media was already committed; preserving it and "
                           + "the staging source for recovery: " + mediaUri);
            }
            return false;
        }
    }

    private static long copyIntoMediaStore(final ContentResolver resolver,
                                           final File sourceFile,
                                           final Uri mediaUri)
            throws IOException {
        final ParcelFileDescriptor descriptor =
            resolver.openFileDescriptor(mediaUri, "w");
        if (descriptor == null) {
            throw new IOException("MediaStore returned no writable descriptor");
        }

        long copiedBytes = 0L;
        try (FileInputStream input = new FileInputStream(sourceFile);
             FileOutputStream output =
                 new ParcelFileDescriptor.AutoCloseOutputStream(descriptor)) {
            final byte[] buffer = new byte[COPY_BUFFER_SIZE];
            int bytesRead;
            while ((bytesRead = input.read(buffer)) != -1) {
                output.write(buffer, 0, bytesRead);
                copiedBytes += bytesRead;
            }
            output.flush();
            output.getFD().sync();
        }
        return copiedBytes;
    }

    private static boolean mediaStoreItemMatchesSource(
            final ContentResolver resolver,
            final Uri mediaUri,
            final File sourceFile) {
        try (InputStream publishedInput = resolver.openInputStream(mediaUri);
             FileInputStream sourceInput = new FileInputStream(sourceFile)) {
            return publishedInput != null
                && streamsHaveSameContent(sourceInput, publishedInput);
        } catch (final IOException | RuntimeException exception) {
            Log.w(TAG, "Failed to verify an existing public-media item: "
                       + mediaUri, exception);
            return false;
        }
    }

    private static boolean publishThroughLegacyPublicDirectory(
            final Context context,
            final File sourceFile,
            final String mimeType,
            final String applicationDirectory,
            final String mediaDirectory) {
        final String publicRoot = PHOTO_DIRECTORY.equals(mediaDirectory)
            ? Environment.DIRECTORY_PICTURES
            : Environment.DIRECTORY_MOVIES;
        final File publicDirectory = new File(
            Environment.getExternalStoragePublicDirectory(publicRoot),
            applicationDirectory);
        if (!publicDirectory.isDirectory() && !publicDirectory.mkdirs()) {
            Log.e(TAG, "Failed to create legacy public media directory: "
                       + publicDirectory);
            return false;
        }

        final File directDestination = new File(
            publicDirectory,
            sourceFile.getName());
        if (directDestination.isFile()
            && !sameAbsoluteFile(sourceFile, directDestination)
            && filesHaveSameContent(sourceFile, directDestination)) {
            final Uri existingUri = scanFileAndWait(
                context,
                directDestination,
                mimeType);
            if (existingUri != null
                && isKnownCurrentPublication(context, existingUri)) {
                final boolean video = VIDEO_DIRECTORY.equals(mediaDirectory);
                final boolean sourceRemoved =
                    deleteSourceAndLegacyIndex(context, sourceFile);
                if (sourceRemoved) {
                    completeSourceCleanup(context, existingUri);
                }
                Log.i(TAG, "Durable legacy public media already exists: "
                           + directDestination + " -> " + existingUri);
                return true;
            }
        }

        final File destinationFile = uniqueDestinationFile(
            publicDirectory,
            sourceFile);
        if (destinationFile == null) {
            return false;
        }
        if (destinationFile.isFile()) {
            final Uri mediaUri = scanFileAndWait(
                context,
                destinationFile,
                mimeType);
            if (mediaUri == null) {
                return false;
            }
            // The source is already inside the durable public directory.
            // Do not claim historical media for this installation's cleanup.
            Log.i(TAG, "Durable legacy public media already exists: "
                       + destinationFile + " -> " + mediaUri);
            return true;
        }

        final File partialFile = new File(
            publicDirectory,
            "." + destinationFile.getName() + ".publication.partial");
        if (partialFile.exists() && !partialFile.delete()) {
            Log.e(TAG, "Failed to clear stale public-media partial: "
                       + partialFile);
            return false;
        }

        final long sourceLength = sourceFile.length();
        try (FileInputStream input = new FileInputStream(sourceFile);
             FileOutputStream output = new FileOutputStream(partialFile)) {
            final byte[] buffer = new byte[COPY_BUFFER_SIZE];
            long copiedBytes = 0L;
            int bytesRead;
            while ((bytesRead = input.read(buffer)) != -1) {
                output.write(buffer, 0, bytesRead);
                copiedBytes += bytesRead;
            }
            output.flush();
            output.getFD().sync();
            if (copiedBytes != sourceLength) {
                throw new IOException("Legacy public-media byte count mismatch");
            }
        } catch (final IOException exception) {
            Log.e(TAG, "Failed to copy legacy public media: " + sourceFile,
                  exception);
            if (!partialFile.delete() && partialFile.exists()) {
                Log.w(TAG, "Failed to remove public-media partial: "
                           + partialFile);
            }
            return false;
        }

        if (!partialFile.renameTo(destinationFile)) {
            Log.e(TAG, "Failed to finalize legacy public media: "
                       + destinationFile);
            if (!partialFile.delete()) {
                Log.w(TAG, "Failed to remove uncommitted public media: "
                           + partialFile);
            }
            return false;
        }
        destinationFile.setLastModified(sourceFile.lastModified());
        final Uri mediaUri = scanFileAndWait(
            context,
            destinationFile,
            mimeType);
        if (mediaUri == null) {
            return false;
        }
        final boolean video = VIDEO_DIRECTORY.equals(mediaDirectory);
        if (!recordPublishedMediaUri(context, mediaUri, video)) {
            Log.e(TAG, "Failed to journal durable legacy public media: "
                       + mediaUri);
            return false;
        }
        final boolean sourceRemoved =
            deleteSourceAndLegacyIndex(context, sourceFile);
        if (sourceRemoved) {
            completeSourceCleanup(context, mediaUri);
        }
        Log.i(TAG, "Published durable legacy public media: "
                   + destinationFile + " -> " + mediaUri);
        return true;
    }

    private static PublicationTarget resolvePublicationTarget(
            final Context context,
            final File sourceFile,
            final String applicationDirectory,
            final String mediaDirectory) {
        final String volumeName = resolveMediaStoreVolumeName(context, sourceFile);
        if (volumeName == null || volumeName.isEmpty()) {
            return null;
        }
        final boolean photo = PHOTO_DIRECTORY.equals(mediaDirectory);
        final String relativePath =
            (photo ? Environment.DIRECTORY_PICTURES
                   : Environment.DIRECTORY_MOVIES)
                + "/" + applicationDirectory + "/";
        final Uri collectionUri = photo
            ? MediaStore.Images.Media.getContentUri(volumeName)
            : MediaStore.Video.Media.getContentUri(volumeName);
        return new PublicationTarget(photo, relativePath, collectionUri);
    }

    private static String resolveMediaStoreVolumeName(final Context context,
                                                      final File sourceFile) {
        final Set<String> availableVolumes =
            MediaStore.getExternalVolumeNames(context);
        final StorageManager storageManager =
            (StorageManager) context.getSystemService(Context.STORAGE_SERVICE);
        StorageVolume sourceVolume = null;
        if (storageManager != null) {
            try {
                sourceVolume = storageManager.getStorageVolume(sourceFile);
            } catch (final RuntimeException exception) {
                Log.w(TAG, "Failed to resolve source storage volume: "
                           + sourceFile, exception);
            }
        }

        if (sourceVolume != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final String resolvedName = sourceVolume.getMediaStoreVolumeName();
            if (resolvedName != null && availableVolumes.contains(resolvedName)) {
                return resolvedName;
            }
        }
        if (sourceVolume != null && sourceVolume.isPrimary()) {
            return MediaStore.VOLUME_EXTERNAL_PRIMARY;
        }
        if (sourceVolume != null && sourceVolume.getUuid() != null) {
            for (final String availableVolume : availableVolumes) {
                if (sourceVolume.getUuid().equalsIgnoreCase(availableVolume)) {
                    return availableVolume;
                }
            }
        }

        if (availableVolumes.contains(MediaStore.VOLUME_EXTERNAL_PRIMARY)) {
            Log.w(TAG, "Falling back to the primary public-media volume for "
                       + sourceFile);
            return MediaStore.VOLUME_EXTERNAL_PRIMARY;
        }
        Log.e(TAG, "No writable MediaStore volume is available");
        return null;
    }

    private static List<ExistingMedia> findExistingMedia(
            final ContentResolver resolver,
            final PublicationTarget target,
            final String displayName) {
        final List<ExistingMedia> result = new ArrayList<>();
        final String[] projection = {
            MediaStore.MediaColumns._ID,
            MediaStore.MediaColumns.SIZE,
            MediaStore.MediaColumns.IS_PENDING,
        };
        final String selection =
            MediaStore.MediaColumns.DISPLAY_NAME + " = ? AND "
                + MediaStore.MediaColumns.RELATIVE_PATH + " = ?";
        try (Cursor cursor = resolver.query(
                 target.collectionUri,
                 projection,
                 selection,
                 new String[] {displayName, target.relativePath},
                 null)) {
            if (cursor == null) {
                return result;
            }
            while (cursor.moveToNext()) {
                result.add(new ExistingMedia(
                    ContentUris.withAppendedId(
                        target.collectionUri,
                        cursor.getLong(0)),
                    cursor.getLong(1),
                    cursor.getInt(2) != 0));
            }
        } catch (final RuntimeException exception) {
            Log.w(TAG, "Failed to query an existing public-media destination",
                  exception);
        }
        return result;
    }

    private static Uri findSourceCleanupPublication(
            final Context context,
            final ContentResolver resolver,
            final PublicationTarget target,
            final File sourceFile) {
        final Set<String> sourceCleanupUris;
        synchronized (PUBLICATION_LOCK) {
            final SharedPreferences preferences = publicationPreferences(context);
            sourceCleanupUris = new HashSet<>(
                preferences.getStringSet(
                    SOURCE_CLEANUP_URI_SET,
                    Collections.<String>emptySet()));
        }

        final String[] projection = {
            MediaStore.MediaColumns.SIZE,
            MediaStore.MediaColumns.IS_PENDING,
            MediaStore.MediaColumns.MIME_TYPE,
            MediaStore.MediaColumns.RELATIVE_PATH,
        };
        for (final String uriString : sourceCleanupUris) {
            final Uri uri;
            try {
                uri = Uri.parse(uriString);
            } catch (final RuntimeException exception) {
                continue;
            }
            try (Cursor cursor = resolver.query(
                     uri, projection, null, null, null)) {
                if (cursor == null
                    || !cursor.moveToFirst()
                    || cursor.getLong(0) != sourceFile.length()
                    || cursor.getInt(1) != 0
                    || !sameRelativePath(
                        cursor.getString(3), target.relativePath)) {
                    continue;
                }
                final String storedMimeType = cursor.getString(2);
                if (storedMimeType == null
                    || (target.photo
                            ? !storedMimeType.startsWith("image/")
                            : !storedMimeType.startsWith("video/"))) {
                    continue;
                }
                if (mediaStoreItemMatchesSource(resolver, uri, sourceFile)) {
                    return uri;
                }
            } catch (final RuntimeException exception) {
                Log.w(TAG, "Failed to inspect source-cleanup media URI: "
                           + uri, exception);
            }
        }
        return null;
    }

    private static File selectAppSpecificDirectory(
            final Context context,
            final String preferredStoragePath,
            final File[] candidates) {
        final StorageManager storageManager =
            (StorageManager) context.getSystemService(Context.STORAGE_SERVICE);
        if (storageManager == null || candidates == null) {
            return null;
        }

        StorageVolume preferredVolume = null;
        if (preferredStoragePath != null && !preferredStoragePath.isEmpty()) {
            try {
                preferredVolume = storageManager.getStorageVolume(
                    new File(preferredStoragePath));
            } catch (final RuntimeException exception) {
                Log.w(TAG, "Failed to resolve preferred storage volume for "
                           + preferredStoragePath, exception);
            }
        }

        File firstAvailable = null;
        for (final File candidate : candidates) {
            if (candidate == null
                || !Environment.MEDIA_MOUNTED.equals(
                       Environment.getExternalStorageState(candidate))) {
                continue;
            }
            if (firstAvailable == null) {
                firstAvailable = candidate;
            }
            try {
                if (sameStorageVolume(
                        preferredVolume,
                        storageManager.getStorageVolume(candidate))) {
                    return candidate;
                }
            } catch (final RuntimeException exception) {
                Log.w(TAG, "Failed to inspect app-specific storage directory: "
                           + candidate, exception);
            }
        }
        return firstAvailable;
    }

    private static void appendExistingSourceDirectories(
            final File[] roots,
            final String applicationDirectory,
            final String mediaDirectory,
            final boolean staging,
            final List<String> result) {
        if (roots == null) {
            return;
        }
        for (final File root : roots) {
            if (root == null
                || !Environment.MEDIA_MOUNTED.equals(
                       Environment.getExternalStorageState(root))) {
                continue;
            }
            final File applicationRoot = new File(root, applicationDirectory);
            final File sourceDirectory = staging
                ? new File(new File(applicationRoot, "Staging"), mediaDirectory)
                : new File(applicationRoot, mediaDirectory);
            final String absolutePath = sourceDirectory.getAbsolutePath();
            if (sourceDirectory.isDirectory()
                && !result.contains(absolutePath)) {
                result.add(absolutePath);
            }
        }
    }

    private static void cleanupRegisteredVideos(
            final Context context,
            final long maximumBytes,
            final String applicationDirectory) {
        final SharedPreferences preferences = publicationPreferences(context);
        final Set<String> registeredUris;
        synchronized (PUBLICATION_LOCK) {
            registeredUris = new HashSet<>(
                preferences.getStringSet(
                    VIDEO_URI_SET,
                    Collections.<String>emptySet()));
        }
        if (registeredUris.isEmpty()) {
            return;
        }

        final ContentResolver resolver = context.getContentResolver();
        final List<PublishedVideo> videos = new ArrayList<>();
        final Set<String> retainedUris = new HashSet<>();
        long totalBytes = 0L;
        for (final String uriString : registeredUris) {
            final Uri uri;
            try {
                uri = Uri.parse(uriString);
            } catch (final RuntimeException exception) {
                continue;
            }
            final PublishedVideo video = queryPublishedVideo(
                resolver,
                uri,
                applicationDirectory);
            if (video == null) {
                continue;
            }
            if (video.size < 0L) {
                retainedUris.add(uriString);
                continue;
            }
            videos.add(video);
            retainedUris.add(uriString);
            totalBytes = saturatedAdd(totalBytes, video.size);
        }

        Collections.sort(videos, new Comparator<PublishedVideo>() {
            @Override
            public int compare(final PublishedVideo first,
                               final PublishedVideo second) {
                final int dateComparison = Long.compare(
                    first.dateAdded,
                    second.dateAdded);
                return dateComparison != 0
                    ? dateComparison
                    : first.uri.toString().compareTo(second.uri.toString());
            }
        });

        for (final PublishedVideo video : videos) {
            if (totalBytes < maximumBytes) {
                break;
            }
            try {
                final int deleted = resolver.delete(video.uri, null, null);
                if (deleted <= 0) {
                    Log.w(TAG, "Public-video cleanup did not delete " + video.uri);
                    break;
                }
                totalBytes = Math.max(0L, totalBytes - video.size);
                retainedUris.remove(video.uri.toString());
                Log.i(TAG, "Deleted managed public video: " + video.uri);
            } catch (final RuntimeException exception) {
                Log.w(TAG, "Failed to delete managed public video: "
                           + video.uri, exception);
                break;
            }
        }

        synchronized (PUBLICATION_LOCK) {
            final Set<String> updatedUris = new HashSet<>(
                preferences.getStringSet(
                    VIDEO_URI_SET,
                    Collections.<String>emptySet()));
            for (final String registeredUri : registeredUris) {
                if (!retainedUris.contains(registeredUri)) {
                    updatedUris.remove(registeredUri);
                }
            }
            if (!preferences.edit()
                    .putStringSet(VIDEO_URI_SET, updatedUris)
                    .commit()) {
                Log.w(TAG, "Failed to persist the public-video cleanup registry");
            }
        }
    }

    private static PublishedVideo queryPublishedVideo(
            final ContentResolver resolver,
            final Uri uri,
            final String applicationDirectory) {
        final String[] projection = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
            ? new String[] {
                MediaStore.MediaColumns.DISPLAY_NAME,
                MediaStore.MediaColumns.SIZE,
                MediaStore.MediaColumns.DATE_ADDED,
                MediaStore.MediaColumns.RELATIVE_PATH,
            }
            : new String[] {
                MediaStore.MediaColumns.DISPLAY_NAME,
                MediaStore.MediaColumns.SIZE,
                MediaStore.MediaColumns.DATE_ADDED,
                MediaStore.MediaColumns.DATA,
            };
        try (Cursor cursor = resolver.query(uri, projection, null, null, null)) {
            if (cursor == null) {
                return new PublishedVideo(uri, -1L, -1L);
            }
            if (!cursor.moveToFirst()) {
                return null;
            }
            final String name = cursor.getString(0);
            if (name == null || !CUSTOM_VIDEO_PATTERN.matcher(name).matches()) {
                return null;
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                final String expectedRelativePath =
                    Environment.DIRECTORY_MOVIES
                        + "/" + applicationDirectory + "/";
                if (!sameRelativePath(cursor.getString(3), expectedRelativePath)) {
                    return null;
                }
            } else {
                final String absolutePath = cursor.getString(3);
                final File expectedDirectory = new File(
                    Environment.getExternalStoragePublicDirectory(
                        Environment.DIRECTORY_MOVIES),
                    applicationDirectory);
                if (absolutePath == null
                    || !sameAbsoluteFile(
                        new File(absolutePath).getParentFile(),
                        expectedDirectory)) {
                    return null;
                }
            }
            return new PublishedVideo(uri, cursor.getLong(1), cursor.getLong(2));
        } catch (final RuntimeException exception) {
            Log.w(TAG, "Failed to inspect managed public video: " + uri,
                  exception);
            // Preserve an inaccessible registry entry so a transient provider
            // failure does not permanently exempt the media from the limit.
            return new PublishedVideo(uri, -1L, -1L);
        }
    }

    private static Uri scanFileAndWait(final Context context,
                                       final File mediaFile,
                                       final String mimeType) {
        if (!mediaFile.isFile() || mediaFile.length() <= 0L) {
            return null;
        }
        final CountDownLatch completed = new CountDownLatch(1);
        final Uri[] scannedUri = new Uri[1];
        try {
            MediaScannerConnection.scanFile(
                context,
                new String[] {mediaFile.getAbsolutePath()},
                new String[] {mimeType},
                new MediaScannerConnection.OnScanCompletedListener() {
                    @Override
                    public void onScanCompleted(final String scannedPath,
                                                final Uri uri) {
                        scannedUri[0] = uri;
                        completed.countDown();
                    }
                });
        } catch (final RuntimeException exception) {
            Log.e(TAG, "Failed to scan public media: " + mediaFile, exception);
            return null;
        }

        try {
            if (!completed.await(MEDIA_SCAN_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                Log.e(TAG, "Timed out waiting for public-media scan: "
                           + mediaFile);
                return null;
            }
        } catch (final InterruptedException exception) {
            Thread.currentThread().interrupt();
            Log.e(TAG, "Interrupted while waiting for public-media scan: "
                       + mediaFile, exception);
            return null;
        }
        if (scannedUri[0] == null) {
            Log.e(TAG, "Public media scan returned no URI: " + mediaFile);
        }
        return scannedUri[0];
    }

    private static File uniqueDestinationFile(final File directory,
                                              final File sourceFile) {
        final String fileName = sourceFile.getName();
        final File direct = new File(directory, fileName);
        if (!direct.exists() || sameAbsoluteFile(sourceFile, direct)) {
            return direct;
        }
        final int dot = fileName.lastIndexOf('.');
        final String stem = dot > 0 ? fileName.substring(0, dot) : fileName;
        final String extension = dot > 0 ? fileName.substring(dot) : "";
        final Matcher localMediaStemMatcher =
            LOCAL_MEDIA_STEM_PATTERN.matcher(stem);
        final boolean hasLocalMediaSuffix = localMediaStemMatcher.matches();
        for (int suffix = 1; suffix <= 10000; ++suffix) {
            final String uniqueStem = hasLocalMediaSuffix
                ? localMediaStemMatcher.group(1)
                    + "_copy_" + suffix
                    + localMediaStemMatcher.group(2)
                : stem + "_" + suffix;
            final File candidate = new File(
                directory,
                uniqueStem + extension);
            if (!candidate.exists()) {
                return candidate;
            }
        }
        Log.e(TAG, "Unable to allocate a unique public-media filename for "
                   + fileName);
        return null;
    }

    private static boolean filesHaveSameContent(final File first,
                                                final File second) {
        if (sameAbsoluteFile(first, second)) {
            return true;
        }
        if (!first.isFile()
            || !second.isFile()
            || first.length() != second.length()) {
            return false;
        }
        final byte[] firstBuffer = new byte[COPY_BUFFER_SIZE];
        final byte[] secondBuffer = new byte[COPY_BUFFER_SIZE];
        try (FileInputStream firstInput = new FileInputStream(first);
             FileInputStream secondInput = new FileInputStream(second)) {
            return streamsHaveSameContent(
                firstInput,
                secondInput,
                firstBuffer,
                secondBuffer);
        } catch (final IOException exception) {
            Log.w(TAG, "Failed to compare a public-media destination: "
                       + second, exception);
            return false;
        }
    }

    private static boolean streamsHaveSameContent(final InputStream first,
                                                  final InputStream second)
            throws IOException {
        return streamsHaveSameContent(
            first,
            second,
            new byte[COPY_BUFFER_SIZE],
            new byte[COPY_BUFFER_SIZE]);
    }

    private static boolean streamsHaveSameContent(
            final InputStream first,
            final InputStream second,
            final byte[] firstBuffer,
            final byte[] secondBuffer) throws IOException {
        while (true) {
            final int firstRead = first.read(firstBuffer);
            final int secondRead = second.read(secondBuffer);
            if (firstRead != secondRead) {
                return false;
            }
            if (firstRead < 0) {
                return true;
            }
            for (int index = 0; index < firstRead; ++index) {
                if (firstBuffer[index] != secondBuffer[index]) {
                    return false;
                }
            }
        }
    }

    private static boolean sameAbsoluteFile(final File first,
                                            final File second) {
        return first != null
            && second != null
            && first.getAbsoluteFile().equals(second.getAbsoluteFile());
    }

    private static boolean sameRelativePath(final String first,
                                            final String second) {
        if (first == null || second == null) {
            return false;
        }
        return trimTrailingSlashes(first.replace('\\', '/')).equals(
            trimTrailingSlashes(second.replace('\\', '/')));
    }

    private static String trimTrailingSlashes(final String path) {
        int end = path.length();
        while (end > 0 && path.charAt(end - 1) == '/') {
            --end;
        }
        return path.substring(0, end);
    }

    private static boolean isFileInPublicTargetDirectory(
            final File sourceFile,
            final String relativePath) {
        final File parent = sourceFile.getAbsoluteFile().getParentFile();
        if (parent == null || relativePath == null || relativePath.isEmpty()) {
            return false;
        }
        String normalizedRelativePath =
            relativePath.replace('\\', '/');
        while (normalizedRelativePath.endsWith("/")) {
            normalizedRelativePath = normalizedRelativePath.substring(
                0,
                normalizedRelativePath.length() - 1);
        }
        return parent.getAbsolutePath()
            .replace('\\', '/')
            .endsWith("/" + normalizedRelativePath);
    }

    private static boolean deleteSourceAndLegacyIndex(final Context context,
                                                      final File sourceFile) {
        final Uri indexedUri = findMediaUri(context, sourceFile.getAbsolutePath());
        if (indexedUri != null) {
            try {
                if (context.getContentResolver().delete(indexedUri, null, null) > 0) {
                    return true;
                }
            } catch (final RuntimeException exception) {
                Log.w(TAG, "Failed to delete the legacy media index for "
                           + sourceFile, exception);
            }
        }
        final boolean deleted = !sourceFile.exists() || sourceFile.delete();
        if (!deleted) {
            Log.w(TAG, "Failed to delete media staging source: " + sourceFile);
        }
        return deleted;
    }

    private static Uri findMediaUri(final Context context,
                                    final String absolutePath) {
        final ContentResolver resolver = context.getContentResolver();
        final Uri filesUri = MediaStore.Files.getContentUri(
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                ? MediaStore.VOLUME_EXTERNAL
                : "external");
        final String[] projection =
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                ? new String[] {
                    MediaStore.Files.FileColumns._ID,
                    MediaStore.MediaColumns.VOLUME_NAME,
                }
                : new String[] {MediaStore.Files.FileColumns._ID};
        final String selection = MediaStore.MediaColumns.DATA + " = ?";
        try (Cursor cursor = resolver.query(
                 filesUri,
                 projection,
                 selection,
                 new String[] {absolutePath},
                 null)) {
            if (cursor != null && cursor.moveToFirst()) {
                final Uri specificFilesUri;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    specificFilesUri = MediaStore.Files.getContentUri(
                        cursor.getString(1));
                } else {
                    specificFilesUri = filesUri;
                }
                return ContentUris.withAppendedId(
                    specificFilesUri,
                    cursor.getLong(0));
            }
        } catch (final RuntimeException exception) {
            Log.w(TAG, "Failed to query legacy MediaStore path: "
                       + absolutePath, exception);
        }
        return null;
    }

    private static void recoverStalePendingPublicationsOnce(
            final Context context) {
        synchronized (PUBLICATION_LOCK) {
            if (stalePendingRecovered) {
                return;
            }
            final SharedPreferences preferences = publicationPreferences(context);
            final Set<String> pendingUris = new HashSet<>(
                preferences.getStringSet(
                    PENDING_URI_SET,
                    Collections.<String>emptySet()));
            final Set<String> retained = new HashSet<>();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                for (final String uriString : pendingUris) {
                    try {
                        final Uri uri = Uri.parse(uriString);
                        final PendingMediaState state =
                            queryPendingMediaState(
                                context.getContentResolver(),
                                uri);
                        if (state == null) {
                            retained.add(uriString);
                        } else if (!state.exists) {
                            Log.i(TAG, "Discarded missing pending-media journal entry: "
                                       + uri);
                        } else if (!state.pending) {
                            if (!recordPublishedMediaUri(
                                    context, uri, state.video)) {
                                retained.add(uriString);
                                Log.w(TAG, "Failed to recover publication journal for "
                                           + uri);
                                continue;
                            }
                            Log.i(TAG, "Recovered an already-published public-media row: "
                                       + uri);
                        } else if (context.getContentResolver().delete(
                                       uri, null, null) > 0) {
                            Log.i(TAG, "Removed stale pending public media: "
                                       + uri);
                        } else {
                            retained.add(uriString);
                        }
                    } catch (final RuntimeException exception) {
                        retained.add(uriString);
                        Log.w(TAG, "Failed to recover pending public media: "
                                   + uriString, exception);
                    }
                }
            }
            if (!preferences.edit()
                    .putStringSet(PENDING_URI_SET, retained)
                    .commit()) {
                Log.w(TAG, "Failed to persist pending-publication recovery");
            }
            stalePendingRecovered = true;
        }
    }

    private static PendingMediaState queryPendingMediaState(
            final ContentResolver resolver,
            final Uri uri) {
        final String[] projection = {
            MediaStore.MediaColumns.IS_PENDING,
            MediaStore.MediaColumns.MIME_TYPE,
        };
        try (Cursor cursor = resolver.query(uri, projection, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst()) {
                return new PendingMediaState(false, false, false);
            }
            final String mimeType = cursor.getString(1);
            return new PendingMediaState(
                true,
                cursor.getInt(0) != 0,
                mimeType != null && mimeType.startsWith("video/"));
        } catch (final RuntimeException exception) {
            Log.w(TAG, "Failed to inspect pending public media: " + uri,
                  exception);
            return null;
        }
    }

    private static boolean addPendingUri(final Context context, final Uri uri) {
        synchronized (PUBLICATION_LOCK) {
            final SharedPreferences preferences = publicationPreferences(context);
            final Set<String> values = new HashSet<>(
                preferences.getStringSet(
                    PENDING_URI_SET,
                    Collections.<String>emptySet()));
            values.add(uri.toString());
            return preferences.edit()
                .putStringSet(PENDING_URI_SET, values)
                .commit();
        }
    }

    private static void removePendingUri(final Context context, final Uri uri) {
        synchronized (PUBLICATION_LOCK) {
            final SharedPreferences preferences = publicationPreferences(context);
            final Set<String> values = new HashSet<>(
                preferences.getStringSet(
                    PENDING_URI_SET,
                    Collections.<String>emptySet()));
            values.remove(uri.toString());
            if (!preferences.edit()
                    .putStringSet(PENDING_URI_SET, values)
                    .commit()) {
                Log.w(TAG, "Failed to clear pending public-media URI: " + uri);
            }
        }
    }

    private static boolean completePublicationJournal(final Context context,
                                                      final Uri uri,
                                                      final boolean video) {
        synchronized (PUBLICATION_LOCK) {
            final SharedPreferences preferences = publicationPreferences(context);
            final Set<String> pending = new HashSet<>(
                preferences.getStringSet(
                    PENDING_URI_SET,
                    Collections.<String>emptySet()));
            pending.remove(uri.toString());
            final SharedPreferences.Editor editor =
                preferences.edit().putStringSet(PENDING_URI_SET, pending);
            if (video) {
                final Set<String> videos = new HashSet<>(
                    preferences.getStringSet(
                        VIDEO_URI_SET,
                        Collections.<String>emptySet()));
                videos.add(uri.toString());
                editor.putStringSet(VIDEO_URI_SET, videos);
            }
            final Set<String> sourceCleanupUris = new HashSet<>(
                preferences.getStringSet(
                    SOURCE_CLEANUP_URI_SET,
                    Collections.<String>emptySet()));
            sourceCleanupUris.add(uri.toString());
            editor.putStringSet(
                SOURCE_CLEANUP_URI_SET,
                sourceCleanupUris);
            return editor.commit();
        }
    }

    private static boolean recordPublishedMediaUri(final Context context,
                                                   final Uri uri,
                                                   final boolean video) {
        if (uri == null) {
            return false;
        }
        synchronized (PUBLICATION_LOCK) {
            final SharedPreferences preferences = publicationPreferences(context);
            final Set<String> sourceCleanupUris = new HashSet<>(
                preferences.getStringSet(
                    SOURCE_CLEANUP_URI_SET,
                    Collections.<String>emptySet()));
            sourceCleanupUris.add(uri.toString());
            final SharedPreferences.Editor editor = preferences.edit()
                .putStringSet(SOURCE_CLEANUP_URI_SET, sourceCleanupUris);
            if (video) {
                final Set<String> videos = new HashSet<>(
                    preferences.getStringSet(
                        VIDEO_URI_SET,
                        Collections.<String>emptySet()));
                videos.add(uri.toString());
                editor.putStringSet(VIDEO_URI_SET, videos);
            }
            return editor.commit();
        }
    }

    private static void completeSourceCleanup(final Context context,
                                              final Uri uri) {
        if (uri == null) {
            return;
        }
        synchronized (PUBLICATION_LOCK) {
            final SharedPreferences preferences = publicationPreferences(context);
            final Set<String> values = new HashSet<>(
                preferences.getStringSet(
                    SOURCE_CLEANUP_URI_SET,
                    Collections.<String>emptySet()));
            values.remove(uri.toString());
            if (!preferences.edit()
                    .putStringSet(SOURCE_CLEANUP_URI_SET, values)
                    .commit()) {
                Log.w(TAG, "Failed to clear source-cleanup media URI: " + uri);
            }
        }
    }

    private static boolean isKnownCurrentPublication(final Context context,
                                                     final Uri uri) {
        if (uri == null) {
            return false;
        }
        synchronized (PUBLICATION_LOCK) {
            final SharedPreferences preferences = publicationPreferences(context);
            final String uriString = uri.toString();
            return preferences.getStringSet(
                       PENDING_URI_SET,
                       Collections.<String>emptySet()).contains(uriString)
                || preferences.getStringSet(
                       VIDEO_URI_SET,
                       Collections.<String>emptySet()).contains(uriString)
                || preferences.getStringSet(
                       SOURCE_CLEANUP_URI_SET,
                       Collections.<String>emptySet()).contains(uriString);
        }
    }

    private static SharedPreferences publicationPreferences(
            final Context context) {
        synchronized (PUBLICATION_LOCK) {
            final SharedPreferences preferences = context.getSharedPreferences(
                PUBLICATION_PREFERENCES,
                Context.MODE_PRIVATE);
            if (installationRegistryValidated) {
                return preferences;
            }

            final File installationMarker = new File(
                context.getNoBackupFilesDir(),
                INSTALLATION_MARKER_FILE);
            if (!installationMarker.isFile()) {
                if (!preferences.edit().clear().commit()) {
                    Log.e(TAG, "Cannot clear a potentially restored public-media registry");
                    throw new IllegalStateException(
                        "Public-media installation registry is not trustworthy");
                }
                try (FileOutputStream output =
                         new FileOutputStream(installationMarker, false)) {
                    output.write(1);
                    output.flush();
                    output.getFD().sync();
                } catch (final IOException exception) {
                    Log.e(TAG, "Failed to persist the no-backup installation marker",
                          exception);
                    throw new IllegalStateException(
                        "Cannot establish the public-media installation marker",
                        exception);
                }
            }
            installationRegistryValidated = true;
            return preferences;
        }
    }

    private static long saturatedAdd(final long first, final long second) {
        if (second > 0L && first > Long.MAX_VALUE - second) {
            return Long.MAX_VALUE;
        }
        return first + Math.max(0L, second);
    }

    private static boolean sameStorageVolume(final StorageVolume first,
                                             final StorageVolume second) {
        if (first == null || second == null) {
            return false;
        }
        if (first.isPrimary() || second.isPrimary()) {
            return first.isPrimary() == second.isPrimary();
        }
        final String firstUuid = first.getUuid();
        final String secondUuid = second.getUuid();
        return firstUuid != null && firstUuid.equalsIgnoreCase(secondUuid);
    }

    private static boolean isSupportedMediaDirectory(final String name) {
        return PHOTO_DIRECTORY.equals(name) || VIDEO_DIRECTORY.equals(name);
    }

    private static boolean isSafeDirectoryName(final String directoryName) {
        return directoryName != null
            && !directoryName.isEmpty()
            && !".".equals(directoryName)
            && !"..".equals(directoryName)
            && directoryName.indexOf('/') < 0
            && directoryName.indexOf('\\') < 0;
    }
}
