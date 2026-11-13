#pragma once

#include <source_location>

#include <cstdlib>
#include <iostream>

namespace test_check {
  inline void require (bool condition, const char* expression,
                       std::source_location location = std::source_location::current ()) {
    if (not condition) {
      std::cerr << location.file_name () << ':' << location.line ()
                << ": check failed: " << expression << '\n';
      std::exit (EXIT_FAILURE);
    }
  }
}

// Test observations and operations must also execute when NDEBUG is defined.
#define CHECK(...) test_check::require (static_cast<bool> ((__VA_ARGS__)), #__VA_ARGS__)
