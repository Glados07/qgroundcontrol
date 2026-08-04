/****************************************************************************
 *
 * Local decoded-frame photo sizing policy regression tests.
 *
 ****************************************************************************/

#include "GimbalPhotoCapturePolicy.h"

#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtTest/QTest>

class GimbalPhotoCapturePolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void recordingResolutionAndDpr_data();
    void recordingResolutionAndDpr();
    void dci4kPreservesCompleteFrame();
    void fractionalDprCorrectionProducesExactOutput();
    void exactOutputDoesNotDetach();
    void invalidGeometryIsRejected();
};

void GimbalPhotoCapturePolicyTest::recordingResolutionAndDpr_data()
{
    QTest::addColumn<QSize>("recordingSize");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<QSize>("logicalGrabSize");

    QTest::newRow("720p-dpr1")
        << QSize(1280, 720) << qreal(1.0) << QSize(1280, 720);
    QTest::newRow("1080p-dpr2")
        << QSize(1920, 1080) << qreal(2.0) << QSize(960, 540);
    QTest::newRow("1440p-dpr1.5")
        << QSize(2560, 1440) << qreal(1.5) << QSize(1707, 960);
    QTest::newRow("uhd-dpr2")
        << QSize(3840, 2160) << qreal(2.0) << QSize(1920, 1080);
}

void GimbalPhotoCapturePolicyTest::recordingResolutionAndDpr()
{
    QFETCH(QSize, recordingSize);
    QFETCH(qreal, dpr);
    QFETCH(QSize, logicalGrabSize);

    const auto geometry = GimbalPhotoCapturePolicy::captureGeometry(
        recordingSize,
        QSize(1920, 1080),
        dpr);

    QVERIFY(geometry.isValid());
    QCOMPARE(geometry.outputPixelSize, recordingSize);
    QCOMPARE(geometry.contentPixelSize, recordingSize);
    QCOMPARE(geometry.grabLogicalSize, logicalGrabSize);
}

void GimbalPhotoCapturePolicyTest::dci4kPreservesCompleteFrame()
{
    const auto geometry = GimbalPhotoCapturePolicy::captureGeometry(
        QSize(4096, 2160),
        QSize(1920, 1080),
        2.0);

    QVERIFY(geometry.isValid());
    QCOMPARE(geometry.outputPixelSize, QSize(4096, 2160));
    QCOMPARE(geometry.contentPixelSize, QSize(3840, 2160));
    QCOMPARE(geometry.grabLogicalSize, QSize(1920, 1080));

    QImage frame(3840, 2160, QImage::Format_RGB32);
    {
        QPainter painter(&frame);
        painter.fillRect(QRect(0, 0, 1920, 1080), Qt::red);
        painter.fillRect(QRect(1920, 0, 1920, 1080), Qt::green);
        painter.fillRect(QRect(0, 1080, 1920, 1080), Qt::blue);
        painter.fillRect(QRect(1920, 1080, 1920, 1080), Qt::yellow);
    }
    frame.setDevicePixelRatio(2.0);
    const QImage output =
        GimbalPhotoCapturePolicy::prepareImageForSaving(frame, geometry);

    QCOMPARE(output.size(), QSize(4096, 2160));
    QCOMPARE(output.pixelColor(0, 1080), QColor(Qt::black));
    QCOMPARE(output.pixelColor(127, 1080), QColor(Qt::black));
    QCOMPARE(output.pixelColor(128, 0), QColor(Qt::red));
    QCOMPARE(output.pixelColor(3967, 0), QColor(Qt::green));
    QCOMPARE(output.pixelColor(128, 2159), QColor(Qt::blue));
    QCOMPARE(output.pixelColor(3967, 2159), QColor(Qt::yellow));
    QCOMPARE(output.pixelColor(3968, 1080), QColor(Qt::black));
}

void GimbalPhotoCapturePolicyTest::fractionalDprCorrectionProducesExactOutput()
{
    const auto geometry = GimbalPhotoCapturePolicy::captureGeometry(
        QSize(1920, 1080),
        QSize(1920, 1080),
        2.625);
    QVERIFY(geometry.isValid());
    QCOMPARE(geometry.grabLogicalSize, QSize(731, 411));

    // Qt's logical-target x DPR rounding can differ by a pixel or two.
    QImage roundedGrab(1919, 1079, QImage::Format_RGB32);
    roundedGrab.fill(Qt::red);
    roundedGrab.setDevicePixelRatio(2.625);
    const QImage output =
        GimbalPhotoCapturePolicy::prepareImageForSaving(roundedGrab, geometry);

    QCOMPARE(output.size(), QSize(1920, 1080));
    QCOMPARE(output.pixelColor(960, 540), QColor(Qt::red));
}

void GimbalPhotoCapturePolicyTest::exactOutputDoesNotDetach()
{
    const auto geometry = GimbalPhotoCapturePolicy::captureGeometry(
        QSize(1920, 1080),
        QSize(1920, 1080),
        2.0);
    QVERIFY(geometry.isValid());

    QImage frame(1920, 1080, QImage::Format_RGB32);
    frame.fill(Qt::cyan);
    frame.setDevicePixelRatio(2.0);
    const qint64 originalCacheKey = frame.cacheKey();
    const QImage output =
        GimbalPhotoCapturePolicy::prepareImageForSaving(frame, geometry);

    QCOMPARE(output.size(), QSize(1920, 1080));
    QCOMPARE(output.devicePixelRatio(), qreal(2.0));
    QCOMPARE(output.cacheKey(), originalCacheKey);
}

void GimbalPhotoCapturePolicyTest::invalidGeometryIsRejected()
{
    QVERIFY(!GimbalPhotoCapturePolicy::captureGeometry(
                 QSize(), QSize(1920, 1080), 1.0).isValid());
    QVERIFY(!GimbalPhotoCapturePolicy::captureGeometry(
                 QSize(0, 1080), QSize(1920, 1080), 1.0).isValid());
    QVERIFY(!GimbalPhotoCapturePolicy::captureGeometry(
                 QSize(1920, 1080), QSize(1920, 1080), 0.0).isValid());
    QVERIFY(GimbalPhotoCapturePolicy::prepareImageForSaving(
                QImage(),
                GimbalPhotoCapturePolicy::CaptureGeometry()).isNull());
}

QTEST_GUILESS_MAIN(GimbalPhotoCapturePolicyTest)

#include "GimbalPhotoCapturePolicyTest.moc"
