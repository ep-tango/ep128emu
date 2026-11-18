// Popup-free NICK LPT byte and FixBias selectors for macOS FLTK 1.4.5.
// Both Fl_Choice menus caused SIGSEGV in Fl_Menu_Item::pulldown() on 2026-11-18.
#ifndef EP128EMU_LPT_ARROW_SELECTOR_HPP
#define EP128EMU_LPT_ARROW_SELECTOR_HPP
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Enumerations.H>
#include <cstdio>

namespace Ep128Emu {
class LPTArrowSelector : public Fl_Group {
  Fl_Button *left_;
  Fl_Button *right_;
  Fl_Box *readout_;
  int current_;
  int maximum_;
  bool byteNames_;

  static void arrowCallback(Fl_Widget *w, void *data) {
    LPTArrowSelector *self = static_cast<LPTArrowSelector *>(data);
    const int delta = (w == self->left_ ? -1 : 1);
    if (self->value(self->current_ + delta)) self->do_callback();
  }
  void updateReadout() {
    static const char *const names[16] = {
      "Lines", "Mode IRQ RLD", "LM ALT bits", "RM ALT bits",
      "LD1 low", "LD1 high", "LD2 low", "LD2 high",
      "Palette 0", "Palette 1", "Palette 2", "Palette 3",
      "Palette 4", "Palette 5", "Palette 6", "Palette 7"
    };
    char text[64];
    if (current_ < 0) std::snprintf(text, sizeof(text), "--");
    else if (byteNames_)
      std::snprintf(text, sizeof(text), "%02X %s", current_, names[current_]);
    else std::snprintf(text, sizeof(text), "%02X", current_);
    readout_->copy_label(text);
  }
public:
  // initial=-1 is an intentionally unset FixBias; Refresh reads real port 80h.
  LPTArrowSelector(int x, int y, int w, int h,
                   int maximum, int initial, bool byteNames)
    : Fl_Group(x, y, w, h), left_(0), right_(0), readout_(0),
      current_(initial), maximum_(maximum), byteNames_(byteNames) {
    const int arrowWidth = 26;  // FL_SIMPLE_COUNTER clamps at only 13 px.
    begin();
    left_ = new Fl_Button(x, y, arrowWidth, h, "<");
    readout_ = new Fl_Box(x + arrowWidth, y, w - 2 * arrowWidth, h);
    right_ = new Fl_Button(x + w - arrowWidth, y, arrowWidth, h, ">");
    left_->labelsize(18);
    right_->labelsize(18);
    readout_->box(FL_DOWN_BOX);
    readout_->labelfont(FL_COURIER);
    readout_->labelsize(12);
    readout_->align(FL_ALIGN_INSIDE | FL_ALIGN_CENTER);
    left_->callback(&arrowCallback, this);
    right_->callback(&arrowCallback, this);
    end();
    updateReadout();
  }
  int value() const { return current_; }
  bool value(int n) {
    if (n < 0 || n > maximum_ || n == current_) return false;
    current_ = n;
    updateReadout();
    return true; // programmatic selection must never call parent callback
  }
  static int byteOffset(int n) { return n >= 0 && n < 16 ? n : -1; }
  Fl_Button *leftButton() const { return left_; }   // native callback QA
  Fl_Button *rightButton() const { return right_; } // native callback QA
  const char *displayText() const { return readout_->label(); }
  int arrowWidth() const { return left_->w(); }
};
} // namespace Ep128Emu
#endif
