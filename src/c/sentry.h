//- Copyright 2013 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

// Sentries are compile-time macros that can be passed to other macros such as
// assertions and try-catch. They check properties of values. It's a way to make
// the condition to check orthogonal to the mechanics of actually checking it
// and reacting if the check fails. The name "guard" might have been better but
// that's already taken by neutrino parameter guards.
//
// There's a fair bit of macro magic going on here but it should be robust.

#ifndef _SENTRY
#define _SENTRY

#include "condition.h"
#include "tagged.h"
#include "value.h"

// Signature of a function that implements a family-based sentry.
typedef bool (*family_sentry_impl_f)(heap_object_family_t family, value_t self,
    value_t *error);

// Sentry implementation for a family-based sentry along with the family to test
// against.
typedef struct {
  heap_object_family_t family;
  family_sentry_impl_f impl;
} family_sentry_test_t;

// Bundles together a family and an impl to make a test struct.
static inline family_sentry_test_t family_sentry_test_new(heap_object_family_t family,
    family_sentry_impl_f impl) {
  family_sentry_test_t result = {family, impl};
  return result;
}

// A sentry is a tuple of the components you need to implement the check
// appropriate for the different places they're used. The entries are.
//
//   (is_empty?, impl, argument, display_name)
//
// where the different components mean,
//
//   is_empty?: Should this sentry be ignored completely?
//   impl: name of the function that implements this sentry.
//   argument: argument that will be passed in all calls to the sentry function.
//   test_constr: constructor function for a test struct for this sentry.
//   display_name: a literal string that will be printed if a sentry check
//     fails.

// A sentry that checks that a value is in a particular family.
#define snInFamily(ofFamily) (_,                                               \
    in_family_sentry_impl,                                                     \
    family_sentry_test_new,                                                    \
    ofFamily,                                                                  \
    "inFamily(" #ofFamily ")")

static inline bool in_family_sentry_impl(heap_object_family_t family, value_t self,
    value_t *error_out) {
  if (!in_family(family, self)) {
    *error_out = new_unexpected_type_condition(value_type_info_for_family(family),
        get_value_type_info(self));
    return false;
  } else {
    return true;
  }
}

// Is the value nothing or in a particular family?
#define snInFamilyOpt(ofFamily) (_,                                            \
    in_family_opt_sentry_impl,                                                 \
    family_sentry_test_new,                                                    \
    ofFamily,                                                                  \
    "inFamilyOpt(" #ofFamily ")")

static inline bool in_family_opt_sentry_impl(heap_object_family_t family,
    value_t self, value_t *error_out) {
  if (!in_family_opt(family, self)) {
    *error_out = new_unexpected_type_condition(value_type_info_for_family(family),
        get_value_type_info(self));
    return false;
  } else {
    return true;
  }
}

// Is the value null or in a particular family?
#define snInFamilyOrNull(ofFamily) (_,                                         \
    in_family_or_null_sentry_impl,                                             \
    family_sentry_test_new,                                                    \
    ofFamily,                                                                  \
    "inFamilyOrNull(" #ofFamily ")")

static inline bool in_family_or_null_sentry_impl(heap_object_family_t family,
    value_t self, value_t *error_out) {
  if (!in_family_or_null(family, self)) {
    *error_out = new_unexpected_type_condition(value_type_info_for_family(family),
        get_value_type_info(self));
    return false;
  } else {
    return true;
  }
}


// Is the value in a particular domain?
#define snInDomain(vdDomain) (_,                                               \
    in_domain_sentry_impl,                                                     \
    0,                                                                         \
    vdDomain,                                                                  \
    "inDomain(" #vdDomain ")")

static inline bool in_domain_sentry_impl(value_domain_t domain, value_t self,
    value_t *error_out) {
  if (!in_domain(domain, self)) {
    *error_out = new_unexpected_type_condition(value_type_info_empty(),
        get_value_type_info(self));
    return false;
  } else {
    return true;
  }
}

// Is the value nothing or in a particular domain?
#define snInDomainOpt(vdDomain) (_,                                            \
    in_domain_opt_sentry_impl,                                                 \
    0,                                                                         \
    vdDomain,                                                                  \
    "inDomainOpt(" #vdDomain ")")

static inline bool in_domain_opt_sentry_impl(value_domain_t domain, value_t self,
    value_t *error_out) {
  if (!in_domain_opt(domain, self)) {
    *error_out = new_unexpected_type_condition(value_type_info_empty(),
        get_value_type_info(self));
    return false;
  } else {
    return true;
  }
}

#define EVAL(E)

// Sentry that checks that the value is an array and the elements are all heap
// objects that pass the given sentry.
#define snIsArrayOfFamily(SENTRY) (_,                                          \
    is_array_of_family_sentry_impl,                                            \
    0,                                                                         \
    (__SENTRY_CONSTR__ SENTRY (__SENTRY_ARGUMENT__ SENTRY, __SENTRY_IMPL__ SENTRY)), \
    "isArrayOfFamily(" __SENTRY_NAME__ SENTRY ")")

// Returns true iff the given value is an array of heap objects for which the
// given family test holds.
bool is_array_of_family_sentry_impl(family_sentry_test_t test, value_t self,
    value_t *error_out);


// Sentry that checks that the given value is within a syntax family.
#define snInSyntaxFamily (_,                                                   \
    in_syntax_family_sentry_impl,                                              \
    family_sentry_test_new,                                                    \
    __ofUnknown__,                                                             \
    "inSyntaxFamily")

static inline bool in_syntax_family_sentry_impl(heap_object_family_t unused,
    value_t self, value_t *error_out) {
  if (!in_syntax_family(self)) {
    *error_out = new_unexpected_type_condition(value_type_info_empty(),
        get_value_type_info(self));
    return false;
  } else {
    return true;
  }
}


// Sentry that checks that the given value is within a syntax family.
#define snInSyntaxFamilyOpt (_,                                                \
    in_syntax_family_opt_sentry_impl,                                          \
    family_sentry_test_new,                                                    \
    __ofUnknown__,                                                             \
    "inSyntaxFamilyOpt")

static inline bool in_syntax_family_opt_sentry_impl(heap_object_family_t unused,
    value_t self, value_t *error_out) {
  if (!in_syntax_family_opt(self)) {
    *error_out = new_unexpected_type_condition(value_type_info_empty(),
        get_value_type_info(self));
    return false;
  } else {
    return true;
  }
}


// Sentry that checks that the given value is within a syntax family.
#define snInSyntaxFamilyOrNull (_,                                             \
    in_syntax_family_or_null_sentry_impl,                                      \
    family_sentry_test_new,                                                    \
    __ofUnknown__,                                                             \
    "inSyntaxFamilyOrNull")

static inline bool in_syntax_family_or_null_sentry_impl(heap_object_family_t unused,
    value_t self, value_t *error_out) {
  if (!(is_null(self) || in_syntax_family(self))) {
    *error_out = new_unexpected_type_condition(value_type_info_empty(),
        get_value_type_info(self));
    return false;
  } else {
    return true;
  }
}


// A sentry that does nothing.
#define snNoCheck (X,                                                          \
    no_check_sentry_impl,                                                      \
    0,                                                                         \
    0,                                                                         \
    "noCheck")

// Expect function used for the no-check sentry. Always succeeds.
static inline bool no_check_sentry_impl(int unused, value_t self, value_t *error_out) {
  return true;
}

// These macros extract components from a sentry tuple.
#define __SENTRY_IS_EMPTY__(IS_EMPTY, IMPL, CONSTR, ARG, NAME) IS_EMPTY
#define __SENTRY_IMPL__(IS_EMPTY, IMPL, CONSTR, ARG, NAME) IMPL
#define __SENTRY_CONSTR__(IS_EMPTY, IMPL, CONSTR, ARG, NAME) CONSTR
#define __SENTRY_ARGUMENT__(IS_EMPTY, IMPL, CONSTR, ARG, NAME) ARG
#define __SENTRY_NAME__(IS_EMPTY, IMPL, CONSTR, ARG, NAME) NAME

// Yields an expression that is true iff the sentry holds.
#define SENTRY_TEST(SENTRY, VAR_IN, VAR_OUT) __SENTRY_IMPL__ SENTRY (__SENTRY_ARGUMENT__ SENTRY, VAR_IN, VAR_OUT)

#define __EXPECT_UNCOND__(SENTRY, EXPR) do {                                   \
    value_t __eu_error__;                                                      \
    if (!(__SENTRY_IMPL__ SENTRY (__SENTRY_ARGUMENT__ SENTRY, (EXPR), &__eu_error__))) \
      return __eu_error__;                                                     \
  } while (false)

// If the given sentry holds for the given expression does nothing. Otherwise
// returns an appropriate condition.
#define EXPECT_SENTRY(SENTRY, EXPR) __SENTRY_IS_EMPTY__ SENTRY (, __EXPECT_UNCOND__(SENTRY, EXPR))

#define __CHECK_SENTRY_UNCOND__(SENTRY, EXPR) do {                             \
    value_t __csu_error__;                                                     \
    if (!(__SENTRY_IMPL__ SENTRY (__SENTRY_ARGUMENT__ SENTRY, (EXPR), &__csu_error__))) \
      check_fail(__FILE__, __LINE__, "CHECK_SENTRY(%s, %s) failed.\n  Error: %v", (__SENTRY_NAME__ SENTRY), #EXPR, __csu_error__); \
  } while (false)

// Check that fails unless the object is in the specified family.
#define CHECK_SENTRY(SENTRY, EXPR)                                             \
    __SENTRY_IS_EMPTY__ SENTRY (, IF_CHECKS_ENABLED(__CHECK_SENTRY_UNCOND__(SENTRY, EXPR)))

#endif // _SENTRY
