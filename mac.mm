// Copyright 2022-present Contributors to the jobman project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/mikaelsundell/jobman

#include "mac.h"

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#include <os/log.h>

#include <QGuiApplication>
#include <QScreen>

namespace mac
{
    namespace utils {
        QPointF toNativeCursor(int x, int y)
        {
            QScreen* screen = QGuiApplication::primaryScreen();
            QPointF cursor = QPointF(x, y);
            qreal reverse = screen->geometry().height() - cursor.y();
            return QPointF(cursor.x(), reverse);
        }
    
        NSWindow* toNativeWindow(WId winId)
        {
            NSView *view = (NSView*)winId;
            return [view window];
        }
    
        NSScreen* toNativeScreen(WId winId)
        {
            NSWindow *window = toNativeWindow(winId);
            return [window screen];
        }
    
        struct ColorSyncProfile {
            uint32_t screenNumber;
            CFStringRef displayProfileUrl;
            ColorSyncProfile() : screenNumber(0), displayProfileUrl(nullptr) {}
            ColorSyncProfile(const ColorSyncProfile& other)
            : screenNumber(other.screenNumber) {
                displayProfileUrl = other.displayProfileUrl ? static_cast<CFStringRef>(CFRetain(other.displayProfileUrl)) : nullptr;
            }
            ColorSyncProfile& operator=(const ColorSyncProfile& other) {
                if (this != &other) {
                    screenNumber = other.screenNumber;
                    if (displayProfileUrl) CFRelease(displayProfileUrl);
                    displayProfileUrl = other.displayProfileUrl ? static_cast<CFStringRef>(CFRetain(other.displayProfileUrl)) : nullptr;
                }
                return *this;
            }
            ~ColorSyncProfile() {
                if (displayProfileUrl) CFRelease(displayProfileUrl);
            }
        };
        QMap<uint32_t, ColorSyncProfile> colorsynccache;
        ColorSyncProfile grabColorSyncProfile(NSScreen* screen)
        {
            ColorSyncProfile colorSyncProfile;
            NSDictionary* screenDescription = [screen deviceDescription];
            NSNumber* screenID = [screenDescription objectForKey:@"NSScreenNumber"];
            colorSyncProfile.screenNumber = [screenID unsignedIntValue];
            ColorSyncProfileRef csProfileRef = ColorSyncProfileCreateWithDisplayID((CGDirectDisplayID)colorSyncProfile.screenNumber);
            if (csProfileRef) {
                CFURLRef iccURLRef = ColorSyncProfileGetURL(csProfileRef, NULL);
                if (iccURLRef) {
                    colorSyncProfile.displayProfileUrl = CFURLCopyFileSystemPath(iccURLRef, kCFURLPOSIXPathStyle);
                }
                CFRelease(csProfileRef);
            }
            return colorSyncProfile;
        }
    
        ColorSyncProfile grabDisplayProfile(NSScreen* screen) {
            NSDictionary* screenDescription = [screen deviceDescription];
            CGDirectDisplayID displayId = [[screenDescription objectForKey:@"NSScreenNumber"] unsignedIntValue];
            if (colorsynccache.contains(displayId)) {
                return colorsynccache.value(displayId);
            }
            ColorSyncProfile colorSyncProfile = grabColorSyncProfile(screen);
            colorsynccache.insert(displayId, colorSyncProfile);
            return colorSyncProfile;
        }
    }

    void setDarkAppearance()
    {
        // we force dark aque no matter appearance set in system settings
        [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
    }

    IccProfile grabIccProfile(WId wid)
    {
        NSScreen* screen = utils::toNativeScreen(wid);
        utils::ColorSyncProfile colorsyncProfile = utils::grabDisplayProfile(screen);
        return IccProfile {
            int(colorsyncProfile.screenNumber),
            QString::fromCFString(colorsyncProfile.displayProfileUrl)
        };
    }

    QString grabIccProfileUrl(WId wid)
    {
        return grabIccProfile(wid).displayProfileUrl;
    }

    void console(const QString& log)
    {
        NSLog(@"%@", log.toNSString());
    }
}
