#ifndef CURRENT_MODULE_HPP
#define CURRENT_MODULE_HPP
#include "logger/exception_handler.hpp"

inline std::string GetCurrentModule() {
  const char* modules[] = { "GTA5.exe", "Paragon_Legacy.exe", "game_win64_final.exe" };

  for (int i = 0; i < 3; ++i) {
    if (GetModuleHandle(modules[i]) != NULL) {
      return modules[i];
    }
  }
  return ""; // None found
}

#endif //CURRENT_MODULE_HPP
