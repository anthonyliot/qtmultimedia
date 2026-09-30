// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QtTest/qtest.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qscreen.h>
#include <QtGui/qsurfaceformat.h>
#include <QtMultimedia/qmediaplayer.h>
#include <QtMultimedia/qvideoframe.h>
#include <QtMultimedia/qvideoframeformat.h>
#include <QtMultimedia/qvideosink.h>
#include <QtMultimedia/private/qvideowindow_p.h>

#ifdef Q_OS_MACOS
#  include <QtMultimedia/private/qavfhelpers_p.h>
#endif

#include <qmockintegration.h>
#include <qmockmediaplayer.h>

#include <memory>

QT_USE_NAMESPACE

Q_ENABLE_MOCK_MULTIMEDIA_PLUGIN

namespace {
// Where QVideoWindow sets a preferred frame rate, if the display has a variable
// refresh rate: where update requests are paced to the display
bool platformPacesToTheDisplay()
{
    return QGuiApplication::platformName() == u"cocoa";
}

// A rate that the display shows with an even cadence, whatever its refresh
// rate: the refresh rate divided by the smallest whole number above 1 that
// divides it (every other refresh at 60 Hz, every third one at 75 Hz)
qreal exactFrameRate(qreal refreshRate)
{
    const int wholeRefreshRate = qRound(refreshRate);
    int refreshes = 2;
    while (refreshes < wholeRefreshRate && wholeRefreshRate % refreshes != 0)
        ++refreshes;
    return refreshRate / refreshes;
}

QVideoFrame frameAt(qreal rate)
{
    QVideoFrameFormat format(QSize(10, 20), QVideoFrameFormat::Format_ARGB8888);
    format.setStreamFrameRate(rate);
    return QVideoFrame(format);
}
} // namespace

class tst_QVideoWindow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void preferredFrameRate_isSetForRatesTheDisplayShowsExactly();
    void preferredFrameRate_isResetWhenItNoLongerApplies();
    void preferredFrameRate_isNotSetWhilePaused();
    void preferredFrameRate_isNotSetWithoutMediaPlayer();
    void preferredFrameRate_leavesAnotherPreferenceAlone();

private:
    // The window shows the frames of a playing (mock) media player, and assumes
    // a variable refresh rate, as this display may have a fixed one
    std::unique_ptr<QVideoWindow> m_window;
    std::unique_ptr<QMediaPlayer> m_player;
    qreal m_exact = 0; // QVideoFrameFormat keeps the rate as a float
};

void tst_QVideoWindow::init()
{
#ifndef QT_BUILD_INTERNAL
    QSKIP("Needs a developer build");
#else
    qt_setVideoWindowAssumesVariableRefreshRate(true);
    m_window = std::make_unique<QVideoWindow>();
    m_player = std::make_unique<QMediaPlayer>();
    m_player->setVideoOutput(m_window.get());
    QMockIntegration::instance()->lastPlayer()->setIsValid(true);
    m_player->setSource(QUrl(QStringLiteral("file:///video.mp4")));
    m_player->play();
    QCOMPARE(m_player->playbackState(), QMediaPlayer::PlayingState);
    QCOMPARE(m_window->preferredFrameRate(), 0.0);
    m_exact = float(exactFrameRate(m_window->screen()->refreshRate()));
#endif
}

void tst_QVideoWindow::cleanup()
{
#ifdef QT_BUILD_INTERNAL
    qt_setVideoWindowAssumesVariableRefreshRate(false);
#endif
    m_player.reset();
    m_window.reset();
}

void tst_QVideoWindow::preferredFrameRate_isSetForRatesTheDisplayShowsExactly()
{
    // The video window asks for the rate of a playing media player's frames
    // when the display shows it with an even cadence, so that the display can
    // refresh at that rate
    const qreal expected = platformPacesToTheDisplay() ? m_exact : 0.0;
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), expected);

    // 2.5 refreshes per frame would show some frames longer than others
    m_window->videoSink()->setVideoFrame(frameAt(m_window->screen()->refreshRate() / 2.5));
    QCOMPARE(m_window->preferredFrameRate(), 0.0);

    // The frames arrive at the stream's rate times the playback rate
    m_player->setPlaybackRate(2);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact / 2));
    QCOMPARE(m_window->preferredFrameRate(), expected);
    m_player->setPlaybackRate(1);

    // Unknown rate, and no frame: no preference
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    m_window->videoSink()->setVideoFrame(frameAt(0));
    QCOMPARE(m_window->preferredFrameRate(), 0.0);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    m_window->videoSink()->setVideoFrame(QVideoFrame());
    QCOMPARE(m_window->preferredFrameRate(), 0.0);
}

void tst_QVideoWindow::preferredFrameRate_isResetWhenItNoLongerApplies()
{
    if (!platformPacesToTheDisplay())
        QSKIP("QVideoWindow only sets a preferred frame rate on the cocoa platform");

    const auto setIt = [this] {
        m_window->videoSink()->setVideoFrame(frameAt(m_exact));
        QCOMPARE(m_window->preferredFrameRate(), m_exact);
    };

    // The display has a fixed refresh rate, e.g. after the window moved to
    // another display, unless this one has a variable refresh rate itself
    bool variableRefreshRate = false;
#ifdef Q_OS_MACOS
    variableRefreshRate = QAVFHelpers::hasVariableRefreshRate(m_window->screen());
#endif
    if (!variableRefreshRate) {
        setIt();
        qt_setVideoWindowAssumesVariableRefreshRate(false);
        m_window->videoSink()->setVideoFrame(frameAt(m_exact));
        QCOMPARE(m_window->preferredFrameRate(), 0.0);
        qt_setVideoWindowAssumesVariableRefreshRate(true);
    }

    // Without vertical sync, update requests are timer based
    setIt();
    QSurfaceFormat format = m_window->requestedFormat();
    format.setSwapInterval(0);
    m_window->setFormat(format);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), 0.0);
    format.setSwapInterval(1);
    m_window->setFormat(format);

    // The media player is gone
    setIt();
    m_player.reset();
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), 0.0);
}

void tst_QVideoWindow::preferredFrameRate_isNotSetWhilePaused()
{
    if (!platformPacesToTheDisplay())
        QSKIP("QVideoWindow only sets a preferred frame rate on the cocoa platform");

    // A paused player delivers frames when seeking, which shouldn't wait for
    // the grid of the video's rate
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), m_exact);
    m_player->pause();
    QCOMPARE(m_player->playbackState(), QMediaPlayer::PausedState);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), 0.0);
    m_player->play();
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), m_exact);
}

void tst_QVideoWindow::preferredFrameRate_isNotSetWithoutMediaPlayer()
{
    // Other sources, like cameras, may not deliver at the rate they report
    QVideoWindow window;
    window.videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(window.preferredFrameRate(), 0.0);
}

void tst_QVideoWindow::preferredFrameRate_leavesAnotherPreferenceAlone()
{
    // The window only sets and resets its preferred frame rate while that's the
    // one it set itself, or none. 12.3 is never an exact rate here.
    const qreal other = 12.3;
    const qreal expected = platformPacesToTheDisplay() ? m_exact : 0.0;

    // Set before the window's own
    m_window->setPreferredFrameRate(other);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), other);

    // Set after it: neither replaced while it would apply, nor reset after
    m_window->setPreferredFrameRate(0);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), expected);
    m_window->setPreferredFrameRate(other);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), other);
    m_player->pause();
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), other);

    // Someone else then sets exactly the window's former value: it's theirs now
    m_window->setPreferredFrameRate(m_exact);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), m_exact);

    // Reset by the one who set it: the window sets its own again
    m_player->play();
    m_window->setPreferredFrameRate(0);
    m_window->videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(m_window->preferredFrameRate(), expected);

    // Without a media player
    QVideoWindow window;
    window.setPreferredFrameRate(other);
    window.videoSink()->setVideoFrame(frameAt(m_exact));
    QCOMPARE(window.preferredFrameRate(), other);
}

QTEST_MAIN(tst_QVideoWindow)

#include "tst_qvideowindow.moc"
