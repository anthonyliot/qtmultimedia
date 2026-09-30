// Copyright (C) 2016 The Qt Company Ltd and/or its subsidiary(-ies).
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "avfdisplaylink_p.h"

#include <QtCore/qcoreapplication.h>
#include <QtGui/qscreen.h>
#include <QtGui/qscreen_platform.h>

#ifdef QT_DEBUG_AVF
#include <QtCore/qdebug.h>
#endif



#import <Foundation/NSRunLoop.h>
#if !defined(QT_PLATFORM_UIKIT)
#import <AppKit/NSScreen.h>
#endif

QT_USE_NAMESPACE

@interface QT_MANGLE_NAMESPACE (DisplayLinkObserver) : NSObject
{
    AVFDisplayLink *_Nonnull m_avfDisplayLink;
    CADisplayLink *m_displayLink;
}
- (id)initWithAVFDisplayLink:(AVFDisplayLink *_Nonnull)link;
- (void)setDisplayLink:(CADisplayLink *)displayLink;
- (void)start;
- (void)stop;
- (void)displayLinkNotification:(CADisplayLink *)sender;
@end

@implementation QT_MANGLE_NAMESPACE (DisplayLinkObserver)

- (id)initWithAVFDisplayLink:(AVFDisplayLink *_Nonnull)link
{
    self = [super init];
    if (self)
        m_avfDisplayLink = link;
    return self;
}

- (void)dealloc
{
    [self setDisplayLink:nullptr];
    [super dealloc];
}

- (void)setDisplayLink:(CADisplayLink *)displayLink
{
    if (m_displayLink == displayLink)
        return;
    if (m_displayLink) {
        [m_displayLink invalidate];
        [m_displayLink release];
    }
    m_displayLink = [displayLink retain];
}

- (void)start
{
    // In the common modes, so that the video keeps playing while the run loop
    // tracks a menu or a live resize, or runs a modal session. Qt delivers the
    // event that displayLinkNotification: posts in those modes as well.
    [m_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stop
{
    [m_displayLink removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)displayLinkNotification:(CADisplayLink *)sender
{
    Q_UNUSED(sender);
    m_avfDisplayLink->displayLinkEvent();
}

@end

#ifdef QT_NAMESPACE
using DisplayLinkObserver = QT_MANGLE_NAMESPACE(DisplayLinkObserver);
#endif

AVFDisplayLink::AVFDisplayLink(QObject *parent)
    : QObject(parent)
{
#if defined(QT_PLATFORM_UIKIT)
    m_observer = [[DisplayLinkObserver alloc] initWithAVFDisplayLink:this];
    CADisplayLink *dl = [CADisplayLink displayLinkWithTarget:m_observer
                                                    selector:@selector(displayLinkNotification:)];
    [m_observer setDisplayLink:dl];
#else
    // -[NSScreen displayLinkWithTarget:selector:] is available from macOS 14.0,
    // and our minimum deployment target is 14.4, so CVDisplayLink, which is
    // deprecated, is no longer needed. Until we know the window the video is
    // shown in, see setWindow(), use the main screen.
    m_observer = [[DisplayLinkObserver alloc] initWithAVFDisplayLink:this];
    recreateDisplayLink();
#endif
}

void AVFDisplayLink::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        disconnect(m_window, nullptr, this, nullptr);
    m_window = window;
    if (m_window)
        connect(m_window, &QWindow::screenChanged, this, &AVFDisplayLink::recreateDisplayLink);
#if !defined(QT_PLATFORM_UIKIT)
    recreateDisplayLink();
#endif
}

// Creates the display link of the screen the video is shown on, and follows the
// window to other screens. Like the display link of the main screen used
// before, it keeps running while the window is hidden, as the frames may be
// used for more than showing them. Without a window, or on platforms other
// than cocoa, it uses the main screen at the time.
void AVFDisplayLink::recreateDisplayLink()
{
#if !defined(QT_PLATFORM_UIKIT)
    NSScreen *screen = nil;
    if (QScreen *windowScreen = m_window ? m_window->screen() : nullptr) {
        if (auto *cocoaScreen = windowScreen->nativeInterface<QNativeInterface::QCocoaScreen>())
            screen = cocoaScreen->nativeScreen();
    }
    if (!screen)
        screen = NSScreen.mainScreen;

    const bool wasActive = m_isActive;
    if (wasActive)
        stop();
    // Invalidates the previous one. nil if there's no screen at all.
    [m_observer setDisplayLink:[screen displayLinkWithTarget:m_observer
                                                    selector:@selector(displayLinkNotification:)]];
    if (wasActive)
        start();
#endif
}

AVFDisplayLink::~AVFDisplayLink()
{
#ifdef QT_DEBUG_AVF
    qDebug() << Q_FUNC_INFO;
#endif

    stop();

    if (m_observer) {
        // The display link retains its target, the observer, until it's
        // invalidated, so releasing the observer alone would leak both
        [m_observer setDisplayLink:nil];
        [m_observer release];
        m_observer = nil;
    }
}

bool AVFDisplayLink::isValid() const
{
    return m_observer;
}

bool AVFDisplayLink::isActive() const
{
    return m_isActive;
}

void AVFDisplayLink::start()
{
    if (!m_isActive) {
        if (m_observer)
            [m_observer start];
        m_isActive = true;
    }
}

void AVFDisplayLink::stop()
{
    if (m_isActive) {
        if (m_observer)
            [m_observer stop];
        m_framePending = false;
        m_isActive = false;
    }
}

void AVFDisplayLink::displayLinkEvent()
{
    if (!m_framePending.exchange(true))
        qApp->postEvent(this, new QEvent(QEvent::User), Qt::HighEventPriority);
}

bool AVFDisplayLink::event(QEvent *event)
{
    switch (event->type()){
    case QEvent::User: {
        if (!m_framePending.exchange(false))
            return false;

        Q_EMIT tick();

        return false;
    }
    default:
        break;
    }
    return QObject::event(event);
}

#include "moc_avfdisplaylink_p.cpp"
