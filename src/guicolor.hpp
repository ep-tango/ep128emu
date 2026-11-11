
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

#ifndef EP128EMU_GUICOLOR_HPP
#define EP128EMU_GUICOLOR_HPP

class Fl_Window;

namespace Ep128Emu {

  /*!
   * Set the GUI theme to be used (should be called before any window is
   * shown):
   *   0: default (dark background)
   *   1: Windows 2000
   *   2: plastic
   *   3: Gtk+
   */
  void setGUIColorScheme(int colorScheme = 0);

  /*!
   * Set the machine-specific window icon before the first show. Supported
   * on Windows and FLTK 1.3.3+ X11/Cocoa. On X11, also set the matching
   * WM_CLASS so the correct .desktop launcher owns the taskbar window.
   * 'iconNum' can be one of the following values:
   *   0: ep128emu logo
   *   1: zx128emu logo
   *   2: cpc464emu logo
   *   3: tvc64emu logo
   *  10: information icon
   *  11: question icon
   *  12: warning icon
   *  13: error icon
   */
  void setWindowIcon(Fl_Window *w, int iconNum);
#ifdef WIN32
  // Refresh the realized top-level window's taskbar/Alt-Tab icons on Win7+.
  // A Windows executable's static Explorer icon is intentionally unchanged.
  void setWindowsTaskbarMachineIcon(Fl_Window *w, int iconNum);
#endif

}       // namespace Ep128Emu

#endif  // EP128EMU_GUICOLOR_HPP

