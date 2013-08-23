// Copyright 2013 the Neutrino authors (see AUTHORS).
// Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "signals.h"

invalid_syntax_cause_t get_invalid_syntax_signal_cause(value_t signal) {
  return get_signal_details(signal);
}

const char *get_signal_cause_name(signal_cause_t cause) {
  switch (cause) {
#define __GEN_CASE__(Name) case sc##Name: return #Name;
    ENUM_SIGNAL_CAUSES(__GEN_CASE__)
#undef __GEN_CASE__
    default:
      return "invalid signal";
  }
}
