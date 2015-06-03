//- Copyright 2013 the Neutrino authors (see AUTHORS).
//- Licensed under the Apache License, Version 2.0 (see LICENSE).

/// # Process and execution objects
///
/// Types related to process execution are defined in this header. The code that
/// actually executes bytecode lives in {{interp.h}}.
///
/// The aim has been to have a few different execution mechanisms that work as
/// a foundation for the control abstractions provided by the surface language.
/// The main abstractions are:
///
///   - Segmented {{#Stack}}(Stacks). A stack segment is known as a
///     {{#StackPiece}}(stack piece). A stack segment is basically a dumb object
///     array, both of these types have very little functionality on their own.
///   - The {{#Frame}}(frame) type provides access to a segmented stack, both
///     for inspection and modification. This is a stack-allocated type, not
///     heap allocated.
///   - {{#Escape}}(Escapes) capture the information you need to do a non-local
///     return. They can be thought of as pointers into the stack which allow
///     you top abort execution and jump back to the place they're pointing to.
///     If you know `setjmp`/`longjmp` from
///     [setjmp.h](http://en.wikipedia.org/wiki/Setjmp.h), this is basically the
///     same thing.
///   - Various closures: {{#Lambda}}(lambdas), {{#Block}}(blocks), and
///     {{#CodeShard}}(code shards). These are similar but work slightly differently:
///     lambdas and blocks are visible at the surface language level; they're
///     different in that lambdas can outlive their scope so they have to
///     capture the values they close over whereas blocks are killed when their
///     scope exits and so can access outer values directly through the stack.
///     Code shards are like blocks but are not visible on the surface so they
///     don't need to be killed because the runtime knows they won't outlive
///     their scope.

#ifndef _PROCESS
#define _PROCESS

#include "derived.h"
#include "value-inl.h"
#include "sync/semaphore.h"
#include "utils/boundbuf.h"

/// ## Stack piece
///
/// A stack piece is a contiguous segment of a segmented call stack. The actual
/// backing store of a stack piece is just an array. All values on the stack are
/// properly tagged so a stack piece can be gc'ed trivially.
///
/// ### Open vs. closed
///
/// A stack piece can be either _open_ or _closed_. All interaction with a stack
/// piece happens through a `frame_t` which holds information about the piece's
/// top frame. Opening a stack piece means writing information about the top
/// frame into a `frame_t`. Closing it means writing the information back into
/// the piece. Saving information about the current top frame also happens when
/// invoking a method and the same code is responsible for doing both. The fake
/// frame that is at the top of a closed stack piece is called the _lid_.
///
/// ### Stack pointer
///
/// Stack pieces have a pointer back to the stack they're a part of (unless
/// they're not a member of one in which case the pointer is nothing). Stack
/// validation checks that all pieces point to the stack so make sure to always
/// set the stack pointer on a piece first before putting the piece into the
/// stack.

static const size_t kStackPieceHeaderSize = HEAP_OBJECT_SIZE(4);
static const size_t kStackPieceCapacityOffset = HEAP_OBJECT_FIELD_OFFSET(0);
static const size_t kStackPiecePreviousOffset= HEAP_OBJECT_FIELD_OFFSET(1);
static const size_t kStackPieceStackOffset = HEAP_OBJECT_FIELD_OFFSET(2);
static const size_t kStackPieceLidFramePointerOffset = HEAP_OBJECT_FIELD_OFFSET(3);
static const size_t kStackPieceStorageOffset = HEAP_OBJECT_FIELD_OFFSET(4);

// Calculates the size of a heap stack piece with the given capacity.
size_t calc_stack_piece_size(size_t capacity);

// The capacity of this stack piece.
ACCESSORS_DECL(stack_piece, capacity);

// The previous, lower, stack piece.
ACCESSORS_DECL(stack_piece, previous);

// The stack this piece is a part of.
ACCESSORS_DECL(stack_piece, stack);

// The frame pointer for the lid frame. Only set if the piece is closed.
ACCESSORS_DECL(stack_piece, lid_frame_pointer);

// Returns the beginning of the storage portion of the given stack piece.
value_t *get_stack_piece_storage(value_t self);

// Returns true if the given stack piece is in the closed state.
bool is_stack_piece_closed(value_t self);

// Flags that describe a stack frame.
typedef enum {
  // This is a maintenance frame inserted by the runtime.
  ffSynthetic        = 0x01,
  // This is a bottom frame of a stack piece, the one that returns to the
  // previous stack piece.
  ffStackPieceBottom = 0x02,
  // This isn't a real frame but the initial state of a stack piece that has
  // no frames.
  ffStackPieceEmpty  = 0x04,
  // This is the bottom frame of a stack, the one that ends execution on that
  // stack.
  ffStackBottom      = 0x08,
  // This is an organic stack frame generated by a method invocation.
  ffOrganic          = 0x10,
  // This frame is the lid of a closed stack piece.
  ffLid              = 0x20
} frame_flag_t;


/// ## Frame

// A transient stack frame. The current structure isn't clever but it's not
// intended to be, it's intended to work and be fully general.
struct frame_t {
  // Pointer to the next available field on the stack.
  value_t *stack_pointer;
  // Pointer to the bottom of the stack fields.
  value_t *frame_pointer;
  // The limit beyond which no more data can be written for this frame.
  value_t *limit_pointer;
  // The flags describing this frame.
  value_t flags;
  // The stack piece that contains this frame.
  value_t stack_piece;
  // The program counter.
  size_t pc;
};

// Returns an empty frame to use for initialization.
static frame_t frame_empty() {
  frame_t result = {NULL, NULL, NULL, nothing(), nothing(), 0};
  return result;
}

// The number of word-size fields in a frame.
#define kFrameFieldCount 5

// The number of words in a stack frame header.
static const size_t kFrameHeaderSize
    = (kFrameFieldCount - 2)  // The frame fields minus the stack pointer which
                              //   is implicit and the stack piece.
    + 1                       // The code block
    + 1                       // The PC
    + 1;                      // The argument map

// Offsets _down_ from the frame pointer to the header fields.
static const size_t kFrameHeaderPreviousFramePointerOffset = 0;
static const size_t kFrameHeaderPreviousLimitPointerOffset = 1;
static const size_t kFrameHeaderPreviousFlagsOffset = 2;
static const size_t kFrameHeaderPreviousPcOffset = 3;
static const size_t kFrameHeaderCodeBlockOffset = 4;
static const size_t kFrameHeaderArgumentMapOffset = 5;

// Tries to allocate a new frame above the given frame of the given capacity.
// Returns true iff allocation succeeds.
bool try_push_new_frame(frame_t *frame, size_t capacity, uint32_t flags,
    bool is_lid);

// Puts the given stack piece in the open state and stores the state required to
// interact with it in the given frame struct.
void open_stack_piece(value_t piece, frame_t *frame);

// Records the state stored in the given frame in its stack piece and closes
// the stack piece.
void close_frame(frame_t *frame);

// Pops the given frame off, storing the next frame. The stack piece that holds
// the given frame must have more frames, otherwise the behavior is undefined
// (or fails when checks are enabled).
void frame_pop_within_stack_piece(frame_t *frame);

// Record the frame pointer for the previous stack frame, the one below this one.
void frame_set_previous_frame_pointer(frame_t *frame, size_t value);

// Returns the frame pointer for the previous stack frame, the one below this
// one.
size_t frame_get_previous_frame_pointer(frame_t *frame);

// Record the limit pointer of the previous stack frame.
void frame_set_previous_limit_pointer(frame_t *frame, size_t value);

// Returns the limit pointer of the previous stack frame.
size_t frame_get_previous_limit_pointer(frame_t *frame);

// Record the flags of the previous stack frame.
void frame_set_previous_flags(frame_t *frame, value_t flags);

// Returns the flags of the previous stack frame.
value_t frame_get_previous_flags(frame_t *frame);

// Sets the code block this frame is executing.
void frame_set_code_block(frame_t *frame, value_t code_block);

// Returns the code block this frame is executing.
value_t frame_get_code_block(frame_t *frame);

// Sets the program counter for this frame.
void frame_set_previous_pc(frame_t *frame, size_t pc);

// Returns the program counter for this frame.
size_t frame_get_previous_pc(frame_t *frame);

// Sets the mapping from parameters to argument indices for this frame.
void frame_set_argument_map(frame_t *frame, value_t map);

// Returns the mapping from parameter to argument indices for this frame.
value_t frame_get_argument_map(frame_t *frame);

// Pushes a value onto this stack frame. The returned value will always be
// success except on bounds check failures in soft check failure mode where it
// will be OutOfBounds.
value_t frame_push_value(frame_t *frame, value_t value);

// Pops a value off this stack frame. Bounds checks whether there is a value to
// pop and in soft check failure mode returns an OutOfBounds condition if not.
value_t frame_pop_value(frame_t *frame);

// Returns the index'th value counting from the top of this stack. Bounds checks
// whether there is a value to return and in soft check failure mode returns an
// OutOfBounds condition if not.
value_t frame_peek_value(frame_t *frame, size_t index);

// Returns the value of the index'th parameter.
value_t frame_get_argument(frame_t *frame, size_t param_index);

// Returns the value of the index'th argument in evaluation order.
value_t frame_get_raw_argument(frame_t *frame, size_t eval_index);

// Returns the index'th argument to an invocation using the given tags in sorted
// tag order from the given frame. The argument is pending in the sense that
// the call hasn't actually been performed yet, the arguments are just on the
// top of the stack.
value_t frame_get_pending_argument_at(frame_t *frame, value_t self, int64_t index);

// Sets the value of the index'th parameter. This is kind of a dubious thing to
// be doing so avoid if at all possible.
void frame_set_argument(frame_t *frame, size_t param_index, value_t value);

// Returns the value of the index'th local variable in this frame.
value_t frame_get_local(frame_t *frame, size_t index);

// Is this frame synthetic, that is, does it correspond to an activation
// inserted by the runtime and not caused by an invocation?
bool frame_has_flag(frame_t *frame, frame_flag_t flag);

// Returns a pointer to the bottom of the stack piece on which this frame is
// currently executing.
value_t *frame_get_stack_piece_bottom(frame_t *frame);

// Returns a pointer to te top of the stack piece which this frame is currently
// executing.
value_t *frame_get_stack_piece_top(frame_t *frame);

// Points the frame struct to the next frame on the stack without writing to the
// stack.
void frame_walk_down_stack(frame_t *frame);

// Creates a plain barrier in the given frame with the given handler.
void frame_push_barrier(frame_t *frame, value_t handler);

// Allocates a contiguous chunk of this frame.
value_array_t frame_alloc_array(frame_t *frame, size_t size);

// Allocates a derived object section within the current frame, returning the
// derived object pointer.
value_t frame_alloc_derived_object(frame_t *frame, genus_descriptor_t *desc);

// Pops the derived object currently at the top off the stack off.
void frame_destroy_derived_object(frame_t *frame, genus_descriptor_t *desc);


/// ### Frame iterator
///
/// Utility for scanning through the frames in a stack without mutating the
/// stack.

// Data used while iterating the frames of a stack.
typedef struct {
  // The currently active frame.
  frame_t current;
} frame_iter_t;

// Initializes the given frame iterator. After this call the current frame will
// be the one passed as an argument.
void frame_iter_init_from_frame(frame_iter_t *iter, frame_t *frame);

// Returns the current frame. The result is well-defined until the first call to
// frame_iter_advance that returns false.
frame_t *frame_iter_get_current(frame_iter_t *iter);

// Advances the iterator to the next frame. Returns true iff advancing was
// successful, in which case frame_iter_get_current can be called to get the
// next frame.
bool frame_iter_advance(frame_iter_t *iter);


/// ## Stack
///
/// Most of the execution works with individual stack pieces, not the stack, but
/// but whenever we need to manipulate the pieces, create new ones for instance,
/// that's the stack's responsibility.
///
/// The stack is also responsible for the _barriers_ that are threaded all the
/// way through the stack pieces. A barrier is a section of a stack piece that
/// must be processed before the scope that created the barrier exits. During
/// normal execution the scope's code ensures that the barrier is processed
/// whereas when escaping the barriers are traversed and processed from the
/// outside. This is how ensure blocks are implemented: a barrier is pushed that
/// when processed executes the code corresponding to the ensure block.
///
/// Each barrier contains a pointer to the next one so the stack only needs to
/// keep track of the top one and it'll point to the others. That's convenient
/// because it means that all the space required to hold the barriers can be
/// stack allocated.

static const size_t kStackSize = HEAP_OBJECT_SIZE(3);
static const size_t kStackTopPieceOffset = HEAP_OBJECT_FIELD_OFFSET(0);
static const size_t kStackDefaultPieceCapacityOffset = HEAP_OBJECT_FIELD_OFFSET(1);
static const size_t kStackTopBarrierOffset = HEAP_OBJECT_FIELD_OFFSET(2);

// The top stack piece of this stack.
ACCESSORS_DECL(stack, top_piece);

// The default capacity of the stack pieces that make up this stack.
INTEGER_ACCESSORS_DECL(stack, default_piece_capacity);

// The current top barrier.
ACCESSORS_DECL(stack, top_barrier);

// Allocates a new frame on this stack. If allocating fails, for instance if a
// new stack piece is required and we're out of memory, a condition is returned.
// The arg map array is used to determine how many arguments should be copied
// from the old to the new segment in the case where we have to create a new
// one. It is passed as a value rather than a size because the value is easily
// available wherever this gets called and it saves reading the size in the
// common case where no new segment gets allocated. If you're _absolutely_ sure
// no new segment will be allocated you can pass null for the arg map.
value_t push_stack_frame(runtime_t *runtime, value_t stack, frame_t *frame,
    size_t frame_capacity, value_t arg_map);

// Opens the top stack piece of the given stack into the given frame.
frame_t open_stack(value_t stack);


/// ### Stack barrier
///
/// Helper type that encapsulates a region of the stack that holds a stack
/// barrier.

static const int32_t kStackBarrierSize = 3;
static const size_t kStackBarrierHandlerOffset = 0;
static const size_t kStackBarrierNextPieceOffset = 1;
static const size_t kStackBarrierNextPointerOffset = 2;


/// ### Barrier iterator
///
/// Utility for scanning through the barriers on a stack without modifying them.

// Holds the barrier iteration state.
typedef struct {
  value_t current;
} barrier_iter_t;

// Initializes the given barrier iterator, returning the first barrier.
value_t barrier_iter_init(barrier_iter_t *iter, frame_t *frame);

// Advances the iterator to the next barrier, downwards towards the bottom of
// the call stack. Returns the next barrier.
value_t barrier_iter_advance(barrier_iter_t *iter);


/// ## Escape
///
/// An escape is a scoped object that encapsulates the execution state at a
/// a point immediately after the place where the escape was created
/// (_captured_). When fired, the escape restores the state effectively
/// returning, possibly non-locally, to immediately after the capture point.
/// This is how constructs like return, break, continue, etc., is implemented.
///
/// ### Escape home
///
/// Since an escape is scoped, that is, it is invalidated as soon as the scope
/// that created it exits, most of its state can be stored on the stack as a
/// derived escape section object. The escape itself is just a pointer to its
/// state on the stack.
///
/// An escape uses two stack regions: the state to return to when fired and a
/// barrier which ensures that the escape object is invalidated however the
/// scope exits.
///
/// ### Escaping through barriers
///
/// When an escape is fired we can't jump directly back to where it was created,
/// we first have to execute all barriers between the current point of execution
/// and the escape's origin. This is done by "parking" the interpreter at a
/// bytecode and never advancing the program counter. At each turn around the
/// interpreter the same instruction is executed which fetches the next barrier
/// comparing it with the escape's destination, and if the barrier is above it
/// the associated handler is executed.
///
/// ### Returning multiple times through the same escape
///
/// To a first approximation escapes can only be fired once because firing an
/// escape causes execution to exit the scope that created it -- that's the
/// whole point -- which causes the escape to be invalidated. However, that is
/// actually not the whole story. Since escapes don't jump directly out but
/// process barriers first it is possible for a barrier to interrupt the escape
/// by firing a _different_ escape that doesn't escape as far out as the
/// original did. So a more accurate way to think of the escape process (though
/// not necessarily more useful in practice) is not as a direct jump to the
/// origin but as an _attempt_ to escape there, an attempt that can be stopped
/// by barriers along the way. The same escape can be attempted as many times
/// as you want -- as long as it doesn't succeed, once it's succeeded it is
/// immediately invalidated.
///
/// Is this the behavior you want? I think it probably is. In any case, it falls
/// out if you take a straightforward formulation of escapes and barriers so
/// to get a different behavior you'd have to either change how escapes or
/// barriers work more generally, or have a special case here which would make
/// them less orthogonal.

static const size_t kEscapeSize = HEAP_OBJECT_SIZE(1);
static const size_t kEscapeSectionOffset = HEAP_OBJECT_FIELD_OFFSET(0);

// The escape section that contains the data for this escape object. Becomes
// nothing when the escape is killed.
ACCESSORS_DECL(escape, section);


/// ## Lambda
///
/// Long-lived (or potentially long lived) code objects that are visible to the
/// surface language. Because a lambda can live indefinitely there's no
/// guarantee that it won't be called after the scope that defines its outer
/// state has exited. Hence, pessimistically capture all their outer state on
/// construction.

static const size_t kLambdaSize = HEAP_OBJECT_SIZE(2);
static const size_t kLambdaMethodsOffset = HEAP_OBJECT_FIELD_OFFSET(0);
static const size_t kLambdaCapturesOffset = HEAP_OBJECT_FIELD_OFFSET(1);

// Returns the method space where the methods supported by this lambda live.
ACCESSORS_DECL(lambda, methods);

// Returns the array of captured outers for this lambda.
ACCESSORS_DECL(lambda, captures);

// Returns the index'th outer value captured by the given lambda.
value_t get_lambda_capture(value_t self, size_t index);


/// ## Block
///
/// Code objects visible at the surface level which only live as long as the
/// scope that defined them. Because blocks can only be executed while the scope
/// is still alive they can access their outer state through the stack through
/// a mechanism called _refraction_. Whenever a block is created a derived
/// section object is allocated on the stack, the block's _home_.
///
//%                                  (block 1)
//%          :            :         +----------+
//%          +============+    +--- | section  |
//%     +--- :            :    |    +----------+
//%     |    :  block 1   :    |
//%     |    :  section   : <--+
//%     |    :            :
//%     |    +============+
//%     |    :            :
//%     |    :   locals   :
//%     |    :            :
//%     +--> +============+
//%          |   subject  | ---+
//%          +------------+    |
//%          :  possibly  :    |
//%          :    many    :    |     (block 2)
//%          :   frames   :    +--> +----------+
//%          +============+         | section  |
//%     +--- :            :    +--- +----------+
//%     |    :  block 2   :    |
//%     |    :  section   : <--+
//%     |    :            :
//%     |    +============+
//%     |    :            :
//%     |    :   locals   :
//%     |    :            :
//%     +--> +============+
//%          :            :
///
/// To access say a local variable in an enclosing scope from a block you follow
/// the block's pointer back to its home section. There you can get the frame
/// pointer of the frame that created the block which is all you need to access
/// state in that frame. To access scopes yet further down the stack, in the
/// case where you have nested blocks within blocks, you can peel off the scopes
/// one at a time by first going through the block itself into the frame that
/// created it, then the next enclosing block which will be the subject of that
/// frame, and so on until all the layers of blocks scopes have been peeled off.
/// This process of going through (potentially) successive blocks' originating
/// scopes is what is referred to as refraction.
///
/// Because blocks don't need to capture state they're cheap to create, just
/// a derived object allocation and a small heap object that wraps the derived
/// object pointer such that it can be killed when the scope exits.

static const size_t kBlockSize = HEAP_OBJECT_SIZE(1);
static const size_t kBlockSectionOffset = HEAP_OBJECT_FIELD_OFFSET(0);

// The section on the stack where this block's state lives.
ACCESSORS_DECL(block, section);

// Returns an incomplete frame that provides access to arguments and locals for
// the frame that is located block_depth scopes outside the given block.
void get_refractor_refracted_frame(value_t self, size_t block_depth,
    struct frame_t *frame_out);


// --- B a c k t r a c e ---

static const size_t kBacktraceSize = HEAP_OBJECT_SIZE(1);
static const size_t kBacktraceEntriesOffset = HEAP_OBJECT_FIELD_OFFSET(0);

// The array buffer of backtrace entries.
ACCESSORS_DECL(backtrace, entries);

// Creates a new backtrace by traversing the stack starting from the given
// frame.
value_t capture_backtrace(runtime_t *runtime, frame_t *frame);


// --- B a c k t r a c e   e n t r y ---

static const size_t kBacktraceEntrySize = HEAP_OBJECT_SIZE(2);
static const size_t kBacktraceEntryInvocationOffset = HEAP_OBJECT_FIELD_OFFSET(0);
static const size_t kBacktraceEntryOpcodeOffset = HEAP_OBJECT_FIELD_OFFSET(1);

// The invocation record for this entry.
ACCESSORS_DECL(backtrace_entry, invocation);

// The opcode that caused this entry to be created.
ACCESSORS_DECL(backtrace_entry, opcode);

// Print the given invocation map on the given context. This is really an
// implementation detail of how backtrace entries print themselves but it's
// tricky enough that it makes sense to be able to test as a separate thing.
void backtrace_entry_invocation_print_on(value_t invocation, int32_t opcode,
    print_on_context_t *context);

// Creates a backtrace entry from the given stack frame. If no entry can be
// created nothing is returned.
value_t capture_backtrace_entry(runtime_t *runtime, frame_t *frame);


/// ## Task
///
/// A task is a point of synchronous execution. That is, each task can exist
/// alongside others and each have their execution state, but only one can ever
/// execute at any one time.

static const size_t kTaskSize = HEAP_OBJECT_SIZE(2);
static const size_t kTaskProcessOffset = HEAP_OBJECT_FIELD_OFFSET(0);
static const size_t kTaskStackOffset = HEAP_OBJECT_FIELD_OFFSET(1);

// The process that contains this task.
ACCESSORS_DECL(task, process);

// The stack on which this task executes.
ACCESSORS_DECL(task, stack);


/// ## Process
///
/// A process is the unit of asynchronous execution. Two processes execute
/// (potentially) independently of each other and can only affect each other
/// through explicitly sending asynchronous messages.

typedef struct pending_atomic_t pending_atomic_t;
typedef struct incoming_request_state_t incoming_request_state_t;
typedef struct exported_service_capsule_t exported_service_capsule_t;

// The number of pending results that we'll let buffer in an airlock.
#define kProcessAirlockCompleteBufferSize 16

// The number of incoming requests we'll let buffer in an airlock.
#define kProcessAirlockIncomingBufferSize 16

// Data allocated in the C heap which is accessible from other threads
// throughout the lifetime of the process. This is how asynchronous interaction
// with a process is implemented: other threads can put data into the airlock
// and the process will take it out when it wants.
typedef struct {
  // The runtime that contains the process.
  runtime_t *runtime;
  // How much space is available for foreign requests to be completed?
  native_semaphore_t foreign_vacancies;
  // Mutex that guards the complete request buffer.
  native_mutex_t complete_buffer_mutex;
  // Buffer that holds the state of completed requests.
  byte_t complete_buffer[BOUNDED_BUFFER_SIZE(kProcessAirlockCompleteBufferSize)];
  // The number of outstanding foreign requests whose results haven't been
  // delivered to their associated promise.
  size_t open_foreign_request_count;
  // Buffer that holds the state of incoming requests.
  byte_t incoming_buffer[BOUNDED_BUFFER_SIZE(kProcessAirlockIncomingBufferSize)];
  // How much room is left in the incoming request buffer?
  native_semaphore_t incoming_vacancies;
  // Mutex that guards the incoming request buffer.
  native_mutex_t incoming_buffer_mutex;
} process_airlock_t;

// Create and initialize a process airlock. Returns null if anything fails.
process_airlock_t *process_airlock_new(runtime_t *runtime);

// Notify the process that the request with the given state has completed.
void process_airlock_schedule_atomic(process_airlock_t *airlock,
    pending_atomic_t *result);

// If the given airlock has a pending atomic operation takes it, stores it in
// result_out, and returns true. If not returns false. Never blocks.
bool process_airlock_next_pending_atomic(process_airlock_t *airlock,
    pending_atomic_t **result_out);

// If the given airlock has a pending incoming request takes it, stores
// it in result_out, and returns true. If not returns false. Never blocks.
bool process_airlock_next_incoming(process_airlock_t *airlock,
    incoming_request_state_t **result_out);

// Notify the process that there is a request to an exported service incoming.
void process_airlock_schedule_incoming_request(process_airlock_t *airlock,
    incoming_request_state_t *request);

// Dispose the airlock's state appropriately, including deleting the airlock
// value.
bool process_airlock_destroy(process_airlock_t *airlock);

static const size_t kProcessSize = HEAP_OBJECT_SIZE(4);
static const size_t kProcessWorkQueueOffset = HEAP_OBJECT_FIELD_OFFSET(0);
static const size_t kProcessRootTaskOffset = HEAP_OBJECT_FIELD_OFFSET(1);
static const size_t kProcessHashSourceOffset = HEAP_OBJECT_FIELD_OFFSET(2);
static const size_t kProcessAirlockPtrOffset = HEAP_OBJECT_FIELD_OFFSET(3);

// The work queue that holds tasks for this process.
ACCESSORS_DECL(process, work_queue);

// This process' root task, the task that is used to execute work from the
// queue.
ACCESSORS_DECL(process, root_task);

// This process' built-in hash source.
ACCESSORS_DECL(process, hash_source);

// This process' airlock structure.
ACCESSORS_DECL(process, airlock_ptr);

// Returns the airlock struct for the given process.
process_airlock_t *get_process_airlock(value_t process);

// A collection of values that make up a pending job.
typedef struct {
  // The code block to execute to run the job.
  value_t code;
  // An optional piece of data that is available to the code block.
  value_t data;
  // Optional promise to resolve with the result of running this job.
  value_t promise;
  // Optional promise that must be resolved before this job can be run.
  value_t guard;
} job_t;

#define kProcessWorkQueueWidth (sizeof(job_t) / sizeof(value_t))

// Initialize a job struct.
void job_init(job_t *job, value_t code, value_t data, value_t promise,
    value_t guard);

// Adds a job to the queue of work to perform for this process. The job struct
// is copied so it can be disposed immediately after this call.
value_t offer_process_job(runtime_t *runtime, value_t process, job_t *job);

// Returns the next scheduled code block to be executed on the given process.
// If there are no more work left a NotFound condition is returned.
value_t take_process_job(value_t process, job_t *job_out);

// Process any pending atomic requests.
value_t deliver_process_outstanding_pending_atomic(value_t process);

// Schedule any incoming requests in this process' airlock onto the worklist.
value_t deliver_process_incoming(runtime_t *runtime, value_t process);

// Returns true if there is no more work for this process to perform.
bool is_process_idle(value_t process);


/// ## Reified arguments
///
/// Reified arguments capture the tags and value passed to an invocation.

static const size_t kReifiedArgumentsSize = HEAP_OBJECT_SIZE(4);
static const size_t kReifiedArgumentsParamsOffset = HEAP_OBJECT_FIELD_OFFSET(0);
static const size_t kReifiedArgumentsValuesOffset = HEAP_OBJECT_FIELD_OFFSET(1);
static const size_t kReifiedArgumentsArgmapOffset = HEAP_OBJECT_FIELD_OFFSET(2);
static const size_t kReifiedArgumentsTagsOffset = HEAP_OBJECT_FIELD_OFFSET(3);

// The argument parameters from the method being called.
ACCESSORS_DECL(reified_arguments, params);

// The concrete argument values. The values must come in evaluation order, that
// is, the order in which they were passed on the stack by the caller, not the
// order of the parameters in the callee.
ACCESSORS_DECL(reified_arguments, values);

// The argument map passed by the caller. This gives, for each parameter index,
// the corresponding evaluation index.
ACCESSORS_DECL(reified_arguments, argmap);

// The call tags used by the caller to resolve the method. These need to be
// available in addition to the params in case the call uses extra arguments
// that the callee doesn't know about.
ACCESSORS_DECL(reified_arguments, tags);


/// ## Incoming request thunk
///
/// An incoming request thunk is all the data about an incoming request bundled
/// together.

static const size_t kIncomingRequestThunkSize = HEAP_OBJECT_SIZE(1);
static const size_t kIncomingRequestThunkRequestStatePtrOffset = HEAP_OBJECT_SIZE(0);

// Pointer to the C request state data.
ACCESSORS_DECL(incoming_request_thunk, request_state_ptr);


#endif // _PROCESS
