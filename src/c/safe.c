//- Copyright 2013 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

#include "condition.h"
#include "runtime.h"
#include "safe-inl.h"
#include "try-inl.h"


bool safe_value_is_immediate(safe_value_t s_value) {
  return value_is_immediate(s_value.as_value);
}

bool object_tracker_is_always_weak(object_tracker_t *tracker) {
  return (tracker->flags & tfAlwaysWeak) != 0;
}

bool object_tracker_is_maybe_weak(object_tracker_t *tracker) {
  return (tracker->flags & tfMaybeWeak) != 0;
}

bool object_tracker_is_finalize_explicit(object_tracker_t *tracker) {
  return (tracker->flags & tfFinalizeExplicit) != 0;
}

bool object_tracker_is_garbage(object_tracker_t *tracker) {
  return (tracker->state & tsGarbage) != 0;
}

bool safe_value_is_nothing(safe_value_t s_value) {
  // This works both for immediates and non-immediates because non-immediates
  // are object-tagged and hence distinct from nothing.
  return is_nothing(s_value.as_value);
}

bool safe_value_is_condition(safe_value_t s_value) {
  // Same as safe_value_is_nothing.
  return is_condition(s_value.as_value);
}

bool object_tracker_is_currently_weak(object_tracker_t *tracker) {
  if (object_tracker_is_always_weak(tracker)) {
    return true;
  } else if (object_tracker_is_maybe_weak(tracker)) {
    maybe_weak_object_tracker_t *maybe_weak = maybe_weak_object_tracker_from(tracker);
    CHECK_FALSE("weakness not determined", maybe_weak->weakness == wsUnknown);
    return maybe_weak->weakness == wsWeak;
  } else {
    return false;
  }
}

maybe_weak_object_tracker_t *maybe_weak_object_tracker_from(object_tracker_t *tracker) {
  return object_tracker_is_maybe_weak(tracker)
      ? (maybe_weak_object_tracker_t*) tracker
      : NULL;
}

finalize_explicit_object_tracker_t *finalize_explicit_object_tracker_from(object_tracker_t *tracker) {
  return object_tracker_is_finalize_explicit(tracker)
      ? (finalize_explicit_object_tracker_t*) tracker
      : NULL;
}

safe_value_t object_tracker_to_safe_value(object_tracker_t *handle) {
  value_t target = handle->value;
  safe_value_t s_result;
  s_result.as_value.encoded = ((address_arith_t) handle) + get_value_domain(target);
  CHECK_FALSE("cast into condition", safe_value_is_immediate(s_result));
  return s_result;
}

object_tracker_t *safe_value_to_object_tracker(safe_value_t s_value) {
  CHECK_FALSE("using immediate as indirect", safe_value_is_immediate(s_value));
  address_t result = (address_t) (address_arith_t) (s_value.as_value.encoded & ~kDomainTagMask);
  return (object_tracker_t*) result;
}

safe_value_t protect_immediate(value_t value) {
  CHECK_TRUE("value not immediate", value_is_immediate(value));
  safe_value_t s_result;
  s_result.as_value = value;
  CHECK_TRUE("cast out of condition", safe_value_is_immediate(s_result));
  return s_result;
}

safe_value_t empty_safe_value() {
  return protect_immediate(new_integer(0));
}

value_t deref_immediate(safe_value_t s_value) {
  CHECK_TRUE("using indirect as immediate", safe_value_is_immediate(s_value));
  return s_value.as_value;
}

value_t deref(safe_value_t s_value) {
  return safe_value_is_immediate(s_value)
      ? deref_immediate(s_value)
      : safe_value_to_object_tracker(s_value)->value;
}

bool safe_value_is_garbage(safe_value_t s_value) {
  return !safe_value_is_immediate(s_value)
      && object_tracker_is_garbage(safe_value_to_object_tracker(s_value));
}

void safe_value_pool_init(safe_value_pool_t *pool, safe_value_t *values,
    size_t capacity, runtime_t *runtime) {
  pool->values = values;
  pool->capacity = capacity;
  pool->used = 0;
  pool->runtime = runtime;
}

void safe_value_pool_dispose(safe_value_pool_t *pool) {
  for (size_t i = 0; i < pool->used; i++)
    safe_value_destroy(pool->runtime, pool->values[i]);
}

safe_value_t protect(safe_value_pool_t *pool, value_t value) {
  COND_CHECK_TRUE_WITH_VALUE("safe value pool overflow", ccSafePoolFull,
      protect_immediate(new_condition(ccSafePoolFull)),
      pool->used < pool->capacity);
  safe_value_t result = runtime_protect_value(pool->runtime, value);
  // Technically we only need to record the non-immediate values but that makes
  // the number of used values highly variable. This way it's harder (though
  // not impossible obviously) for bugs to hide.
  pool->values[pool->used++] = result;
  return result;
}
