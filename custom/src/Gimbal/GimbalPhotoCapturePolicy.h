/****************************************************************************
 *
 * Local decoded-frame photo sizing policy for the custom gimbal camera.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QSize>

class QImage;

class GimbalPhotoCapturePolicy
{
public:
    struct CaptureGeometry {
        QSize outputPixelSize;
        QSize contentPixelSize;
        QSize grabLogicalSize;

        bool isValid() const {
            return outputPixelSize.isValid()
                && !outputPixelSize.isEmpty()
                && contentPixelSize.isValid()
                && !contentPixelSize.isEmpty()
                && grabLogicalSize.isValid()
                && !grabLogicalSize.isEmpty();
        }
    };

    /// Resolves an offscreen Qt Quick target in device-independent pixels.
    /// The returned output size is always the requested recording size. When
    /// the live stream uses a different aspect ratio, content is fitted inside
    /// that output so the complete decoded frame remains visible.
    static CaptureGeometry captureGeometry(const QSize& outputPixelSize,
                                            const QSize& sourcePixelSize,
                                            qreal effectiveDevicePixelRatio);

    /// Corrects fractional-DPR rounding and adds centered black padding when
    /// the recording and decoded-stream aspect ratios differ.
    static QImage prepareImageForSaving(
        const QImage& grabbedImage,
        const CaptureGeometry& geometry);
};
