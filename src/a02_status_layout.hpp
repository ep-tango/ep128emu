#ifndef EP128EMU_A02_STATUS_LAYOUT_HPP
#define EP128EMU_A02_STATUS_LAYOUT_HPP

// Deterministic FLTK-unit layout, independent of Retina backing factor.
// The menu occupies x=0..380; keep a 5-unit gap. At small window widths
// the optional tape description clips, never the P/R state or clock.
// Original disk indicator occupies x=2..32; REM begins x=38 (6 px gap).
namespace Ep128Emu {
  struct A02StatusLayout {
    static int groupWidth(int windowWidth) {
      const int available = windowWidth - 385;
      return (available < 360 ? 360 :
              (available > 472 ? 472 : available));
    }
    static int remCaptionX() { return 38; }
    static int rem1LedX() { return 70; }
    static int rem1NumberX() { return 82; }
    static int rem2LedX() { return 96; }
    static int rem2NumberX() { return 108; }
    static int demoBadgeX() { return 121; }
    static int logoX() { return 134; }
    static int logoWidth() { return 52; }
    static int demoBadgeWidth() { return 12; }
    static int statusLedWidth() { return 11; }
    static int diskEndX() { return 32; }
    static int powerLedX() { return 189; }
    static int tapeInfoX() { return 203; }
    static int counterWidth() { return 100; }
    static int counterX(int groupWidth) { return groupWidth - counterWidth() - 2; }
    static int tapeInfoWidth(int groupWidth) {
      return counterX(groupWidth) - tapeInfoX() - 4;
    }
    static int stateX(int groupWidth) { return counterX(groupWidth) + 4; }
    static int recordStartX() { return 135; }
  };
}
#endif
