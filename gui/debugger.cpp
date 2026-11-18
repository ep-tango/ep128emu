
// ep128emu -- portable Enterprise 128 emulator
// Copyright (C) 2003-2010 Istvan Varga <istvanv@users.sourceforge.net>
// http://sourceforge.net/projects/ep128emu/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include "ep128emu.hpp"
#include "gui.hpp"
#include "debuglib.hpp"
#include "debugger.hpp"
#include "ep128vm.hpp"
#include "nick_lpt_viewer.hpp"
#include "lpt_arrow_selector.hpp"
#include <FL/Fl_Browser.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Enumerations.H>
#include <cstring>
#include <cstdio>

Ep128EmuGUI_LuaScript::~Ep128EmuGUI_LuaScript()
{
}

void Ep128EmuGUI_LuaScript::errorCallback(const char *msg)
{
  if (msg == (char *) 0 || msg[0] == '\0')
    msg = "Lua script error";
  debugWindow.gui.errorMessage(msg);
}

void Ep128EmuGUI_LuaScript::messageCallback(const char *msg)
{
  if (!msg)
    msg = "";
  debugWindow.monitor_->printMessage(msg);
}

Ep128EmuGUI_ScrollableOutput::~Ep128EmuGUI_ScrollableOutput()
{
}

int Ep128EmuGUI_ScrollableOutput::handle(int evt)
{
  if (evt == FL_MOUSEWHEEL) {
    int     tmp = Fl::event_dy();
    if (tmp > 0) {
      if (downWidget)
        downWidget->do_callback();
    }
    else if (tmp < 0) {
      if (upWidget)
        upWidget->do_callback();
    }
    return 1;
  }
  return Fl_Multiline_Output::handle(evt);
}

// Read-only LPT viewer; runtime parser lives in src/nick_lpt_viewer.hpp.
// This window belongs to the existing debugger and is destroyed with it.
void Ep128EmuGUI_DebugWindow::lptRefreshCallback(Fl_Widget *, void *userData)
{
  Ep128EmuGUI_DebugWindow *p =
      reinterpret_cast<Ep128EmuGUI_DebugWindow *>(userData);
  p->refreshLPTViewer();
}

void Ep128EmuGUI_DebugWindow::lptCloseCallback(Fl_Widget *, void *userData)
{
  Ep128EmuGUI_DebugWindow *p =
      reinterpret_cast<Ep128EmuGUI_DebugWindow *>(userData);
  if (p->lptWindow)
    p->lptWindow->hide();
}

void Ep128EmuGUI_DebugWindow::lptEditSelectionCallback(
    Fl_Widget *, void *userData)
{
  reinterpret_cast<Ep128EmuGUI_DebugWindow *>(userData)->updateLPTEditSelection();
}

void Ep128EmuGUI_DebugWindow::lptEditToggleCallback(
    Fl_Widget *, void *userData)
{
  reinterpret_cast<Ep128EmuGUI_DebugWindow *>(userData)->updateLPTEditSelection();
}

void Ep128EmuGUI_DebugWindow::lptEditApplyCallback(
    Fl_Widget *, void *userData)
{
  reinterpret_cast<Ep128EmuGUI_DebugWindow *>(userData)->applyLPTEdit();
}

void Ep128EmuGUI_DebugWindow::updateLPTEditSelection()
{
  if (!lptEditValue || !lptApplyButton || !lptEditByte || !lptTable ||
      !lptEditToggle) return;
  const int row = lptTable->value() - 2; // browser line 1 is the header
  const int offset = Ep128Emu::LPTArrowSelector::byteOffset(
      static_cast<Ep128Emu::LPTArrowSelector *>(lptEditByte)->value());
  const bool haveByte = (row >= 0 && unsigned(row) < lptRowCount &&
                         offset >= 0 && offset < 16);
  char text[4] = "";
  if (haveByte) {
    std::snprintf(text, sizeof(text), "%02X", unsigned(lptRaw[row][offset]));
  }
  lptEditValue->value(text);
  // Read-only by default; Apply requires an explicitly armed checkbox AND
  // a paused debugger with ownership of the VM thread lock.
  const bool canEdit = bool(lptEditToggle->value()) && haveByte &&
                       active() && gui.debugWindowOpenFlag;
  if (canEdit) {
    lptEditValue->activate();
    lptApplyButton->activate();
  }
  else {
    lptEditValue->deactivate();
    lptApplyButton->deactivate();
  }
}

void Ep128EmuGUI_DebugWindow::applyLPTEdit()
{
  if (!lptEditToggle || !lptEditToggle->value() || !lptEditValue ||
      !active() || !gui.debugWindowOpenFlag || !lptTable || !lptEditByte)
    return;
  const int row = lptTable->value() - 2;
  const int offset = Ep128Emu::LPTArrowSelector::byteOffset(
      static_cast<Ep128Emu::LPTArrowSelector *>(lptEditByte)->value());
  if (row < 0 || unsigned(row) >= lptRowCount || offset < 0 || offset >= 16)
    return;
  if (!dynamic_cast<Ep128::Ep128VM *>(&(gui.vm))) {
    gui.errorMessage("LPT editing is available in Enterprise mode only");
    return;
  }
  uint8_t value = 0;
  if (!Ep128Emu::nickLPTParseHexByte(lptEditValue->value(), value)) {
    gui.errorMessage("LPT byte must be exactly two hexadecimal digits (00..FF)");
    return;
  }
  const uint16_t entryAddress = lptAddresses[row];
  const uint16_t address = Ep128Emu::nickLPTByteAddress(entryAddress,
                                                       unsigned(offset));
  const uint8_t expected = lptRaw[row][offset];
  if (value == expected) return; // no-op never mutates the guest
  if (gui.vmThread.lock(100) != 0) {
    gui.errorMessage("LPT edit refused: emulator thread is busy");
    return;
  }
  const uint16_t base = Ep128Emu::nickLPTBase(gui.vm.readIOPort(0x0082),
                                                   gui.vm.readIOPort(0x0083));
  const uint32_t physical = Ep128Emu::nickLPTPhysicalAddress(address);
  const uint8_t current = gui.vm.readMemory(physical);
  if (!Ep128Emu::nickLPTEditPrecondition(lptViewBase, base, expected, current)) {
    gui.vmThread.unlock();
    gui.errorMessage("LPT was changed by the program. Refresh before editing.");
    refreshLPTViewer();
    return;
  }
  // Exactly one deliberately selected byte in the FC..FF video RAM.
  // This is not a port write, ROM patch, or change to the emulated NICK core.
  gui.vm.writeMemory(physical, value, false);
  const bool success = gui.vm.readMemory(physical) == value;
  gui.vmThread.unlock();
  if (!success) {
    gui.errorMessage("LPT write verification failed");
    return;
  }
  refreshLPTViewer(); // verifies and redraws actual RAM; may change LPB count
}

void Ep128EmuGUI_DebugWindow::showLPTViewer()
{
  if (!dynamic_cast<Ep128::Ep128VM *>(&(gui.vm))) {
    gui.errorMessage("NICK LPT viewer is available in Enterprise mode only");
    return;
  }
  if (!lptWindow) {
    lptWindow = new Fl_Double_Window(1120, 680, "NICK Line Parameter Table Viewer");
    lptWindow->user_data((void *) this);
    lptWindow->callback(&lptCloseCallback, (void *) this);
    lptWindow->begin();
    Fl_Box *biasLabel = new Fl_Box(15, 10, 56, 26, "FixBias:");
    biasLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    // No FLTK popup: the 18:48 FixBias crash used the same pulldown path.
    lptBias = new Ep128Emu::LPTArrowSelector(76, 10, 100, 26, 31, -1, false);
    lptBias->callback(&lptRefreshCallback, (void *) this);
    Fl_Button *refresh = new Fl_Button(194, 10, 92, 26, "Refresh");
    refresh->callback(&lptRefreshCallback, (void *) this);
    lptEditToggle = new Fl_Button(300, 10, 125, 26, "Edit LPT");
    lptEditToggle->type(FL_TOGGLE_BUTTON);
    lptEditToggle->value(0); // never silently arm a guest RAM writer
    lptEditToggle->tooltip("Allow single-byte LPT edits while debugger is paused; changes real NICK video RAM");
    lptEditToggle->callback(&lptEditToggleCallback, (void *) this);
    Fl_Box *editLabel = new Fl_Box(15, 44, 60, 26, "Byte:");
    editLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    // Two-arrow hex selector. The FLTK popup used here previously crashed
    // the macOS main thread in Fl_Menu_Item::pulldown(). No popup or tooltip.
    lptEditByte = new Ep128Emu::LPTArrowSelector(76, 44, 225, 26, 15, 8, true);
    lptEditByte->callback(&lptEditSelectionCallback, (void *) this);
    Fl_Box *valueLabel = new Fl_Box(319, 44, 56, 26, "Hex:");
    valueLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    lptEditValue = new Fl_Input(375, 44, 62, 26);
    lptEditValue->textfont(FL_COURIER);
    lptEditValue->maximum_size(2);
    lptEditValue->deactivate();
    lptApplyButton = new Fl_Button(452, 44, 90, 26, "Apply");
    lptApplyButton->callback(&lptEditApplyCallback, (void *) this);
    lptApplyButton->deactivate();
    Fl_Box *writeHint = new Fl_Box(555, 44, 545, 26,
                               "Edit LPT + Apply writes RAM. ALT: LM b6/7 LSB/MSB; RM b7/6 IND0/1.");
    writeHint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    writeHint->labelsize(12);
    lptInfo = new Fl_Box(15, 78, 1090, 53);
    lptInfo->box(FL_DOWN_BOX);
    lptInfo->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    lptInfo->labelsize(12);
    lptTable = new Fl_Browser(15, 138, 1090, 525);
    lptTable->type(FL_HOLD_BROWSER);
    lptTable->textfont(FL_COURIER);
    lptTable->textsize(12);
    lptTable->column_char('\t');
    lptTable->callback(&lptEditSelectionCallback, (void *) this);
    // ALT help is displayed persistently, without a flickering hover tooltip.
    // 12 decoded fields + 8 individually RGB-colored NICK palette cells.
    static const int colWidths[] = {
      62, 44, 78, 52, 31, 38, 35, 35, 35, 52, 52, 45,
      55, 55, 55, 55, 55, 55, 55, 55, 0
    };
    lptTable->column_widths(colWidths);
    lptWindow->resizable(lptTable);
    lptWindow->end();
    lptWindow->size_range(1060, 460, 1600, 1100);
  }
  lptEditToggle->value(0); // always re-open in safe read-only mode
  refreshLPTViewer();
  lptWindow->show();
}

void Ep128EmuGUI_DebugWindow::refreshLPTViewer()
{
  if (!lptWindow || !lptTable || !lptBias || !lptInfo)
    return;
  if (!dynamic_cast<Ep128::Ep128VM *>(&(gui.vm))) {
    lptRowCount = 0;
    lptTable->clear();
    lptInfo->copy_label("LPT viewer is available in Enterprise mode only");
    updateLPTEditSelection();
    return;
  }
  // The debugger normally holds the VM lock; VMThread::lock is safely
  // nested (lockCnt), and also works if a Refresh happens while continuing.
  // All physical VRAM/port reads take place within a single VM snapshot.
  if (gui.vmThread.lock(100) != 0) {
    lptInfo->copy_label("Cannot read LPT: virtual machine is busy");
    return;
  }
  const int priorLine = lptTable->value() - 2;
  const bool restoreSelection = priorLine >= 0 && unsigned(priorLine) < lptRowCount;
  const uint16_t selectedAddress = (restoreSelection ? lptAddresses[priorLine] : 0U);
  Ep128Emu::NickLPTEntry entries[512];
  unsigned int count = 0;
  const uint8_t port80 = gui.vm.readIOPort(0x0080);
  const uint8_t port82 = gui.vm.readIOPort(0x0082);
  const uint8_t port83 = gui.vm.readIOPort(0x0083);
  const uint16_t base = Ep128Emu::nickLPTBase(port82, port83);
  bool reload = false;
  for (; count < 512U; ++count) {
    const uint16_t address = uint16_t(base + uint16_t(count * 16U));
    uint8_t raw[16];
    for (unsigned int j = 0; j < 16U; ++j) {
      const uint16_t a = uint16_t(address + uint16_t(j));
      raw[j] = gui.vm.readMemory(Ep128Emu::nickLPTPhysicalAddress(a));
    }
    lptAddresses[count] = address;
    for (unsigned int j = 0; j < 16U; ++j) lptRaw[count][j] = raw[j];
    entries[count] = Ep128Emu::decodeNickLPTEntry(address, raw);
    if (entries[count].reload) {
      reload = true;
      ++count;
      break;
    }
  }
  gui.vmThread.unlock();
  lptRowCount = count;
  lptViewBase = base;
  Ep128Emu::LPTArrowSelector *biasSelector =
      static_cast<Ep128Emu::LPTArrowSelector *>(lptBias);
  if (biasSelector->value() < 0)
    biasSelector->value(int(port80 & 0x1F));
  const unsigned int bias = unsigned(biasSelector->value());
  lptTable->clear();
  lptTable->add("ADDR\tLINES\tMODE\tCOLORS\tIRQ\tVRES\tRLD\tLM\tRM\tLD1\tLD2\tALT\t@c@.P0\t@c@.P1\t@c@.P2\t@c@.P3\t@c@.P4\t@c@.P5\t@c@.P6\t@c@.P7\t ");
  for (unsigned int i = 0; i < count; ++i) {
    const Ep128Emu::NickLPTEntry& e = entries[i];
    char startLine[260];
    std::snprintf(startLine, sizeof(startLine),
                  "%04X\t%3u\t%s\t%3u\t%c\t%c\t%c\t%02X\t%02X\t%04X\t%04X\t%c%c%c%c",
                  unsigned(e.address), unsigned(e.lines),
                  Ep128Emu::nickLPTVideoModeName(e.videoMode),
                  Ep128Emu::nickLPTColorCount(e.colorMode),
                  (e.irq ? 'Y' : '-'), (e.vres ? 'Y' : '-'),
                  (e.reload ? 'Y' : '-'), unsigned(e.leftMargin),
                  unsigned(e.rightMargin), unsigned(e.ld1), unsigned(e.ld2),
                  (e.lsbAlt ? 'L' : '-'), (e.msbAlt ? 'M' : '-'),
                  (e.altInd0 ? '0' : '-'), (e.altInd1 ? '1' : '-'));
    std::string line(startLine);
    for (unsigned int j = 0; j < 8U; ++j) {
      uint8_t r, g, b;
      Ep128Emu::nickLPTColorRGB(e.palette[j], r, g, b);
      // FLTK Fl_Browser supports full 32-bit Fl_Color @B/@C formats.
      // Use the EXACT NICK RGB as swatch background, while keeping the
      // hexadecimal byte visible in high-contrast black or white.
      const Fl_Color ink = (Ep128Emu::nickLPTNeedsLightText(r, g, b)
                            ? FL_WHITE : FL_BLACK);
      const Fl_Color swatch = fl_rgb_color(r, g, b);
      char cell[80];
      std::snprintf(cell, sizeof(cell), "\t@B%lu@C%lu@c@.%02X",
                    (unsigned long) swatch, (unsigned long) ink,
                    unsigned(e.palette[j]));
      line += cell;
    }
    // A trailing, unformatted spacer field is essential: Fl_Browser gives
    // its last field all remaining viewport width. Without this delimiter,
    // P7 expands across the rest of the table even with a 55px column.
    line += "\t ";
    lptTable->add(line.c_str());
  }
  unsigned int selectedRow = 0U;
  if (restoreSelection) {
    for (unsigned int i = 0; i < count; ++i)
      if (lptAddresses[i] == selectedAddress) { selectedRow = i; break; }
  }
  if (count != 0U) lptTable->value(int(selectedRow) + 2);
  updateLPTEditSelection();
  char summary[320];
  std::snprintf(summary, sizeof(summary),
                "NICK base=$%04X  (80h=%02X 82h=%02X 83h=%02X)  %u LPBs%s\n"
                "FixBias %02X: colors 8..F = %02X %02X %02X %02X %02X %02X %02X %02X  (preview only; Edit LPT writes RAM)",
                unsigned(base), unsigned(port80), unsigned(port82),
                unsigned(port83), count, (reload ? ", RELOAD" : ", capped at 512"),
                bias, Ep128Emu::nickLPTFixedPalette(uint8_t(bias), 0),
                Ep128Emu::nickLPTFixedPalette(uint8_t(bias), 1),
                Ep128Emu::nickLPTFixedPalette(uint8_t(bias), 2),
                Ep128Emu::nickLPTFixedPalette(uint8_t(bias), 3),
                Ep128Emu::nickLPTFixedPalette(uint8_t(bias), 4),
                Ep128Emu::nickLPTFixedPalette(uint8_t(bias), 5),
                Ep128Emu::nickLPTFixedPalette(uint8_t(bias), 6),
                Ep128Emu::nickLPTFixedPalette(uint8_t(bias), 7));
  lptInfo->copy_label(summary);
  lptWindow->redraw();
}

// ----------------------------------------------------------------------------

Ep128EmuGUI_DebugWindow::Ep128EmuGUI_DebugWindow(Ep128EmuGUI& gui_)
  : gui(gui_),
    luaScript(*this, gui_.vm)
{
  for (size_t i = 0; i < sizeof(windowTitle); i++)
    windowTitle[i] = '\0';
  std::strcpy(&(windowTitle[0]), "ep128emu debugger");
  savedWindowPositionX = 32;
  savedWindowPositionY = 32;
  focusWidget = (Fl_Widget *) 0;
  prvTab = (Fl_Widget *) 0;
  memoryDumpStartAddress = 0x00000000U;
  memoryDumpViewAddress = 0x00000000U;
  memoryDumpAddressMode = 1;
  ixViewOffset = 0;
  iyViewOffset = 0;
  disassemblyStartAddress = 0x00000000U;
  disassemblyViewAddress = 0x00000000U;
  disassemblyNextAddress = 0x00000000U;
  for (int i = 0; i < 6; i++)
    breakPointLists[i] = "";
  tmpBuffer.reserve(960);
  lptWindow = (Fl_Double_Window *) 0;
  lptTable = (Fl_Browser *) 0;
  lptBias = (Fl_Group *) 0;
  lptInfo = (Fl_Box *) 0;
  lptEditToggle = (Fl_Button *) 0;
  lptEditByte = (Fl_Group *) 0;
  lptEditValue = (Fl_Input *) 0;
  lptApplyButton = (Fl_Button *) 0;
  lptViewBase = 0U;
  lptRowCount = 0U;
  bpEditBuffer = new Fl_Text_Buffer();
  scriptEditBuffer = new Fl_Text_Buffer();
  createDebugWindow();
  window->label(&(windowTitle[0]));
  memoryDumpDisplay->upWidget = memoryDumpPrvPageButton;
  memoryDumpDisplay->downWidget = memoryDumpNxtPageButton;
  memoryDisplay_IX->upWidget = ixPrvPageButton;
  memoryDisplay_IX->downWidget = ixNxtPageButton;
  memoryDisplay_IY->upWidget = iyPrvPageButton;
  memoryDisplay_IY->downWidget = iyNxtPageButton;
  disassemblyDisplay->upWidget = disassemblyPrvPageButton;
  disassemblyDisplay->downWidget = disassemblyNxtPageButton;
}

Ep128EmuGUI_DebugWindow::~Ep128EmuGUI_DebugWindow()
{
  delete lptWindow;
  delete window;
  delete bpEditBuffer;
  delete scriptEditBuffer;
}

void Ep128EmuGUI_DebugWindow::show()
{
  this->activate();
  monitor_->closeTraceFile();
  updateWindow();
  if (!window->shown()) {
    window->resize(savedWindowPositionX, savedWindowPositionY, 960, 720);
    if (focusWidget != (Fl_Widget *) 0 && focusWidget != monitor_)
      focusWidget->take_focus();
    else
      stepButton->take_focus();
  }
  window->label(&(windowTitle[0]));
  window->show();
}

bool Ep128EmuGUI_DebugWindow::shown() const
{
  return bool(window->shown());
}

void Ep128EmuGUI_DebugWindow::hide()
{
  if (lptEditToggle) lptEditToggle->value(0);
  if (lptWindow) lptWindow->hide();
  this->deactivate(1000000.0);
  if (window->shown()) {
    savedWindowPositionX = window->x();
    savedWindowPositionY = window->y();
  }
  window->hide();
  std::strcpy(&(windowTitle[0]), "ep128emu debugger");
}

void Ep128EmuGUI_DebugWindow::activate()
{
  Fl::remove_timeout(&hideWindowCallback, (void *) this);
  debugTabs->clear_output();
  mainTab->clear_output();
  monitorTab->clear_output();
  stepIntoButton->clear_output();
  returnButton->clear_output();
  stepToButton->clear_output();
  stepToAddressValuator->clear_output();
  stepOverButton->clear_output();
  stepButton->clear_output();
  continueButton->clear_output();
}

bool Ep128EmuGUI_DebugWindow::active() const
{
  if (!window->shown())
    return false;
  return (!continueButton->output());
}

void Ep128EmuGUI_DebugWindow::deactivate(double tt)
{
  if (lptEditToggle) lptEditToggle->value(0);
  if (lptEditValue) lptEditValue->deactivate();
  if (lptApplyButton) lptApplyButton->deactivate();
  Fl::remove_timeout(&hideWindowCallback, (void *) this);
  if (tt <= 0.0) {
    this->hide();
    return;
  }
  mainTab->set_output();
  monitorTab->set_output();
  debugTabs->set_output();
  stepIntoButton->set_output();
  returnButton->set_output();
  stepToButton->set_output();
  stepToAddressValuator->set_output();
  stepOverButton->set_output();
  stepButton->set_output();
  continueButton->set_output();
  if (gui.debugWindowOpenFlag) {
    gui.debugWindowOpenFlag = false;
    gui.unlockVMThread();
  }
  if (tt < 1000.0)
    Fl::add_timeout(tt, &hideWindowCallback, (void *) this);
}

bool Ep128EmuGUI_DebugWindow::breakPoint(int type, uint16_t addr, uint8_t value)
{
  if ((type == 0 || type == 3) && monitor_->getIsTraceOn()) {
    monitor_->writeTraceFile(addr);
    if (type == 3)
      return false;
  }
  switch (type) {
  case 0:
  case 3:
    try {
      gui.vm.disassembleInstruction(tmpBuffer, addr, true);
      if (tmpBuffer.length() > 21 && tmpBuffer.length() <= 40) {
        std::sprintf(&(windowTitle[0]), "Break at PC=%04X: %s",
                     (unsigned int) (addr & 0xFFFF), (tmpBuffer.c_str() + 21));
        tmpBuffer = "";
        break;
      }
    }
    catch (...) {
    }
    tmpBuffer = "";
  case 1:
    std::sprintf(&(windowTitle[0]),
                 "Break on reading %02X from memory address %04X",
                 (unsigned int) (value & 0xFF), (unsigned int) (addr & 0xFFFF));
    break;
  case 2:
    std::sprintf(&(windowTitle[0]),
                 "Break on writing %02X to memory address %04X",
                 (unsigned int) (value & 0xFF), (unsigned int) (addr & 0xFFFF));
    break;
  case 5:
    std::sprintf(&(windowTitle[0]),
                 "Break on reading %02X from I/O port %02X",
                 (unsigned int) (value & 0xFF), (unsigned int) (addr & 0xFF));
    break;
  case 6:
    std::sprintf(&(windowTitle[0]),
                 "Break on writing %02X to I/O port %02X",
                 (unsigned int) (value & 0xFF), (unsigned int) (addr & 0xFF));
    break;
  default:
    std::sprintf(&(windowTitle[0]), "Break");
  }
  disassemblyViewAddress = uint32_t(gui.vm.getProgramCounter() & 0xFFFF);
  if (focusWidget == monitor_)
    monitor_->breakMessage(&(windowTitle[0]));
  return true;
}

void Ep128EmuGUI_DebugWindow::updateWindow()
{
  try {
    gui.vm.listCPURegisters(tmpBuffer);
    cpuRegisterDisplay->value(tmpBuffer.c_str());
    {
      char  tmpBuf[64];
      std::sprintf(&(tmpBuf[0]), "0000-3FFF: %02X\n4000-7FFF: %02X\n"
                                 "8000-BFFF: %02X\nC000-FFFF: %02X",
                   (unsigned int) gui.vm.getMemoryPage(0),
                   (unsigned int) gui.vm.getMemoryPage(1),
                   (unsigned int) gui.vm.getMemoryPage(2),
                   (unsigned int) gui.vm.getMemoryPage(3));
      memoryPagingDisplay->value(&(tmpBuf[0]));
    }
    uint32_t  tmp = gui.vm.getStackPointer();
    uint32_t  startAddr = (tmp + 0xFFF4U) & 0xFFF8U;
    uint32_t  endAddr = (startAddr + 0x002FU) & 0xFFFFU;
    dumpMemory(tmpBuffer, startAddr, endAddr, tmp, true, 1);
    stackMemoryDumpDisplay->value(tmpBuffer.c_str());
    tmpBuffer = "";
    updateMemoryDumpDisplay();
    updateIOPortDisplay();
    updateDisassemblyDisplay();
    bpPriorityThresholdValuator->value(
        double(gui.config.debug.bpPriorityThreshold));
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::dumpMemory(std::string& buf,
                                         uint32_t startAddr, uint32_t endAddr,
                                         uint32_t cursorAddr, bool showCursor,
                                         uint8_t addressMode)
{
  try {
    char      tmpBuf[48];
    uint8_t   tmpBuf2[8];
    buf.clear();
    uint32_t  addrMask = uint32_t(addressMode != 0 ? 0x0000FFFFU : 0x003FFFFFU);
    startAddr &= addrMask;
    endAddr &= addrMask;
    cursorAddr &= addrMask;
    if (addressMode > 1 && !showCursor) {
      showCursor = true;
      cursorAddr = 0xFFFFFFFFU;
    }
    while (true) {
      char    *bufp = &(tmpBuf[0]);
      Ep128Emu::printHexNumber(bufp, startAddr, (addressMode != 0 ? 2 : 0),
                               (addressMode != 0 ? 4 : 6),
                               (showCursor ? 0 : 1));
      bufp = bufp + (showCursor ? 6 : 7);
      int     cnt = 7;
      do {
        if (addressMode < 2)
          tmpBuf2[cnt] = gui.vm.readMemory(startAddr, bool(addressMode));
        else
          tmpBuf2[cnt] = gui.vm.readIOPort(uint16_t(startAddr));
        Ep128Emu::printHexNumber(bufp,
                                 tmpBuf2[cnt], (showCursor ? 2 : 1), 2, 0);
        if (showCursor && startAddr == cursorAddr)
          bufp[1] = '*';
        bufp = bufp + (showCursor ? 4 : 3);
        if (startAddr == endAddr)
          break;
        startAddr = (startAddr + 1U) & addrMask;
      } while (--cnt >= 0);
      if (cnt <= 0 && !showCursor) {
        bufp[0] = ' ';
        bufp[1] = ':';
        for (int i = 2; i < 10; i++) {
          bufp[i] = char(tmpBuf2[9 - i] & 0x7F);
          if (bufp[i] < char(0x20) || bufp[i] == char(0x7F))
            bufp[i] = '.';
        }
        bufp[10] = '\0';
        bufp = bufp + 10;
      }
      if (cnt >= 0)
        break;
      bufp[0] = '\n';
      bufp[1] = '\0';
      buf += &(tmpBuf[0]);
    }
    buf += &(tmpBuf[0]);
  }
  catch (std::exception& e) {
    buf.clear();
    gui.errorMessage(e.what());
  }
}

char * Ep128EmuGUI_DebugWindow::dumpMemoryAtRegister(
    char *bufp, const char *regName, uint16_t addr, int byteCnt,
    uint32_t cursorAddr)
{
  while ((*regName) != '\0')
    *(bufp++) = *(regName++);
  while (byteCnt-- > 0) {
    char    *nxtp =
        Ep128Emu::printHexNumber(bufp, gui.vm.readMemory(addr, true), 2, 2, 0);
    if (uint32_t(addr) == cursorAddr)
      bufp[1] = '*';
    bufp = nxtp;
    addr = (addr + 1) & 0xFFFF;
  }
  *bufp = '\0';
  return bufp;
}

void Ep128EmuGUI_DebugWindow::updateMemoryDumpDisplay()
{
  try {
    uint32_t  addrMask =
        uint32_t(memoryDumpAddressMode != 0 ? 0x00FFFFU : 0x3FFFFFU);
    memoryDumpStartAddress &= addrMask;
    memoryDumpViewAddress &= addrMask;
    const char  *fmt = (memoryDumpAddressMode != 0 ? "%04X" : "%06X");
    char  tmpBuf[64];
    std::sprintf(&(tmpBuf[0]), fmt, (unsigned int) memoryDumpStartAddress);
    memoryDumpStartAddressValuator->value(&(tmpBuf[0]));
    dumpMemory(tmpBuffer, memoryDumpViewAddress, memoryDumpViewAddress + 0x2FU,
               0U, false, memoryDumpAddressMode);
    memoryDumpDisplay->value(tmpBuffer.c_str());
    const Ep128::Z80_REGISTERS& r =
        ((const Ep128Emu::VirtualMachine *) &(gui.vm))->getZ80Registers();
    {
      char    *bufp = dumpMemoryAtRegister(&(tmpBuf[0]), " BC-04",
                                           (r.BC.W - 4) & 0xFFFF, 12, r.BC.W);
      *(bufp++) = '\n';
      *bufp = '\0';
      tmpBuffer = &(tmpBuf[0]);
      bufp = dumpMemoryAtRegister(&(tmpBuf[0]), " DE-04",
                                  (r.DE.W - 4) & 0xFFFF, 12, r.DE.W);
      *(bufp++) = '\n';
      *bufp = '\0';
      tmpBuffer += &(tmpBuf[0]);
      bufp = dumpMemoryAtRegister(&(tmpBuf[0]), " HL-04",
                                  (r.HL.W - 4) & 0xFFFF, 12, r.HL.W);
      tmpBuffer += &(tmpBuf[0]);
    }
    memoryDisplay_BC_DE_HL->value(tmpBuffer.c_str());
    for (int i = 0; i < 6; i++) {
      char      *bufp = &(tmpBuf[0]);
      uint16_t  addr = uint16_t(i < 3 ? r.IX.W : r.IY.W);
      int32_t   offs = (i < 3 ? ixViewOffset : (iyViewOffset - 24))
                       + int32_t((i - 1) * 8);
      *(bufp++) = ' ';
      *(bufp++) = 'I';
      *(bufp++) = (i < 3 ? 'X' : 'Y');
      *(bufp++) = (offs >= 0 ? '+' : '-');
      bufp = Ep128Emu::printHexNumber(bufp, uint32_t(offs >= 0 ?
                                                     offs : (-offs)) & 0xFFU,
                                      0, 2, 0);
      bufp = dumpMemoryAtRegister(bufp, "",
                                  uint16_t((int32_t(addr) + offs) & 0xFFFF), 8);
      if (i != 2 && i != 5) {
        *(bufp++) = '\n';
        *bufp = '\0';
      }
      if (i == 0 || i == 3)
        tmpBuffer = &(tmpBuf[0]);
      else
        tmpBuffer += &(tmpBuf[0]);
      if (i == 2)
        memoryDisplay_IX->value(tmpBuffer.c_str());
      else if (i == 5)
        memoryDisplay_IY->value(tmpBuffer.c_str());
    }
    tmpBuffer = "";
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::updateIOPortDisplay()
{
  try {
    gui.vm.listIORegisters(tmpBuffer);
    ioPortDisplay->value(tmpBuffer.c_str());
    tmpBuffer = "";
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::updateDisassemblyDisplay()
{
  try {
    disassemblyStartAddress &= 0xFFFFU;
    disassemblyViewAddress &= 0xFFFFU;
    disassemblyNextAddress &= 0xFFFFU;
    char  tmpBuf[8];
    std::sprintf(&(tmpBuf[0]), "%04X", (unsigned int) disassemblyStartAddress);
    disassemblyStartAddressValuator->value(&(tmpBuf[0]));
    std::string tmp;
    tmp.reserve(48);
    tmpBuffer = "";
    uint32_t  addr = disassemblySearchBack(2);
    uint32_t  pcAddr = uint32_t(gui.vm.getProgramCounter()) & 0xFFFFU;
    for (int i = 0; i < 23; i++) {
      if (i == 22)
        disassemblyNextAddress = addr;
      uint32_t  nxtAddr = gui.vm.disassembleInstruction(tmp, addr, true, 0);
      while (addr != nxtAddr) {
        if (addr == pcAddr)
          tmp[1] = '*';
        addr = (addr + 1U) & 0xFFFFU;
      }
      tmpBuffer += tmp;
      if (i != 22)
        tmpBuffer += '\n';
    }
    disassemblyDisplay->value(tmpBuffer.c_str());
    tmpBuffer = "";
  }
  catch (std::exception& e) {
    gui.errorMessage(e.what());
  }
}

uint32_t Ep128EmuGUI_DebugWindow::disassemblySearchBack(int insnCnt)
{
  uint32_t  addrTable[256];
  uint8_t   insnLengths[256];
  if (insnCnt > 50)
    insnCnt = 50;
  for (uint32_t addr = disassemblyViewAddress - (uint32_t(insnCnt * 4) + 28U);
       true;
       addr++) {
    addr = addr & 0xFFFFU;
    uint32_t  tmp =
        Ep128::Z80Disassembler::getNextInstructionAddr(gui.vm, addr, true);
    insnLengths[addr & 0xFFU] = uint8_t((tmp - addr) & 0xFFU);
    if (addr == (disassemblyViewAddress & 0xFFFFU))
      break;
  }
  for (uint32_t offs = 28U; true; offs--) {
    int       addrCnt = 0;
    uint32_t  addr = disassemblyViewAddress - (uint32_t(insnCnt * 4) + offs);
    bool      doneFlag = false;
    addr = addr & 0xFFFFU;
    addrTable[addrCnt++] = addr;
    do {
      uint32_t  nxtAddr =
          (addr + uint32_t(insnLengths[addr & 0xFFU])) & 0xFFFFU;
      while (addr != nxtAddr) {
        if (addr == disassemblyViewAddress)
          doneFlag = true;
        addr = (addr + 1U) & 0xFFFFU;
      }
      addrTable[addrCnt++] = addr;
    } while (!doneFlag);
    for (int i = 0; i < addrCnt; i++) {
      if (addrTable[i] == disassemblyViewAddress) {
        if (i >= insnCnt)
          return addrTable[i - insnCnt];
      }
    }
    if (offs == 0U) {
      if (insnCnt < 2)
        break;
      if (insnCnt > 2)
        return addrTable[(addrCnt > insnCnt ? ((addrCnt - 1) - insnCnt) : 0)];
      insnCnt--;
      offs = 12U;
    }
  }
  return disassemblyViewAddress;
}

void Ep128EmuGUI_DebugWindow::applyBreakPointList()
{
  const char  *buf = (char *) 0;
  try {
    buf = bpEditBuffer->text();
    if (!buf)
      throw std::bad_alloc();
    std::string bpListText(buf);
    std::free(const_cast<char *>(buf));
    buf = (char *) 0;
    Ep128Emu::BreakPointList  bpList(bpListText);
    gui.vm.clearBreakPoints();
    gui.vm.setBreakPoints(bpList);
  }
  catch (std::exception& e) {
    if (buf)
      std::free(const_cast<char *>(buf));
    gui.errorMessage(e.what());
  }
}

void Ep128EmuGUI_DebugWindow::breakPointCallback(void *userData, int type,
                                                 uint16_t addr, uint8_t value)
{
  Ep128EmuGUI_DebugWindow&  debugWindow =
      *(reinterpret_cast<Ep128EmuGUI_DebugWindow *>(userData));
  if (!debugWindow.luaScript.runBreakPointCallback(type, addr, value)) {
    return;
  }
  Ep128EmuGUI&  gui_ = debugWindow.gui;
  Fl::lock();
  if (gui_.exitFlag || !gui_.mainWindow->shown()) {
    Fl::unlock();
    return;
  }
  if (!debugWindow.breakPoint(type, addr, value)) {
    Fl::unlock();
    return;             // do not show debugger window if tracing
  }
  if (!debugWindow.active()) {
    gui_.debugWindowShowFlag = true;
    Fl::awake();
  }
  while (gui_.debugWindowShowFlag) {
    Fl::unlock();
    gui_.updateDisplay();
    Fl::lock();
  }
  while (true) {
    bool  tmp = debugWindow.active();
    Fl::unlock();
    if (!tmp)
      break;
    gui_.updateDisplay();
    Fl::lock();
  }
}

void Ep128EmuGUI_DebugWindow::hideWindowCallback(void *userData)
{
  reinterpret_cast< Ep128EmuGUI_DebugWindow * >(userData)->hide();
}

