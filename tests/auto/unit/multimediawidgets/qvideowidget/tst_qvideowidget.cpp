// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <qtmultimediaglobal.h>
#include <QtTest/qtest.h>
#include <QtTest/qsignalspy.h>
#include <QtCore/qscopeguard.h>

#include "qvideowidget.h"
#include "qvideosink.h"
#include "qmediaplayer.h"

#include <qvideoframeformat.h>
#include <qvideoframe.h>

#include <QtWidgets/qapplication.h>

#include <qmockintegration.h>
#include <qmockmediaplayer.h>
#include <qmockvideosink.h>

#include <private/mediabackendutils_p.h>
#include <private/qvideowindow_p.h>


Q_ENABLE_MOCK_MULTIMEDIA_PLUGIN

class tst_QVideoWidget : public QObject
{
    Q_OBJECT
public slots:
    void initTestCase();

private slots:
    void nullObject();

    void show();
    void fullScreen();
    void aspectRatio();
    void sizeHint_data();
    void sizeHint();
#if 0
    void brightness_data() { color_data(); }
    void brightness();
    void contrast_data() { color_data(); }
    void contrast();
    void hue_data() { color_data(); }
    void hue();
    void saturation_data() { color_data(); }
    void saturation();
#endif

    void paint();
    void preferredFrameRate_followsStreamFrameRate();

private:
//    void color_data();
};

class QtTestVideoWidget : public QVideoWidget
{
public:
    QtTestVideoWidget(QWidget *parent = nullptr)
        : QVideoWidget(parent)
    {
        resize(320, 240);
    }
};

void tst_QVideoWidget::initTestCase()
{
#ifdef Q_OS_MACOS
    if (isCI())
        QSKIP("SKIP on macOS CI since metal is not supported, otherwise it often crashes. To be "
              "fixed.");
#endif
}

void tst_QVideoWidget::nullObject()
{
    QtTestVideoWidget widget;

    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    widget.setFullScreen(true);
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QCOMPARE(widget.isFullScreen(), true);

    widget.setAspectRatioMode(Qt::IgnoreAspectRatio);
    QCOMPARE(widget.aspectRatioMode(), Qt::IgnoreAspectRatio);
}



void tst_QVideoWidget::show()
{
    QtTestVideoWidget widget;

    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    widget.resize(640, 480);
    QCOMPARE(widget.size(), QSize(640, 480));

    widget.move(10, 10);
    QCOMPARE(widget.size(), QSize(640, 480));

    widget.hide();
}

void tst_QVideoWidget::aspectRatio()
{
    QtTestVideoWidget widget;
    QMediaPlayer player;
    player.setVideoOutput(&widget);

    // Test the aspect ratio defaults to keeping the aspect ratio.
    QCOMPARE(widget.aspectRatioMode(), Qt::KeepAspectRatio);

    // Test the control has been informed of the aspect ratio change, post show.
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QCOMPARE(widget.aspectRatioMode(), Qt::KeepAspectRatio);

    // Test an aspect ratio change is enforced immediately while visible.
    widget.setAspectRatioMode(Qt::IgnoreAspectRatio);
    QCOMPARE(widget.aspectRatioMode(), Qt::IgnoreAspectRatio);

    // Test an aspect ratio set while not visible is respected.
    widget.hide();
    widget.setAspectRatioMode(Qt::KeepAspectRatio);
    QCOMPARE(widget.aspectRatioMode(), Qt::KeepAspectRatio);
    widget.show();
    QCOMPARE(widget.aspectRatioMode(), Qt::KeepAspectRatio);
}

void tst_QVideoWidget::sizeHint_data()
{
#ifdef Q_OS_MACOS
    QSKIP("QTBUG-26481 - Crashes on Mac");
#endif

    QTest::addColumn<QSize>("frameSize");
    QTest::addColumn<QRect>("viewport");
    QTest::addColumn<QSize>("expectedSize");

    QTest::newRow("640x480")
            << QSize(640, 480)
            << QRect(0, 0, 640, 480)
            << QSize(640, 480);

//    QTest::newRow("800x600, (80,60, 640x480) viewport")
//            << QSize(800, 600)
//            << QRect(80, 60, 640, 480)
//            << QSize(640, 480);
}

void tst_QVideoWidget::sizeHint()
{
#ifdef Q_OS_MACOS
    QSKIP("QTBUG-26481 - Crashes on Mac");
#endif

    QFETCH(QSize, frameSize);
//    QFETCH(QRect, viewport);
    QFETCH(QSize, expectedSize);

    QtTestVideoWidget widget;
    QMediaPlayer player;

    player.setVideoOutput(&widget);
    auto mockSink = QMockIntegration::instance()->lastVideoSink();

    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    mockSink->setNativeSize(frameSize);

    QCOMPARE(widget.sizeHint(), expectedSize);
}


void tst_QVideoWidget::fullScreen()
{
    QtTestVideoWidget widget;
    QMediaPlayer player;
    player.setVideoOutput(&widget);
    widget.showNormal();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    Qt::WindowFlags windowFlags = widget.windowFlags();

    QSignalSpy spy(&widget, &QVideoWidget::fullScreenChanged);

    // Test showing full screen with setFullScreen(true).
    widget.setFullScreen(true);
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QCOMPARE(widget.isFullScreen(), true);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.value(0).value(0).toBool(), true);

    // Test returning to normal with setFullScreen(false).
    widget.setFullScreen(false);
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QCOMPARE(widget.isFullScreen(), false);
    QCOMPARE(spy.size(), 2);
    QCOMPARE(spy.value(1).value(0).toBool(), false);
    QCOMPARE(widget.windowFlags(), windowFlags);

    // Test showing full screen with showFullScreen().
    widget.showFullScreen();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QCOMPARE(widget.isFullScreen(), true);
    QCOMPARE(spy.size(), 3);
    QCOMPARE(spy.value(2).value(0).toBool(), true);

    // Test returning to normal with showNormal().
    widget.showNormal();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QCOMPARE(widget.isFullScreen(), false);
    QCOMPARE(spy.size(), 4);
    QCOMPARE(spy.value(3).value(0).toBool(), false);
    QCOMPARE(widget.windowFlags(), windowFlags);

    // Test setFullScreen(false) and showNormal() do nothing when isFullScreen() == false.
    widget.setFullScreen(false);
    QCOMPARE(widget.isFullScreen(), false);
    QCOMPARE(spy.size(), 4);
    widget.showNormal();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QCOMPARE(widget.isFullScreen(), false);
    QCOMPARE(spy.size(), 4);

    // Test setFullScreen(true) and showFullScreen() do nothing when isFullScreen() == true.
    widget.showFullScreen();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    widget.setFullScreen(true);
    QCOMPARE(widget.isFullScreen(), true);
    QCOMPARE(spy.size(), 5);
    widget.showFullScreen();
    QCOMPARE(widget.isFullScreen(), true);
    QCOMPARE(spy.size(), 5);
}

static const uchar rgb32ImageData[] =
{
    0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00
};

void tst_QVideoWidget::paint()
{
    QtTestVideoWidget widget;
    QMediaPlayer player;
    player.setVideoOutput(&widget);
    widget.resize(640,480);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QVideoFrameFormat format(QSize(2, 2), QVideoFrameFormat::Format_XRGB8888);
    QVideoFrame frame(format);
    QVERIFY(frame.map(QVideoFrame::ReadWrite));
    uchar *data = frame.bits(0);
    memcpy(data, rgb32ImageData, sizeof(rgb32ImageData));
    frame.unmap();

    auto *sink = widget.videoSink();
    emit sink->setVideoFrame(frame);

    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

void tst_QVideoWidget::preferredFrameRate_followsStreamFrameRate()
{
#ifndef QT_BUILD_INTERNAL
    QSKIP("Needs a developer build");
#else
    // QVideoWidget shows the video in an internal QVideoWindow, which asks for
    // the rate of a media player's frames when a display with a variable
    // refresh rate shows it evenly. This display may have a fixed one.
    qt_setVideoWindowAssumesVariableRefreshRate(true);
    const auto restore = qScopeGuard([] { qt_setVideoWindowAssumesVariableRefreshRate(false); });
    QVideoWidget widget;
    QMediaPlayer player;
    player.setVideoOutput(&widget);
    QMockIntegration::instance()->lastPlayer()->setIsValid(true);
    player.setSource(QUrl(QStringLiteral("file:///video.mp4")));
    player.play();
    QCOMPARE(player.playbackState(), QMediaPlayer::PlayingState);
    widget.resize(64, 48);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QVideoWindow *videoWindow = nullptr;
    for (QWindow *window : QGuiApplication::allWindows()) {
        if (auto *candidate = qobject_cast<QVideoWindow *>(window))
            videoWindow = candidate;
    }
    if (!videoWindow)
        QSKIP("QVideoWidget doesn't use a QVideoWindow on this platform");

    // A rate the display shows exactly: its refresh rate divided by the smallest
    // whole number above 1 that divides it. Only where update requests are paced
    // to the display, see tst_QVideoFrameBackend.
    const bool paced = QGuiApplication::platformName() == u"cocoa";
    const qreal refreshRate = videoWindow->screen()->refreshRate();
    int refreshes = 2;
    while (refreshes < qRound(refreshRate) && qRound(refreshRate) % refreshes != 0)
        ++refreshes;
    const qreal exact = float(refreshRate / refreshes);
    QVideoFrameFormat format(QSize(2, 2), QVideoFrameFormat::Format_XRGB8888);
    format.setStreamFrameRate(exact);
    widget.videoSink()->setVideoFrame(QVideoFrame(format));
    QCOMPARE(videoWindow->preferredFrameRate(), paced ? exact : 0.0);

    widget.videoSink()->setVideoFrame(QVideoFrame());
    QCOMPARE(videoWindow->preferredFrameRate(), 0.0);
#endif
}

QTEST_MAIN(tst_QVideoWidget)

#include "tst_qvideowidget.moc"
