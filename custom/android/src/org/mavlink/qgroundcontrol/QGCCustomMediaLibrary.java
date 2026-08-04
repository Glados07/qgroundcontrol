package org.mavlink.qgroundcontrol;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;

import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.Context;
import android.database.Cursor;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.MediaStore;
import android.util.Log;

/** QGC_CUSTOM_ANDROID_MEDIA_LIBRARY_V1
 * Places custom camera files in Android shared-media directories and keeps
 * their MediaStore entries synchronized.
 */
public final class QGCCustomMediaLibrary {
    private static final String TAG = "QGCCustomMedia-Custom";
    private static final int COPY_BUFFER_SIZE = 1024 * 1024;
    private static final Map<String, Uri> INDEXED_URIS = new HashMap<>();
    private static final ExecutorService MIGRATION_EXECUTOR =
        Executors.newSingleThreadExecutor(new ThreadFactory() {
            @Override
            public Thread newThread(final Runnable runnable) {
                final Thread thread =
                    new Thread(runnable, "QGCCustomMediaMigration");
                thread.setDaemon(true);
                return thread;
            }
        });

    private interface ScanSuccessAction {
        void run();
    }

    private QGCCustomMediaLibrary() {
    }

    public static String getMediaDirectory(final String preferredStoragePath,
                                           final String applicationDirectory,
                                           final String mediaDirectory) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null) {
            Log.e(TAG, "Cannot resolve media directory: QGCActivity is unavailable");
            return "";
        }

        if (!isSafeDirectoryName(applicationDirectory)
            || !isSafeDirectoryName(mediaDirectory)) {
            Log.e(TAG, "Cannot resolve media directory: invalid directory name");
            return "";
        }

        final StorageManager storageManager =
            (StorageManager) activity.getSystemService(Context.STORAGE_SERVICE);
        if (storageManager == null) {
            Log.e(TAG, "Cannot resolve media directory: StorageManager is unavailable");
            return "";
        }

        StorageVolume preferredVolume = null;
        if (preferredStoragePath != null && !preferredStoragePath.isEmpty()) {
            try {
                preferredVolume = storageManager.getStorageVolume(
                    new File(preferredStoragePath));
            } catch (final RuntimeException exception) {
                Log.w(TAG,
                      "Failed to resolve preferred storage volume for "
                          + preferredStoragePath,
                      exception);
            }
        }

        File firstAvailableDirectory = null;
        File selectedDirectory = null;
        final File[] externalMediaDirectories = activity.getExternalMediaDirs();
        if (externalMediaDirectories != null) {
            for (final File candidate : externalMediaDirectories) {
                if (candidate == null
                    || !Environment.MEDIA_MOUNTED.equals(
                           Environment.getExternalStorageState(candidate))) {
                    continue;
                }

                if (firstAvailableDirectory == null) {
                    firstAvailableDirectory = candidate;
                }

                final StorageVolume candidateVolume;
                try {
                    candidateVolume = storageManager.getStorageVolume(candidate);
                } catch (final RuntimeException exception) {
                    Log.w(TAG,
                          "Failed to resolve storage volume for " + candidate,
                          exception);
                    continue;
                }

                if (sameStorageVolume(preferredVolume, candidateVolume)) {
                    selectedDirectory = candidate;
                    break;
                }
            }
        }

        if (selectedDirectory == null) {
            selectedDirectory = firstAvailableDirectory;
        }
        if (selectedDirectory == null) {
            Log.e(TAG, "No mounted Android shared-media directory is available");
            return "";
        }

        final File result = new File(
            new File(selectedDirectory, applicationDirectory),
            mediaDirectory);
        if (!result.isDirectory() && !result.mkdirs()) {
            Log.e(TAG, "Failed to create shared-media directory: " + result);
            return "";
        }

        Log.i(TAG, "Using shared-media directory: " + result.getAbsolutePath());
        return result.getAbsolutePath();
    }

    public static boolean scanFile(final String filePath,
                                   final String mimeType) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null) {
            Log.e(TAG, "Cannot scan media: QGCActivity is unavailable");
            return false;
        }

        return requestScan(
            activity.getApplicationContext(),
            new File(filePath == null ? "" : filePath),
            mimeType,
            null);
    }

    public static boolean migrateFile(final String sourceFilePath,
                                      final String destinationDirectoryPath,
                                      final String mimeType) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null
            || sourceFilePath == null
            || sourceFilePath.isEmpty()
            || destinationDirectoryPath == null
            || destinationDirectoryPath.isEmpty()) {
            return false;
        }

        final File sourceFile = new File(sourceFilePath);
        final File destinationDirectory = new File(destinationDirectoryPath);
        if (!sourceFile.isFile() || sourceFile.length() <= 0L) {
            Log.w(TAG, "Skipping invalid legacy media file: " + sourceFilePath);
            return false;
        }
        if (!destinationDirectory.isDirectory()
            && !destinationDirectory.mkdirs()) {
            Log.e(TAG,
                  "Failed to create migration destination: "
                      + destinationDirectoryPath);
            return false;
        }

        final File destinationFile =
            new File(destinationDirectory, sourceFile.getName());
        if (sameFile(sourceFile, destinationFile)) {
            return requestScan(
                activity.getApplicationContext(),
                sourceFile,
                mimeType,
                null);
        }

        try {
            MIGRATION_EXECUTOR.execute(new Runnable() {
                @Override
                public void run() {
                    migrateFileOnWorker(
                        activity.getApplicationContext(),
                        sourceFile,
                        destinationFile,
                        mimeType);
                }
            });
            Log.i(TAG,
                  "Queued legacy media migration: "
                      + sourceFile + " -> " + destinationFile);
            return true;
        } catch (final RuntimeException exception) {
            Log.e(TAG,
                  "Failed to queue legacy media migration for " + sourceFile,
                  exception);
            return false;
        }
    }

    public static boolean deleteFile(final String filePath) {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null || filePath == null || filePath.isEmpty()) {
            return false;
        }

        final File mediaFile = new File(filePath);
        final String absolutePath = mediaFile.getAbsolutePath();
        Uri mediaUri;
        synchronized (INDEXED_URIS) {
            mediaUri = INDEXED_URIS.remove(absolutePath);
        }
        if (mediaUri == null) {
            mediaUri = findMediaUri(
                activity.getApplicationContext(),
                absolutePath);
        }

        if (mediaUri != null) {
            try {
                final int deleted = activity.getContentResolver().delete(
                    mediaUri,
                    null,
                    null);
                if (deleted > 0) {
                    Log.i(TAG, "Deleted indexed media: " + absolutePath);
                    return true;
                }
            } catch (final RuntimeException exception) {
                Log.w(TAG,
                      "MediaStore deletion failed for " + absolutePath,
                      exception);
            }
        }

        final boolean deletedFile = !mediaFile.exists() || mediaFile.delete();
        if (!deletedFile) {
            Log.e(TAG, "Failed to delete media file: " + absolutePath);
            return false;
        }

        // Older vendor MediaProviders may retain a stale entry after a raw
        // filesystem deletion. A scan request gives them a chance to reconcile.
        try {
            MediaScannerConnection.scanFile(
                activity.getApplicationContext(),
                new String[] {absolutePath},
                null,
                null);
        } catch (final RuntimeException exception) {
            // The file is already gone, so a vendor scanner failure must not
            // make the native capacity cleanup delete additional segments.
            Log.w(TAG,
                  "Deleted media but failed to refresh its stale index: "
                      + absolutePath,
                  exception);
        }
        Log.i(TAG, "Deleted media file by path: " + absolutePath);
        return true;
    }

    private static void migrateFileOnWorker(final Context context,
                                            final File sourceFile,
                                            final File destinationFile,
                                            final String mimeType) {
        if (!sourceFile.isFile() || sourceFile.length() <= 0L) {
            return;
        }

        if (destinationFile.isFile()) {
            if (filesHaveSameContent(sourceFile, destinationFile)) {
                requestScan(
                    context,
                    destinationFile,
                    mimeType,
                    deleteSourceAfterScan(sourceFile, destinationFile));
            } else {
                Log.e(TAG,
                      "Migration destination already exists with different content;"
                          + " preserving both files: " + destinationFile);
            }
            return;
        }

        final File partialFile = new File(
            destinationFile.getParentFile(),
            "." + destinationFile.getName() + ".migration.partial");
        if (partialFile.exists() && !partialFile.delete()) {
            Log.e(TAG, "Failed to clear stale migration file: " + partialFile);
            return;
        }

        final long sourceLength = sourceFile.length();
        final long usableSpace = destinationFile.getParentFile().getUsableSpace();
        if (usableSpace > 0L && usableSpace < sourceLength) {
            Log.e(TAG,
                  "Insufficient space to migrate legacy media: " + sourceFile);
            return;
        }

        try (FileInputStream input = new FileInputStream(sourceFile);
             FileOutputStream output = new FileOutputStream(partialFile)) {
            final byte[] buffer = new byte[COPY_BUFFER_SIZE];
            int bytesRead;
            while ((bytesRead = input.read(buffer)) != -1) {
                output.write(buffer, 0, bytesRead);
            }
            output.flush();
            output.getFD().sync();
        } catch (final IOException exception) {
            Log.e(TAG, "Failed to copy legacy media: " + sourceFile, exception);
            if (!partialFile.delete() && partialFile.exists()) {
                Log.w(TAG, "Failed to remove partial migration: " + partialFile);
            }
            return;
        }

        if (partialFile.length() != sourceLength) {
            Log.e(TAG,
                  "Legacy media migration length mismatch: " + sourceFile);
            if (!partialFile.delete()) {
                Log.w(TAG, "Failed to remove partial migration: " + partialFile);
            }
            return;
        }

        if (!partialFile.renameTo(destinationFile)) {
            Log.e(TAG,
                  "Failed to finalize migrated media: " + destinationFile);
            if (!partialFile.delete()) {
                Log.w(TAG, "Failed to remove partial migration: " + partialFile);
            }
            return;
        }
        destinationFile.setLastModified(sourceFile.lastModified());

        requestScan(
            context,
            destinationFile,
            mimeType,
            deleteSourceAfterScan(sourceFile, destinationFile));
    }

    private static ScanSuccessAction deleteSourceAfterScan(
        final File sourceFile,
        final File destinationFile) {
        return new ScanSuccessAction() {
            @Override
            public void run() {
                if (deleteFile(sourceFile.getAbsolutePath())) {
                    Log.i(TAG,
                          "Migrated legacy media: "
                              + sourceFile + " -> " + destinationFile);
                } else {
                    Log.w(TAG,
                          "Migrated media was indexed but its legacy source"
                              + " could not be removed: " + sourceFile);
                }
            }
        };
    }

    private static boolean requestScan(final Context context,
                                       final File mediaFile,
                                       final String mimeType,
                                       final ScanSuccessAction successAction) {
        if (!mediaFile.isFile() || mediaFile.length() <= 0L) {
            Log.e(TAG,
                  "Cannot scan missing or empty media file: " + mediaFile);
            return false;
        }

        final String absolutePath = mediaFile.getAbsolutePath();
        final String[] mimeTypes =
            mimeType == null || mimeType.isEmpty()
                ? null
                : new String[] {mimeType};
        try {
            MediaScannerConnection.scanFile(
                context,
                new String[] {absolutePath},
                mimeTypes,
                new MediaScannerConnection.OnScanCompletedListener() {
                    @Override
                    public void onScanCompleted(final String scannedPath,
                                                final Uri uri) {
                        if (uri == null) {
                            Log.w(TAG,
                                  "Media scan completed without a MediaStore URI: "
                                      + scannedPath);
                            return;
                        }

                        synchronized (INDEXED_URIS) {
                            INDEXED_URIS.put(
                                new File(scannedPath).getAbsolutePath(),
                                uri);
                        }
                        Log.i(TAG,
                              "Media indexed: " + scannedPath + " -> " + uri);
                        if (successAction != null) {
                            successAction.run();
                        }
                    }
                });
            return true;
        } catch (final RuntimeException exception) {
            Log.e(TAG,
                  "Failed to request media scan for " + absolutePath,
                  exception);
            return false;
        }
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
                    final String volumeName = cursor.getString(1);
                    specificFilesUri = MediaStore.Files.getContentUri(volumeName);
                } else {
                    specificFilesUri = filesUri;
                }
                return ContentUris.withAppendedId(
                    specificFilesUri,
                    cursor.getLong(0));
            }
        } catch (final RuntimeException exception) {
            Log.w(TAG,
                  "Failed to query MediaStore for " + absolutePath,
                  exception);
        }
        return null;
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

    private static boolean sameFile(final File first, final File second) {
        try {
            return first.getCanonicalFile().equals(second.getCanonicalFile());
        } catch (final IOException exception) {
            return first.getAbsoluteFile().equals(second.getAbsoluteFile());
        }
    }

    private static boolean filesHaveSameContent(final File first,
                                                final File second) {
        if (!first.isFile()
            || !second.isFile()
            || first.length() != second.length()) {
            return false;
        }

        try (FileInputStream firstInput = new FileInputStream(first);
             FileInputStream secondInput = new FileInputStream(second)) {
            final byte[] firstBuffer = new byte[COPY_BUFFER_SIZE];
            final byte[] secondBuffer = new byte[COPY_BUFFER_SIZE];
            while (true) {
                final int firstRead = firstInput.read(firstBuffer);
                final int secondRead = secondInput.read(secondBuffer);
                if (firstRead != secondRead) {
                    return false;
                }
                if (firstRead == -1) {
                    return true;
                }
                for (int index = 0; index < firstRead; ++index) {
                    if (firstBuffer[index] != secondBuffer[index]) {
                        return false;
                    }
                }
            }
        } catch (final IOException exception) {
            Log.w(TAG,
                  "Failed to compare legacy media migration collision",
                  exception);
            return false;
        }
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
