#import <Cocoa/Cocoa.h>
#include <FL/Enumerations.H>
#include "macos_about.hpp"

void ep128emuShowMacOSAboutPanel()
{
  @autoreleasepool {
    NSString *creditsText = [NSString stringWithFormat:
        @"Original emulator by Istvan Varga\nCustom macOS enhancements by Attila G.\n\nmacOS ARM64 - ep128emu 2.1 (2026)\nGUI with FLTK %d.%d.%d",
        FL_MAJOR_VERSION, FL_MINOR_VERSION, FL_PATCH_VERSION];
    NSMutableParagraphStyle *paragraphStyle =
        [[[NSMutableParagraphStyle alloc] init] autorelease];
    [paragraphStyle setAlignment:NSTextAlignmentCenter];
    NSAttributedString *credits = [[NSAttributedString alloc]
        initWithString:creditsText
        attributes:[NSDictionary dictionaryWithObject:paragraphStyle
                                                   forKey:NSParagraphStyleAttributeName]];
    NSDictionary *options = [NSDictionary dictionaryWithObjectsAndKeys:
        credits, @"Credits",
        @"2.1", @"ApplicationVersion",
        nil];
    [NSApp orderFrontStandardAboutPanelWithOptions:options];
#if !__has_feature(objc_arc)
    [credits release];
#endif
  }
}
