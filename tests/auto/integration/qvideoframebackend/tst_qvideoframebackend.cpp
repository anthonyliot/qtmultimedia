// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QtCore/qdebug.h>
#include <QtCore/qelapsedtimer.h>
#include <QtCore/qthreadpool.h>

#include <QtGui/qguiapplication.h>
#include <QtGui/qscreen.h>
#include <QtGui/rhi/qrhi.h>

#include <QtMultimedia/qmediaplayer.h>
#include <QtMultimedia/qvideoframe.h>

#include <QtMultimedia/private/qmultimediautils_p.h>
#include <QtMultimedia/private/qthreadlocalrhi_p.h>
#include <QtMultimedia/private/qvideotexturehelper_p.h>
#include <QtMultimedia/private/qvideowindow_p.h>

#include <QtMultimediaTestLib/private/mediabackendutils_p.h>
#include <QtMultimediaTestLib/private/qintegrationtestbase_p.h>
#include <QtMultimediaTestLib/private/mediafileselector_p.h>
#include <QtMultimediaTestLib/private/testvideosink_p.h>

#include <QtTest/qtest.h>

#include <algorithm>
#include <list>
#include <memory>

#ifdef Q_OS_MACOS
#  include <CoreFoundation/CoreFoundation.h>
#  include <QtMultimedia/private/qavfhelpers_p.h>
#endif

class tst_QVideoFrameBackend : public QIntegrationTestBase
{
    Q_OBJECT

public slots:
    void initTestCase();
    void init() { }
    void cleanup() { }

private slots:
    void testMediaFilesAreSupported();

    void toImage_retainsThePreviousMappedState_data();
    void toImage_retainsThePreviousMappedState();

    void toImage_rendersUpdatedFrame_afterMappingInWriteModeAndModifying_data();
    void toImage_rendersUpdatedFrame_afterMappingInWriteModeAndModifying();

    void toImage_returnsImage_whenCalledFromSeparateThreadAndWhileRenderingToWindow_data();
    void toImage_returnsImage_whenCalledFromSeparateThreadAndWhileRenderingToWindow();

    void playback_deliversFrames_whileRunLoopIsInMode_data();
    void playback_deliversFrames_whileRunLoopIsInMode();

    void streamFrameRate_isReportedForPlayedFrames();

    void videoWindow_preferredFrameRate_isSetDuringPlayback();
    void videoWindow_preferredFrameRate_followsPlaybackRate();
    void videoWindow_preferredFrameRate_isResetWhenPaused();
    void videoWindow_preferredFrameRate_isOnlySetForVariableRefreshRate();
    void videoWindow_receivesFrames_whileHidden();
    void videoWindow_receivesFrames_afterMovingToAnotherScreen();
    void videoWindow_canBeDeleted_whilePlaying();

private:
    QVideoFrame createDefaultFrame() const;

    QVideoFrame createMediaPlayerFrame() const;

    using FrameCreator = decltype(&tst_QVideoFrameBackend::createDefaultFrame);

    template <typename F>
    void addMediaPlayerFrameTestData(F &&f);

private:
    MaybeUrl m_oneRedFrameVideo{ q23::unexpect };
    MaybeUrl m_colorsVideo{ q23::unexpect };
    MediaFileSelector m_mediaSelector;
};

QVideoFrame tst_QVideoFrameBackend::createDefaultFrame() const
{
    return QVideoFrame(QVideoFrameFormat(QSize(10, 20), QVideoFrameFormat::Format_ARGB8888));
}

QVideoFrame tst_QVideoFrameBackend::createMediaPlayerFrame() const
{
    if (!m_oneRedFrameVideo)
        return {};

    TestVideoSink sink;
    QMediaPlayer player;

    player.setVideoOutput(&sink);
    player.setSource(*m_oneRedFrameVideo);

    player.play();

    return sink.waitForFrame();
}

template <typename F>
void tst_QVideoFrameBackend::addMediaPlayerFrameTestData(F &&f)
{
    if (!m_oneRedFrameVideo) {
        qWarning() << "Skipping test data with mediaplayer as the source cannot be open."
                      "\nSee the test case 'testMediaFilesAreSupported' for details";
        return;
    }

    if (isGStreamerPlatform()) {
        qWarning() << "createMediaPlayerFrame spuriously fails with gstreamer";
        return;
    }

    if (isOhosPlatform()) {
        // createMediaPlayerFrame returns an invalid frame on OHOS because the
        // OHOS video output cannot deliver textures into a bare QVideoSink
        // that has no live RHI.
        qWarning() << "createMediaPlayerFrame returns no frame on OHOS";
        return;
    }

    f();
}

void tst_QVideoFrameBackend::initTestCase()
{
    if (!initIntegrationTestCase())
        return;

#ifdef Q_OS_ANDROID
    qWarning() << "Skip media selection, QTBUG-118571";
    return;
#endif

    m_oneRedFrameVideo = m_mediaSelector.select("qrc:/testdata/one_red_frame.mp4");
    m_colorsVideo = m_mediaSelector.select("qrc:/testdata/colors.mp4");
}

void tst_QVideoFrameBackend::testMediaFilesAreSupported()
{
#ifdef Q_OS_ANDROID
    QSKIP("Skip test cases with mediaPlayerFrame on Android CI, because of QTBUG-118571");
#endif
#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects some H.264/HEVC/AV1 profiles used by the test media");
#endif

    QCOMPARE(m_mediaSelector.dumpErrors(), "");
}

void tst_QVideoFrameBackend::toImage_retainsThePreviousMappedState_data()
{
    QTest::addColumn<FrameCreator>("frameCreator");
    QTest::addColumn<QVideoFrame::MapMode>("initialMapMode");

    // clang-format off
    QTest::addRow("defaulFrame.notMapped") << &tst_QVideoFrameBackend::createDefaultFrame
                                           << QVideoFrame::NotMapped;
    QTest::addRow("defaulFrame.readOnly") << &tst_QVideoFrameBackend::createDefaultFrame
                                          << QVideoFrame::ReadOnly;

    addMediaPlayerFrameTestData([]()
    {
        QTest::addRow("mediaPlayerFrame.notMapped")
                << &tst_QVideoFrameBackend::createMediaPlayerFrame
                << QVideoFrame::NotMapped;
        QTest::addRow("mediaPlayerFrame.readOnly")
                << &tst_QVideoFrameBackend::createMediaPlayerFrame
                << QVideoFrame::ReadOnly;
    });

    // clang-format on
}

void tst_QVideoFrameBackend::toImage_retainsThePreviousMappedState()
{
    QFETCH(const FrameCreator, frameCreator);
    QFETCH(const QVideoFrame::MapMode, initialMapMode);
    const bool initiallyMapped = initialMapMode != QVideoFrame::NotMapped;

    QVideoFrame frame = std::invoke(frameCreator, this);
    QVERIFY(frame.isValid());

    frame.map(initialMapMode);
    QCOMPARE(frame.mapMode(), initialMapMode);

    QImage image = frame.toImage();
    QVERIFY(!image.isNull());

    QCOMPARE(frame.mapMode(), initialMapMode);
    QCOMPARE(frame.isMapped(), initiallyMapped);
}

void tst_QVideoFrameBackend::toImage_rendersUpdatedFrame_afterMappingInWriteModeAndModifying_data()
{
    QTest::addColumn<FrameCreator>("frameCreator");
    QTest::addColumn<QVideoFrame::MapMode>("mapMode");

    // clang-format off
    QTest::addRow("defaulFrame.writeOnly") << &tst_QVideoFrameBackend::createDefaultFrame
                                           << QVideoFrame::WriteOnly;
    QTest::addRow("defaulFrame.readWrite") << &tst_QVideoFrameBackend::createDefaultFrame
                                           << QVideoFrame::ReadWrite;

    addMediaPlayerFrameTestData([]()
    {
        QTest::addRow("mediaPlayerFrame.writeOnly")
                << &tst_QVideoFrameBackend::createMediaPlayerFrame
                << QVideoFrame::WriteOnly;
        QTest::addRow("mediaPlayerFrame.readWrite")
                << &tst_QVideoFrameBackend::createMediaPlayerFrame
                << QVideoFrame::ReadWrite;
    });
    // clang-format on
}

void tst_QVideoFrameBackend::toImage_rendersUpdatedFrame_afterMappingInWriteModeAndModifying()
{
    QFETCH(const FrameCreator, frameCreator);
    QFETCH(const QVideoFrame::MapMode, mapMode);

    if (isWMFPlatform())
        // TODO: WMF backend only supports WriteOnly mapping
        QSKIP("WMF backend does not support WriteOnly mapping");

    // Arrange

    QVideoFrame frame = std::invoke(frameCreator, this);
    QVERIFY(frame.isValid());

    QImage originalImage = frame.toImage();
    QVERIFY(!originalImage.isNull());

    // Act: map the frame in write mode and change the top level pixel
    frame.map(mapMode);
    QVERIFY(frame.isWritable());

    QCOMPARE_NE(frame.pixelFormat(), QVideoFrameFormat::Format_Invalid);

    const QVideoTextureHelper::TextureDescription *textureDescription =
            QVideoTextureHelper::textureDescription(frame.pixelFormat());
    QVERIFY(textureDescription);

    uchar *firstPlaneBits = frame.bits(0);
    QVERIFY(firstPlaneBits);

    for (int i = 0; i < textureDescription->strideFactor; ++i)
        firstPlaneBits[i] = ~firstPlaneBits[i];

    frame.unmap();

    // get an image from modified frame
    QImage modifiedImage = frame.toImage();

    // Assert

    QVERIFY(!frame.isMapped());
    QCOMPARE_NE(originalImage.pixel(0, 0), modifiedImage.pixel(0, 0));
    QCOMPARE(originalImage.pixel(1, 0), modifiedImage.pixel(1, 0));
    QCOMPARE(originalImage.pixel(1, 1), modifiedImage.pixel(1, 1));
}

void tst_QVideoFrameBackend::toImage_returnsImage_whenCalledFromSeparateThreadAndWhileRenderingToWindow_data()
{
    QTest::addColumn<QRhi::Implementation>("backend");

#if QT_CONFIG(opengl)
    QTest::newRow("OpenGL") << QRhi::OpenGLES2;
#endif
#if QT_CONFIG(metal)
    QTest::newRow("Metal") << QRhi::Metal;
#endif
#if defined(Q_OS_WIN)
    QTest::newRow("D3D11") << QRhi::D3D11;
#endif
}

void tst_QVideoFrameBackend::toImage_returnsImage_whenCalledFromSeparateThreadAndWhileRenderingToWindow()
{
    QFETCH(QRhi::Implementation, backend);

#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects the H.264 profile used by colors.mp4");
#endif

    if (isCI()) {
#ifdef Q_OS_MACOS
        if (backend == QRhi::Metal)
            QSKIP("SKIP on macOS because of crash and error \"Failed to create QWindow::MetalSurface. Metal is not supported by any of the GPUs in this system.\"");
#elif defined(Q_OS_ANDROID)
        QSKIP("SKIP initTestCase on CI, because of QTBUG-118571");
#endif
    }
    // Arrange
    QVideoWindow window;
    window.show();

    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QMediaPlayer player;
    player.setVideoOutput(&window);

    const QVideoSink *sink = window.videoSink();
    std::list<QImage> images;

    QThreadPool threadPool;
    threadPool.setMaxThreadCount(4);

    // act
    connect(sink, &QVideoSink::videoFrameChanged, sink, [&](const QVideoFrame &frame) {
        if (!frame.isValid())
            return; // ignore last frame

        images.push_back({});

        threadPool.start([it = std::prev(images.end()), frame, backend]() {
            qSetPreferredThreadLocalRhiBackend(backend);
            *it = frame.toImage();
        });
    });

    // Arrange some more
    player.setSource(*m_colorsVideo);
    player.setLoops(5);
    player.setPlaybackRate(5.f);
    player.play();

    QTRY_COMPARE_GE_WITH_TIMEOUT(images.size(), 25u, std::chrono::seconds(60));
    player.stop();

    QVERIFY(threadPool.waitForDone(std::chrono::seconds(60)));

    const size_t validImagesCount = std::count_if(
            images.begin(), images.end(), [](const QImage &image) { return !image.isNull(); });

    QCOMPARE(validImagesCount, images.size());
}

void tst_QVideoFrameBackend::playback_deliversFrames_whileRunLoopIsInMode_data()
{
    QTest::addColumn<QString>("mode");

    // While a menu is open or a window is live resized, and in modal sessions
    QTest::newRow("event tracking") << QStringLiteral("NSEventTrackingRunLoopMode");
    QTest::newRow("modal panel") << QStringLiteral("NSModalPanelRunLoopMode");
}

void tst_QVideoFrameBackend::playback_deliversFrames_whileRunLoopIsInMode()
{
#ifndef Q_OS_MACOS
    QSKIP("Run loop modes are specific to macOS");
#else
    QFETCH(const QString, mode);
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
    if (QGuiApplication::platformName() != u"cocoa")
        QSKIP("Only the cocoa platform delivers events in these run loop modes");

    // Frames keep coming while the main run loop runs in another mode than the
    // default one
    QVideoSink sink;
    QMediaPlayer player;
    player.setVideoOutput(&sink);
    int frames = 0;
    connect(&sink, &QVideoSink::videoFrameChanged, &sink, [&](const QVideoFrame &frame) {
        if (frame.isValid())
            ++frames;
    });
    player.setSource(*m_colorsVideo);
    player.setLoops(QMediaPlayer::Infinite);
    player.play();
    QTRY_VERIFY(frames > 0);

    const int before = frames;
    const CFStringRef runLoopMode = mode.toCFString();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1000)
        CFRunLoopRunInMode(runLoopMode, 0.05, false);
    CFRelease(runLoopMode);
    QVERIFY2(frames - before >= 10,
             qPrintable(QStringLiteral("%1 frames in 1 s at 25 fps").arg(frames - before)));
#endif
}

void tst_QVideoFrameBackend::streamFrameRate_isReportedForPlayedFrames()
{
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects the H.264 profile used by colors.mp4");
#endif
    if (!isFFMPEGPlatform() && !isDarwinPlatform())
        QSKIP("This backend doesn't report the stream frame rate");

    // The frames of a played video tell the frame rate of its stream, 25 fps
    QVideoSink sink;
    QMediaPlayer player;
    player.setVideoOutput(&sink);
    int frames = 0;
    QList<qreal> otherRates;
    connect(&sink, &QVideoSink::videoFrameChanged, &sink, [&](const QVideoFrame &frame) {
        if (!frame.isValid())
            return;
        ++frames;
        if (frame.surfaceFormat().streamFrameRate() != 25.0)
            otherRates.append(frame.surfaceFormat().streamFrameRate());
    });
    player.setSource(*m_colorsVideo);
    player.play();

    QTRY_COMPARE_GE(frames, 5);
    QCOMPARE(otherRates, QList<qreal>());
}

namespace {
// Counts the valid frames a video window's sink gets
struct FrameCounter
{
    explicit FrameCounter(QVideoWindow &window)
    {
        QObject::connect(window.videoSink(), &QVideoSink::videoFrameChanged, &window,
                         [this](const QVideoFrame &frame) {
                             if (frame.isValid()) {
                                 ++frames;
                                 streamFrameRate = frame.surfaceFormat().streamFrameRate();
                             }
                         });
    }
    // Frames within the next ms milliseconds, running the event loop
    int framesWithin(int ms)
    {
        const int before = frames;
        QTest::qWait(ms);
        return frames - before;
    }
    int frames = 0;
    qreal streamFrameRate = 0;
};

// Where QVideoWindow sets a preferred frame rate, if the display has a variable
// refresh rate: where update requests are paced to the display (the policy
// itself is tested with the mock backend, in tst_QVideoWindow)
bool platformPacesToTheDisplay()
{
    return QGuiApplication::platformName() == u"cocoa";
}

// The rates, among those the display shows exactly, to play the 25 fps test
// video at, closest to its own rate first, and of two as close, the faster one:
// at most twice as fast or slow. From the actual refresh rate, so that on a
// 59.94 Hz display they're 29.97 and 19.98, not faster.
QList<qreal> exactRatesToPlayAt(qreal refreshRate)
{
    const int wholeRefreshRate = qRound(refreshRate);
    QList<qreal> rates;
    for (int refreshes = 2; refreshes <= wholeRefreshRate; ++refreshes) {
        const qreal rate = refreshRate / refreshes;
        if (wholeRefreshRate % refreshes == 0 && rate >= 12.5 && rate <= 50)
            rates.append(rate);
    }
    std::stable_sort(rates.begin(), rates.end(),
                     [](qreal a, qreal b) { return qAbs(a - 25) < qAbs(b - 25); });
    return rates;
}

#ifdef QT_BUILD_INTERNAL
// This display may have a fixed refresh rate
struct AssumeVariableRefreshRate
{
    AssumeVariableRefreshRate() { qt_setVideoWindowAssumesVariableRefreshRate(true); }
    ~AssumeVariableRefreshRate() { qt_setVideoWindowAssumesVariableRefreshRate(false); }
};
#endif
} // namespace

void tst_QVideoFrameBackend::videoWindow_preferredFrameRate_isSetDuringPlayback()
{
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects the H.264 profile used by colors.mp4");
#endif
#ifndef QT_BUILD_INTERNAL
    QSKIP("Needs a developer build");
#else
    // Playing a video in a video window sets the window's preferred frame
    // rate from the rate its frames arrive at: the 25 fps video played at the
    // speed that makes it a rate the display shows exactly
    const AssumeVariableRefreshRate assume;
    QVideoWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QList<qreal> rates = exactRatesToPlayAt(window.screen()->refreshRate());
    if (rates.isEmpty())
        QSKIP("No rate this display shows exactly is close enough to 25 fps");
    FrameCounter counter(window);

    QMediaPlayer player;
    player.setVideoOutput(&window);
    player.setSource(*m_colorsVideo);
    player.setPlaybackRate(rates.first() / 25.0);
    player.play();

    QTRY_VERIFY(counter.streamFrameRate > 0);
    // The backend may keep the playback rate as a float
    const qreal expected = platformPacesToTheDisplay()
            ? qVideoPreferredFrameRate(counter.streamFrameRate * player.playbackRate(),
                                       window.screen()->refreshRate())
            : 0.0;
    if (platformPacesToTheDisplay())
        QCOMPARE_GT(expected, 0);
    QTRY_COMPARE(window.preferredFrameRate(), expected);
#endif
}

void tst_QVideoFrameBackend::videoWindow_preferredFrameRate_followsPlaybackRate()
{
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects the H.264 profile used by colors.mp4");
#endif
#ifndef QT_BUILD_INTERNAL
    QSKIP("Needs a developer build");
#else
    // Played faster or slower, the frames arrive faster or slower: asking for
    // the stream's rate would make the window drop frames, or leave some shown
    // longer than others
    const AssumeVariableRefreshRate assume;
    QVideoWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const qreal refreshRate = window.screen()->refreshRate();
    const QList<qreal> rates = exactRatesToPlayAt(refreshRate);
    if (rates.size() < 2)
        QSKIP("Fewer than two rates this display shows exactly are close to 25 fps");
    FrameCounter counter(window);

    QMediaPlayer player;
    player.setVideoOutput(&window);
    player.setSource(*m_colorsVideo);
    player.setLoops(QMediaPlayer::Infinite);
    player.setPlaybackRate(rates.at(0) / 25.0);
    player.play();

    // The backend may keep the playback rate as a float
    const auto expected = [&] {
        return platformPacesToTheDisplay()
                ? qVideoPreferredFrameRate(counter.streamFrameRate * player.playbackRate(),
                                           refreshRate)
                : 0.0;
    };
    QTRY_VERIFY(counter.streamFrameRate > 0);
    QTRY_COMPARE(window.preferredFrameRate(), expected());
    const qreal first = window.preferredFrameRate();
    player.setPlaybackRate(rates.at(1) / 25.0);
    QTRY_COMPARE(window.preferredFrameRate(), expected());
    if (platformPacesToTheDisplay())
        QCOMPARE_NE(window.preferredFrameRate(), first);
#endif
}

void tst_QVideoFrameBackend::videoWindow_preferredFrameRate_isResetWhenPaused()
{
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects the H.264 profile used by colors.mp4");
#endif
#ifndef QT_BUILD_INTERNAL
    QSKIP("Needs a developer build");
#else
    if (!platformPacesToTheDisplay())
        QSKIP("QVideoWindow only sets a preferred frame rate on the cocoa platform");

    // A paused player delivers frames when seeking, which shouldn't wait for
    // the grid of the video's rate
    const AssumeVariableRefreshRate assume;
    QVideoWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const QList<qreal> rates = exactRatesToPlayAt(window.screen()->refreshRate());
    if (rates.isEmpty())
        QSKIP("No rate this display shows exactly is close enough to 25 fps");
    FrameCounter counter(window);

    QMediaPlayer player;
    player.setVideoOutput(&window);
    player.setSource(*m_colorsVideo);
    player.setPlaybackRate(rates.first() / 25.0);
    player.play();
    QTRY_COMPARE_GT(window.preferredFrameRate(), 0);

    player.pause();
    QTRY_COMPARE(player.playbackState(), QMediaPlayer::PausedState);
    const int before = counter.frames;
    player.setPosition(5000);
    QTRY_COMPARE_GT(counter.frames, before);
    QCOMPARE(window.preferredFrameRate(), 0.0);
#endif
}

void tst_QVideoFrameBackend::videoWindow_preferredFrameRate_isOnlySetForVariableRefreshRate()
{
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects the H.264 profile used by colors.mp4");
#endif
    // Without assuming a variable refresh rate, the window follows what
    // QAVFHelpers::hasVariableRefreshRate() reads from AppKit for its display:
    // no preference with a fixed refresh rate, where it would only delay frames
    QVideoWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const qreal refreshRate = window.screen()->refreshRate();
    const QList<qreal> rates = exactRatesToPlayAt(refreshRate);
    if (rates.isEmpty())
        QSKIP("No rate this display shows exactly is close enough to 25 fps");
    FrameCounter counter(window);

    QMediaPlayer player;
    player.setVideoOutput(&window);
    player.setSource(*m_colorsVideo);
    player.setPlaybackRate(rates.first() / 25.0);
    player.play();
    QTRY_VERIFY(counter.frames > 5);

    bool variableRefreshRate = false;
#ifdef Q_OS_MACOS
    variableRefreshRate = QAVFHelpers::hasVariableRefreshRate(window.screen());
#endif
    qInfo() << "display with a variable refresh rate:" << variableRefreshRate;
    const qreal expected = platformPacesToTheDisplay() && variableRefreshRate
            ? qVideoPreferredFrameRate(counter.streamFrameRate * player.playbackRate(), refreshRate)
            : 0.0;
    if (platformPacesToTheDisplay() && variableRefreshRate)
        QCOMPARE_GT(expected, 0);
    QCOMPARE(window.preferredFrameRate(), expected);
}

void tst_QVideoFrameBackend::videoWindow_receivesFrames_whileHidden()
{
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects the H.264 profile used by colors.mp4");
#endif

    // Frames keep coming while the video window is hidden, as they may be
    // used for more than showing them
    QVideoWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    FrameCounter counter(window);

    QMediaPlayer player;
    player.setVideoOutput(&window);
    player.setSource(*m_colorsVideo);
    player.setLoops(QMediaPlayer::Infinite);
    player.play();
    QTRY_VERIFY(counter.frames > 0);

    window.hide();
    QTRY_VERIFY(!window.isExposed());
    QVERIFY2(counter.framesWithin(1000) > 0, "no frames while the window is hidden");
}

void tst_QVideoFrameBackend::videoWindow_receivesFrames_afterMovingToAnotherScreen()
{
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.size() < 2)
        QSKIP("This test needs two screens");

    // Frames keep coming after the video window moves to another screen, where
    // the AVFoundation backend recreates the display link it polls for frames
    // with. A link left on the old screen would still poll, so this catches a
    // broken recreation, not a missing one.
    QVideoWindow window;
    window.setScreen(screens.at(0));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    FrameCounter counter(window);

    QMediaPlayer player;
    player.setVideoOutput(&window);
    player.setSource(*m_colorsVideo);
    player.setLoops(QMediaPlayer::Infinite);
    player.play();
    QTRY_VERIFY(counter.frames > 0);

    window.setGeometry(QRect(screens.at(1)->availableGeometry().topLeft() + QPoint(50, 50),
                             window.size()));
    QTRY_COMPARE(window.screen(), screens.at(1));
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const int frames = counter.framesWithin(1000);
    QVERIFY2(frames >= counter.streamFrameRate * 0.6,
             qPrintable(QStringLiteral("%1 frames in 1 s at %2 fps")
                                .arg(frames).arg(counter.streamFrameRate)));
}

void tst_QVideoFrameBackend::videoWindow_canBeDeleted_whilePlaying()
{
    if (!m_colorsVideo)
        QSKIP("The test video can't be opened, see testMediaFilesAreSupported");
#ifdef Q_OS_HARMONY
    QSKIP("OHOS demuxer rejects the H.264 profile used by colors.mp4");
#endif

    // The backend may keep the window around to follow its screen
    QMediaPlayer player;
    auto window = std::make_unique<QVideoWindow>();
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    FrameCounter counter(*window);
    player.setVideoOutput(window.get());
    player.setSource(*m_colorsVideo);
    player.setLoops(QMediaPlayer::Infinite);
    player.play();
    QTRY_VERIFY(counter.frames > 0);

    window.reset();
    QTest::qWait(500);
    QCOMPARE(player.playbackState(), QMediaPlayer::PlayingState);
}

QTEST_MAIN(tst_QVideoFrameBackend)
#include "tst_qvideoframebackend.moc"
