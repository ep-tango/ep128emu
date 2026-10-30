// Stable native window-caption columns for Enterprise, ZX, CPC and TVC.
// Pure string formatting, with no FLTK dependency (native caption font owns rendering).
#ifndef EP128EMU_TITLEBAR_FIELDS_HPP
#define EP128EMU_TITLEBAR_FIELDS_HPP

#include <string>
#include <cstring>

namespace Ep128EmuTitle {
  static const size_t machineWidth = 4U;
  static const size_t speedWidth = 7U;
  // The metric is the last field: its variable length cannot move columns.
  static const size_t expansionWidth = 21U;

  inline std::string column(const char *value, size_t width)
  {
    std::string s(value ? value : "");
    if (s.size() > width) {
      s.resize(width);
      return s;
    }
    s.append(width - s.size(), ' ');
    return s;
  }

  inline std::string format(const char *application,
                            const char *machine, const char *speed,
                            const char *runtimeMetric, const char *expansion)
  {
    std::string result(application ? application : "ep128emu");
    result += "     [";
    result += column(machine, machineWidth);
    result += "]     [";
    result += column(speed, speedWidth);
    result += "]";
    // Put the long/variable FPS or rewind metric LAST. Do not waste 60
    // caption characters padding this suffix: native titlebars may clip it.
    result += "     [";
    result += column(expansion, expansionWidth);
    result += "]";
    if (runtimeMetric && runtimeMetric[0]) {
      result += "     |     ";
      result += runtimeMetric;
    }
    return result;
  }
}
#endif
