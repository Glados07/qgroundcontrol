/****************************************************************************
 *
 * Local decoded-frame photo sizing policy for the custom gimbal camera.
 *
 ****************************************************************************/

#include "GimbalPhotoCapturePolicy.h"

#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtGui/QPainter>

GimbalPhotoCapturePolicy::CaptureGeometry
GimbalPhotoCapturePolicy::captureGeometry(
    const QSize& outputPixelSize,
    const QSize& sourcePixelSize,
    qreal effectiveDevicePixelRatio)
{
    CaptureGeometry geometry;
    if (!outputPixelSize.isValid()
        || outputPixelSize.isEmpty()
        || !qIsFinite(effectiveDevicePixelRatio)
        || effectiveDevicePixelRatio <= 0.0) {
        return geometry;
    }

    geometry.outputPixelSize = outputPixelSize;
    geometry.contentPixelSize = sourcePixelSize.isValid()
        && !sourcePixelSize.isEmpty()
        ? sourcePixelSize.scaled(outputPixelSize, Qt::KeepAspectRatio)
        : outputPixelSize;
    if (!geometry.contentPixelSize.isValid()
        || geometry.contentPixelSize.isEmpty()) {
        return CaptureGeometry();
    }

    geometry.grabLogicalSize = QSize(
        qMax(1,
             qRound(geometry.contentPixelSize.width()
                    / effectiveDevicePixelRatio)),
        qMax(1,
             qRound(geometry.contentPixelSize.height()
                    / effectiveDevicePixelRatio)));
    return geometry;
}

QImage GimbalPhotoCapturePolicy::prepareImageForSaving(
    const QImage& grabbedImage,
    const CaptureGeometry& geometry)
{
    if (grabbedImage.isNull() || !geometry.isValid()) {
        return QImage();
    }

    // The common 16:9 path already has the requested physical pixel size.
    // Return it without detaching/copying the potentially large 4K buffer.
    if (grabbedImage.size() == geometry.outputPixelSize
        && geometry.contentPixelSize == geometry.outputPixelSize) {
        return grabbedImage;
    }

    // Do not clear the grab's DPR metadata on a shared full-resolution image:
    // QImage would detach and duplicate the entire 4K buffer. Explicit source
    // and target rectangles below make the painter independent of that DPR.
    QImage contentImage = grabbedImage.size() == geometry.contentPixelSize
        ? grabbedImage
        : grabbedImage.scaled(geometry.contentPixelSize,
                              Qt::IgnoreAspectRatio,
                              Qt::SmoothTransformation);
    if (contentImage.isNull()) {
        return QImage();
    }

    if (contentImage.size() == geometry.outputPixelSize) {
        return contentImage;
    }

    QImage outputImage(geometry.outputPixelSize, QImage::Format_RGB32);
    if (outputImage.isNull()) {
        return QImage();
    }
    outputImage.fill(Qt::black);

    const QPoint contentTopLeft(
        (geometry.outputPixelSize.width()
         - geometry.contentPixelSize.width()) / 2,
        (geometry.outputPixelSize.height()
         - geometry.contentPixelSize.height()) / 2);
    QPainter painter(&outputImage);
    painter.drawImage(QRect(contentTopLeft, geometry.contentPixelSize),
                      contentImage,
                      contentImage.rect());
    painter.end();
    return outputImage;
}
