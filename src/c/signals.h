// Copyright 2013 the Neutrino authors (see AUTHORS).
// Licensed under the Apache License, Version 2.0 (see LICENSE).

// Utilities related to runtime-internal signals. Not to be confused with
// system signals which live in <signal.h>, which is why this file is called
// signalS, plural.

#include "value.h"

#ifndef _SIGNALS
#define _SIGNALS

// Creates a new signal with the specified cause and details.
static value_t new_signal_with_details(signal_cause_t cause, uint32_t details) {
  return (value_t) {.as_signal={vdSignal, cause, details}};
}

// Creates a new signal with the specified cause and no details.
static value_t new_signal(signal_cause_t cause) {
  return new_signal_with_details(cause, 0);
}

// Returns the cause of a signal.
static signal_cause_t get_signal_cause(value_t value) {
  CHECK_DOMAIN(vdSignal, value);
  return value.as_signal.cause;
}

// Returns the string name of a signal cause.
const char *get_signal_cause_name(signal_cause_t cause);

// Returns the details associated with the given signal.
static uint32_t get_signal_details(value_t value) {
  CHECK_DOMAIN(vdSignal, value);
  return value.as_signal.details;
}

// Reasons for syntax to be invalid.
typedef enum {
  isSymbolAlreadyBound,
  isSymbolNotBound,
  isNotSyntax,
} invalid_syntax_cause_t;

// Creates a new SyntaxInvalid signal with the given cause.
static value_t new_invalid_syntax_signal(invalid_syntax_cause_t cause) {
  return new_signal_with_details(scInvalidSyntax, cause);
}

// Returns the cause of an invalid syntax signal.
invalid_syntax_cause_t get_invalid_syntax_signal_cause(value_t signal);

#endif // _SIGNALS
