// Copyright (C) 2016 The Qt Company Ltd and/or its subsidiary(-ies).
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "avfdisplaylink_p.h"

#include <QtCore/qcoreapplication.h>

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
    // deprecated, is no longer needed.
    m_observer = [[DisplayLinkObserver alloc] initWithAVFDisplayLink:this];
    CADisplayLink *_Nonnull dl =
            [NSScreen.mainScreen displayLinkWithTarget:m_observer
                                              selector:@selector(displayLinkNotification:)];
    [m_observer setDisplayLink:dl];
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
