#import <Cocoa/Cocoa.h>
#include <FL/Enumerations.H>
#include "macos_about.hpp"
#include "build_identity.hpp"

void ep128emuShowMacOSAboutPanel()
{
  @autoreleasepool {
    NSString *version = [NSString stringWithUTF8String:EP128EMU_CUSTOM_VERSION];
    NSString *variant = [NSString stringWithUTF8String:EP128EMU_BUILD_PLATFORM];
    NSString *creditsText = [NSString stringWithFormat:
        @"Original emulator by Istvan Varga\nCustom macOS enhancements by Attila G.\n\n%@ - ep128emu %@ (2026)\nGUI with FLTK %d.%d.%d",
        variant, version, FL_MAJOR_VERSION, FL_MINOR_VERSION, FL_PATCH_VERSION];
    NSMutableParagraphStyle *paragraphStyle =
        [[[NSMutableParagraphStyle alloc] init] autorelease];
    [paragraphStyle setAlignment:NSTextAlignmentCenter];
    NSAttributedString *credits = [[NSAttributedString alloc]
        initWithString:creditsText
        attributes:[NSDictionary dictionaryWithObject:paragraphStyle
                                                   forKey:NSParagraphStyleAttributeName]];
    NSDictionary *options = [NSDictionary dictionaryWithObjectsAndKeys:
        credits, @"Credits",
        version, @"ApplicationVersion",
        nil];
    [NSApp orderFrontStandardAboutPanelWithOptions:options];
#if !__has_feature(objc_arc)
    [credits release];
#endif
  }
}
