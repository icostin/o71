#ifndef _O71_H
#define _O71_H

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>


#if CHAR_BIT != 8
#error only platforms with 8-bit chars are supported
#endif

#define O71_INLINE static __inline

#if defined(_WIN32) || defined(__CYGWIN__)
#   define O71_LIB_EXPORT __declspec(dllexport)
#   define O71_LIB_IMPORT __declspec(dllimport)
#   define O71_LOCAL
#elif __GNUC__ >= 4
#   define O71_LIB_IMPORT __attribute__ ((visibility ("default")))
#   define O71_LIB_EXPORT __attribute__ ((visibility ("default")))
#   define O71_LOCAL __attribute__ ((visibility ("hidden")))
#else //if defined(__GNUC__)
#   define O71_LIB_IMPORT
#   define O71_LIB_EXPORT
#   define O71_LOCAL
#endif

#if O71_STATIC
#define O71_API
#elif O71_LIB_BUILD
#define O71_API O71_LIB_EXPORT
#else
#define O71_API O71_LIB_IMPORT
#endif

#define O71_STEPS_MAX INT32_MAX

#define O71_IS_REF_TO_SINT(_ref) (((_ref) & 1))
#define O71_IS_REF_TO_MO(_ref) (!((_ref) & 1))
#define O71_MOX_TO_REF(_mox) ((o71_ref_t) (_mox) << 1)
#define O71_REF_TO_MOX(_ref) ((o71_obj_index_t) ((_ref) >> 1))
#define O71_SINT_TO_REF(_val) (((_val) << 1) | 1)
#define O71_REF_TO_SINT(_ref) ((_ref) >> 1)

#define O71R_NULL (O71_MOX_TO_REF(O71X_NULL))
#define O71R_NULL_CLASS (O71_MOX_TO_REF(O71X_NULL_CLASS))
#define O71R_CLASS_CLASS (O71_MOX_TO_REF(O71X_CLASS_CLASS))
#define O71R_STRING_CLASS (O71_MOX_TO_REF(O71X_STRING_CLASS))
#define O71R_SMALL_INT_CLASS (O71_MOX_TO_REF(O71X_SMALL_INT_CLASS))
#define O71R_NATIVE_FUNCTION_CLASS \
    (O71_MOX_TO_REF(O71X_NATIVE_FUNCTION_CLASS))
#define O71R_SCRIPT_FUNCTION_CLASS \
    (O71_MOX_TO_REF(O71X_SCRIPT_FUNCTION_CLASS))
#define O71R_INT_ADD_FUNC (O71_MOX_TO_REF(O71X_INT_ADD_FUNC))

#define O71_BAG_ARRAY 0
#define O71_BAG_RBTREE 1

enum o71_status_e
{
    O71_OK = 0,
    O71_LESS = 0,
    O71_MORE = 1,
    O71_EQUAL,

    O71_PENDING,
    O71_EXC,

    O71_MISSING,

    O71_NO_MEM,
    O71_MEM_LIMIT,
    O71_ARRAY_LIMIT,

    O71_NOT_MEM_OBJ_REF,
    O71_UNUSED_MEM_OBJ_SLOT,
    O71_MODEL_MISMATCH,
    O71_OBJ_DESTRUCTING,
    O71_BAD_FUNC_REF,
    O71_BAD_OPCODE,
    O71_BAD_OPERAND_INDEX,
    O71_BAD_CONST_INDEX,
    O71_BAD_VAR_INDEX,
    O71_BAD_INSN_INDEX,
    O71_BAD_ARG_COUNT,
    O71_CMP_ERROR,
    O71_BAD_STRING_REF,
    O71_BAD_RO_STRING_REF,

    O71_FATAL,
    O71_MEM_CORRUPTED = O71_FATAL,
    O71_REF_COUNT_OVERFLOW,
    O71_BUG,
    O71_TODO,
};

enum o71_builtin_obj_index_e
{
    O71X_NULL = 0,
    O71X_NULL_CLASS,
    O71X_CLASS_CLASS,
    O71X_STRING_CLASS,
    O71X_SMALL_INT_CLASS,
    O71X_NATIVE_FUNCTION_CLASS,
    O71X_SCRIPT_FUNCTION_CLASS,
    O71X_INT_ADD_FUNC,

    O71X__COUNT
};

enum o71_opcode_e
{
    // vx - local var index
    // cx - const index
    O71O_NOP,
    O71O_INIT, // init dest_vx, src_cx - moves a constant to a local variable
    O71O_GET_METHOD, // get_method dest_vx, obj_vx, name_istr_vx

    O71O__CHFLOW, // following are opcodes that can change the flow
    O71O_CALL = O71O__CHFLOW, // call dest_vx, func_vx, arg_n, arg0_vx, ... arg<arg_n - 1>_vx
    O71O__NO_FALL,
    O71O_RETURN = O71O__NO_FALL, // ret value_vx
    O71O_JUMP,
};

#define O71M_INVALID            0
#define O71M_SMALL_INT          1
#define O71M_MEM_OBJ            2
#define O71M_CLASS              (4 | O71M_MEM_OBJ)
#define O71M_STRING             (8 | O71M_MEM_OBJ)
#define O71M_FUNCTION           (0x10 | O71M_MEM_OBJ)
#define O71M_SCRIPT_FUNCTION    (0x20 | O71M_FUNCTION)
#define O71M_EXCEPTION          (0x40 | O71M_MEM_OBJ)

typedef enum o71_status_e o71_status_t;
typedef intptr_t o71_ref_count_t;
typedef struct o71_class_s o71_class_t;
typedef struct o71_dyn_obj_s o71_dyn_obj_t;
typedef struct o71_exc_handler_s o71_exc_handler_t;
typedef struct o71_exe_ctx_s o71_exe_ctx_t;
typedef struct o71_field_desc_s o71_field_desc_t;
typedef struct o71_flow_s o71_flow_t;
typedef struct o71_function_class_s o71_function_class_t;
typedef struct o71_function_s o71_function_t;
typedef struct o71_insn_s o71_insn_t;
typedef struct o71_kv_s o71_kv_t;
typedef struct o71_kvbag_s o71_kvbag_t;
typedef struct o71_kvnode_s o71_kvnode_t;
typedef struct o71_kvbag_loc_s o71_kvbag_loc_t;
typedef struct o71_mem_obj_s o71_mem_obj_t;
typedef struct o71_script_exe_ctx_s o71_script_exe_ctx_t;
typedef struct o71_script_function_s o71_script_function_t;
typedef struct o71_string_s o71_string_t;
typedef struct o71_struct_s o71_struct_t;
typedef struct o71_world_s o71_world_t;
typedef uintptr_t o71_obj_index_t;

/* o71_ref_t ****************************************************************/
/**
 *  Reference to an object in the scripting world.
 *  +---------+-+
 *  |nnnnnnnnn|1| - small int nnnnnnn
 *  +---------+-+
 *
 *  +-------+-+-+
 *  |nnnnnnn|0|0| - ref counted memory object
 *  +-------+-+-+
 *
 *  +-------+-+-+
 *  |nnnnnnn|1|0| - permanent memory object
 *  +-------+-+-+
 */
typedef uintptr_t o71_ref_t;

typedef o71_status_t (* o71_realloc_f)
    (
        void * * data_pp,
        size_t old_size,
        size_t new_size,
        void * realloc_context
    );

typedef o71_status_t (* o71_finish_f)
    (
        o71_world_t * world_p,
        o71_ref_t obj_r
    );

/* o71_get_field_f **********************************************************/
/**
 *  Get field function pointer.
 */
typedef o71_status_t (* o71_get_field_f)
    (
        o71_flow_t * flow_p, 
        o71_ref_t obj_r, 
        o71_ref_t field_r, 
        o71_ref_t * value_p
    );

/* o71_set_field_f **********************************************************/
/**
 *  Set field function pointer.
 *  @warning The value is supposed to have already its reference count 
 *      incremented 
 *  because this function "borrows" the reference from its caller.
 */
typedef o71_status_t (* o71_set_field_f)
    (
        o71_flow_t * flow_p,
        o71_ref_t obj_r,
        o71_ref_t field_r,
        o71_ref_t value
    );

/* o71_call_f ***************************************************************/
/**
 *  Prepares the execution flow to run a function call.
 *  @retval O71_OK 
 *      function call executed successfully
 *  @retval O71_PENDING
 *      a new execution context pushed on the stack waiting to run
 *  @retval O71_EXC
 *      an exception was thrown
 */
typedef o71_status_t (* o71_call_f)
    (
        o71_flow_t * flow_p,
        o71_ref_t func_r,
        o71_ref_t * arg_ra,
        size_t arg_n
    );

typedef o71_status_t (* o71_run_f)
    (
        o71_flow_t * flow_p
    );

/* o71_cmp_f ****************************************************************/
/**
 *  Compares two objects given their references
 */
typedef o71_status_t (* o71_cmp_f) 
    (
        o71_world_t * world_p,
        o71_ref_t a_r,
        o71_ref_t b_r,
        void * ctx
    );

struct o71_mem_obj_s
{
    union
    {
        o71_ref_t class_r;
        size_t object_size; // during destroy phase (o71_cleanup()) the class
        // is replaced with the object size
    };
    union
    {
        o71_ref_count_t ref_n;
        o71_obj_index_t enc_destroy_next_x;
    };
};

struct o71_kvbag_s
{
    union
    {
        o71_kv_t * kv_a;
        /**< array of key values sorted by key. the allocated size is
         *   log2_rounded_up(n);
         *   this is only used if n <= (1 << aexp) where aexp is a constant
         *   passed to all kvbag functions */
        o71_kvnode_t * tree_p;
        /**< when n > (1 << aexp) this holds the root of the red/black tree */
    };
    uint8_t n; // number of used entries in kv_a
    uint8_t m; // allocated size of array of key-values (must be power of two)
    uint8_t l; // limit size for array mode (must be a power of two)
    uint8_t mode; // 0 - array; 1 - red/black tree
};

struct o71_class_s
{
    o71_mem_obj_t hdr;
    o71_ref_t * super_a;
    o71_finish_f finish;
    o71_get_field_f get_field;
    o71_set_field_f set_field;
    o71_kvbag_t method_bag; // instance method bag
    size_t object_size; // instance size
    size_t super_n;
    uint32_t model;
};

#define O71_SM_MODIFIABLE 0
#define O71_SM_READ_ONLY 1
#define O71_SM_INTERN 2

struct o71_string_s
{
    o71_mem_obj_t hdr;
    uint8_t * a;
    size_t n;
    size_t m;
    uint8_t mode;
};

struct o71_struct_s
{
    o71_mem_obj_t hdr;
    o71_ref_t a[0];
};

struct o71_field_desc_s
{
    o71_ref_t name_r;
    unsigned int index;
};

struct o71_struct_class_s
{
    o71_class_t cls;
    o71_field_desc_t * field_desc_a;
    size_t field_n;
    size_t offset; 
    /*< offset from start of struct instance where the array of fields is
     *  located; this allows us to reuse the struct get/set field functions
     *  for other classes
     */
};

struct o71_kv_s
{
    o71_ref_t key_r;
    o71_ref_t value_r;
};

struct o71_kvnode_s
{
    uintptr_t clr[2]; // color+left_node_ptr / right_node_ptr
    o71_kv_t kv;
};

struct o71_kvbag_loc_s
{
    union
    {
        struct
        {
            unsigned int index;
        } array;
        struct
        {
            o71_kvnode_t * node_a[0x40];
            uint8_t side_a[0x40];
            unsigned int last_x;
        } rbtree;
    };
    uint8_t match;
    o71_status_t cmp_error;
};

struct o71_dyn_obj_s
{
    o71_mem_obj_t hdr;
    union
    {
        o71_kv_t * kv_a;
    } opt;
    size_t opt_field_n;
    o71_ref_t fix_field_a[0];
};

struct o71_function_s
{
    o71_mem_obj_t hdr;
    o71_call_f call;
    o71_run_f run;
};

struct o71_exe_ctx_s
{
    o71_exe_ctx_t * caller_p;
    o71_function_t * func_p;
    size_t size;
};

struct o71_flow_s
{
    o71_world_t * world_p;
    o71_exe_ctx_t * exe_ctx_p;
    o71_ref_t value_r;
    o71_ref_t exc_r;
    unsigned int depth;
    unsigned int crt_steps;
    unsigned int max_steps;
    unsigned int flow_id;
};

struct o71_insn_s
{
    uint8_t opcode;
    uint8_t exc_chain_x; // exception chain index
    uint16_t opnd_x; // first operand index
    // insn operands are of the form: vx0, ..vx<N-1>, [cx], [nvx], [vx<N>..vx<N+nvx>]
};

struct o71_exc_handler_s
{
    o71_ref_t exc_type_r;
    size_t insn_x;
};

struct o71_script_function_s
{
    o71_function_t func;
    o71_insn_t * insn_a;
    uint32_t * opnd_a;
    uint32_t * arg_xa;
    o71_ref_t * const_ra;
    o71_exc_handler_t * exc_handler_a;
    size_t * exc_chain_start_xa;

    size_t var_n;
    size_t arg_n;
    size_t insn_n;
    size_t insn_m;
    size_t opnd_n;
    size_t opnd_m;
    size_t const_n;
    size_t const_m;
    size_t exc_handler_n;
    size_t exc_chain_n;

    uint8_t valid;
};

struct o71_script_exe_ctx_s
{
    o71_exe_ctx_t exe_ctx;
    uint32_t insn_x;
    uint8_t calling;
    o71_ref_t var_ra[0];
};

struct o71_world_s
{
    union
    {
        o71_mem_obj_t * * mem_obj_pa;
        void * * obj_pa;
        uintptr_t * enc_next_free_xa; // values are (index * 2 + 1) to be distinguished from used entries (pointers to object - aligned to at least 4)
    };
    size_t obj_n;
    o71_obj_index_t free_list_head_x;
    o71_obj_index_t destroy_list_head_x; // chained using ~obj_p->ref_n

    o71_kvbag_t istr_bag;
    size_t mem_usage;
    size_t mem_limit;
    size_t mem_peak;

    /*  realloc  */
    /**
     *  @retval O71_OK realloc successful
     *  @retval O71_NO_MEM
     *  @retval O71_MEM_LIMIT
     *  @retval O71_MEM_CORRUPTED
     *  @retval O71_BUG
     *  @retval O71_TODO
     */
    o71_realloc_f realloc;
    void * realloc_context;

    o71_flow_t root_flow;

    o71_mem_obj_t null_object;
    o71_class_t null_class;
    o71_class_t class_class;
    o71_class_t string_class;
    o71_class_t small_int_class;
    o71_class_t native_function_class;
    o71_class_t script_function_class;
    o71_function_t int_add_func;

    unsigned int flow_id_seed;
    uint8_t cleaning;
};

/* o71_status_name **********************************************************/
O71_API char const * o71_status_name
(
    o71_status_t status
);

/* o71_world_init ***********************************************************/
/**
 *  @retval O71_OK
 *  @retval O71_NO_MEM
 *  @retval O71_MEM_LIMIT
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
O71_API o71_status_t o71_world_init
(
    o71_world_t * world_p,
    o71_status_t (* realloc)
        (
            void * * data_pp,
            size_t old_size,
            size_t new_size,
            void * realloc_context
        ),
    void * realloc_context,
    size_t mem_limit
);

/* o71_world_finish *********************************************************/
/**
 *  @retval O71_OK
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
O71_API o71_status_t o71_world_finish
(
    o71_world_t * world_p
);

/* o71_cstring **************************************************************/
/**
 *  Produces a string object from the given C string.
 *  The data is allocated.
 *  @retval O71_OK
 *  @retval O71_NO_MEM
 *  @retval O71_MEM_LIMIT
 *  @retval O71_ARRAY_LIMIT too many objects
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
O71_API o71_status_t o71_cstring
(
    o71_world_t * world_p,
    o71_ref_t * str_rp,
    char const * cstr_a
);

/* o71_rocs *****************************************************************/
/**
 *  Produces a read-only string using the given static constant C string.
 *  @retval O71_OK
 *  @retval O71_NO_MEM
 *  @retval O71_MEM_LIMIT
 *  @retval O71_ARRAY_LIMIT too many objects
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
O71_API o71_status_t o71_rocs
(
    o71_world_t * world_p,
    o71_ref_t * str_rp,
    char const * cstr_a
);

/* o71_str_freeze ***********************************************************/
O71_API o71_status_t o71_str_freeze
(
    o71_world_t * world_p,
    o71_ref_t str_r
);

/* o71_str_intern ***********************************************************/
O71_API o71_status_t o71_str_intern
(
    o71_world_t * world_p,
    o71_ref_t str_r,
    o71_ref_t * intern_str_rp
);

/* o71_ics ******************************************************************/
/**
 *  Produces an intern string from a static constant C string.
 */
O71_API o71_status_t o71_ics
(
    o71_world_t * world_p,
    o71_ref_t * str_rp,
    char const * cstr_a
);

/* o71_sfunc_create *********************************************************/
/**
 *  Creates an empty scripting function.
 */
O71_API o71_status_t o71_sfunc_create
(
    o71_world_t * world_p,
    o71_ref_t * sfunc_rp,
    uint32_t arg_n
);

/* o71_sfunc_validate *******************************************************/
/**
 *  Validates a scripting function to mark it as ready for execution.
 *  @param world_p [in]
 *      the world sfunc lives in
 *  @param sfunc_p [in, out]
 *      function to be validated
 *  @retval O71_OK 
 *      function is valid; var_n is updated to reflect the number of 
 *      variables needed
 *  @retval O71_BAD_OPCODE
 *      encountered an instruction with an invalid opcode
 *  @retval O71_BAD_OPERAND_INDEX
 *      encountered an instruction with an invalid operand index
 *  @retval O71_BAD_CONST_INDEX
 *      encountered an instruction with an invalid const index
 *  @retval O71_BAD_VAR_INDEX
 *      encountered an instruction with an invalid var index
 *  @retval O71_BAD_INSN_INDEX
 *      encountered an instruction with an invalid insn index
 */
O71_API o71_status_t o71_sfunc_validate
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p
);

/* o71_sfunc_append_init ****************************************************/
/**
 *  Appends an INIT instruction.
 *  @param const_r [in]
 *      constant reference (just the reference is constant, the object is
 *      free to change)
 *  @note @a const_r will get its ref count incremented by this function as
 *      it will store a permanent reference to the const
 */
O71_API o71_status_t o71_sfunc_append_init
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t dest_vx,
    o71_ref_t const_r
);

/* o71_sfunc_append_ret *****************************************************/
/**
 *  Appends a RET instruction.
 */
O71_API o71_status_t o71_sfunc_append_ret
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t value_vx
);

/* o71_sfunc_append_call ****************************************************/
/**
 *  Appends a CALL instruction.
 *  @param world_p [in]
 *      scripting world
 *  @param sfunc_p [in, out]
 *      function to modify
 *  @param func_vx [in]
 *      index of variable that will contain at run-time a reference to the
 *      function to call
 *  @param arg_n [in]
 *      number of arguments to pass to that function
 *  @param arg_vxap [out]
 *      receives the pointer to the array of @a arg_n items where the caller
 *      can fill in the indexes of variables to be passed as arguments
 */
O71_API o71_status_t o71_sfunc_append_call
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t dest_vx,
    uint32_t func_vx,
    uint32_t arg_n,
    uint32_t * * arg_vxap
);

/* o71_sfunc_append_get_method **********************************************/
/**
 *  Appends a GET_METHOD instruction.
 */
O71_API o71_status_t o71_sfunc_append_get_method
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t dest_vx,
    uint32_t obj_vx,
    uint32_t name_istr_vx
);

/* o71_check_mem_obj_ref ****************************************************/
/**
 *  @retval O71_OK ref points to a valid memory object
 *  @retval O71_NOT_MEM_OBJ_REF ref does point in the memory object space
 *  @retval O71_UNUSED_MEM_OBJ_SLOT ref points to an unused memory object slot
 */
O71_API o71_status_t o71_check_mem_obj_ref
(
    o71_world_t * world_p,
    o71_ref_t obj_r
);

/* o71_mem_obj ******************************************************************/
O71_INLINE void * o71_mem_obj
(
    o71_world_t * world_p,
    o71_ref_t mem_obj_r
)
{
    return world_p->obj_pa[O71_REF_TO_MOX(mem_obj_r)];
}

/* o71_class ****************************************************************/
/**
 *  @retval NULL invalid reference to an unused memory object slot
 */
O71_API o71_class_t * o71_class
(
    o71_world_t * world_p,
    o71_ref_t obj_r
);

/* o71_model ****************************************************************/
/**
 *  @returns model of object pointed by the reference
 *  @retval O71M_INVALID reference points to an unused memory object slot
 */
O71_API uint32_t o71_model
(
    o71_world_t * world_p,
    o71_ref_t obj_r
);

/* o71_cleanup **************************************************************/
/**
 *  Destroys the chain of objects in the destroy chain.
 * can return any error status as relayed from the finish functions of 
 * destroyed objects.
 */
O71_API o71_status_t o71_cleanup
(
    o71_world_t * world_p
);

/* o71_ref ******************************************************************/
/**
 *  Increments the ref count of an object.
 *  @note on release this always returns O71_OK
 *  @retval O71_OK 
 *      ref count incremented
 *      this is the only code returned by release builds
 *  @retval O71_REF_COUNT_OVERFLOW
 *      adding a reference causes int overflow; 
 *      this should only happen due to a bug from some component;
 *      this code can be returned only in checked or debug builds
 *  @retval O71_OBJ_DESTRUCTING
 *      attempt to increase the ref count of an object from the destroy chain;
 *      this code can be returned only in checked or debug builds
 *  @retval O71_UNUSED_MEM_OBJ_SLOT 
 *      bad reference to unused memory object slot;
 *      thic code can be returned only in checked or debug builds
 */
O71_API o71_status_t o71_ref
(
    o71_world_t * world_p,
    o71_ref_t obj_r
);

/* o71_deref ****************************************************************/
/**
  * Decrements the ref count of an object and if no refs are left then it 
  * adds the object to the destroy chain then cleans up the destroy chain.
  * During the call to finish an object, the objects referenced by the finishing
  * object will lose a reference thus they may become part of the destroy chain.
  * @retval O71_OK
  *     object dereferenced ok
  * @retval O71_UNUSED_MEM_OBJ_SLOT
  *     bad reference to unused memory object slot;
  *     this code can be returned only in checked or debug builds
  * @retval O71_MEM_CORRUPTED
  *     allocator detects some corurption while freeing memory for destroyed
  *     objects
  * @retval O71_BUG
  *     some assertion failed
  *     this code can be returned only in checked or debug builds
  * @retval other
  *     the error returned by some object finalizer callback
  **/
O71_API o71_status_t o71_deref
(
    o71_world_t * world_p,
    o71_ref_t obj_r
);

/* o71_prep_call ************************************************************/
/**
 *  Calls a function object.
 *  @retval O71_OK
 *      function executed and the result is stored in flow_p->value_r
 *  @retval O71_PENDING
 *      a new execution context has been pushed into the chain from
 *      flow_p->exe_ctx_p, and the execution can be continued with o71_run()
 *  @retval O71_EXC
 *      an exception was thrown;
 *      the exception object is stored in flow_p->exc_r
 *  @retval O71_BAD_FUNC_REF
 *      @a func_r is not a valid reference to a function object
 *  @note 
 *      The idea is to have script functions to just push a context and return
 *      leaving the execution for the o71_run() function; however, for native
 *      functions which run for a negligeable number of steps, the result
 *      can be computed straight from call() without reaching run().
 *  @note
 *      if @a func_r does not point to a function or the function's call() 
 *      callback returns an error then this function prepares the appropriate
 *      exception and returns O71_EXC
 */
O71_API o71_status_t o71_prep_call
(
    o71_flow_t * flow_p,
    o71_ref_t func_r,
    o71_ref_t * arg_ra,
    size_t arg_n
);

/* o71_run ******************************************************************/
/**
 *  Executes the flow until the execution stack reaches a certain minimum depth
 *  or it executed a certain number of steps.
 *  @param min_depth [in]
 *      depth where to stop execution
 *  @param steps [in]
 *      minimum number of steps that need to be executed before returning
 *      (unless the execution depth criteria is met);
 *      this value must be at most O71_STEPS_MAX;
 */
O71_API o71_status_t o71_run
(
    o71_flow_t * flow_p,
    unsigned int min_depth,
    uint32_t steps
);

/* o71_call *****************************************************************/
/**
 *  Calls a function object.
 *  This function combines o71_prep_call() and o71_run().
 *  If o71_prep_call() returns some error status other than O71_EXC then
 *  it generates the appropriate exception object and sets the flow in the
 *  exception thrown state.
 *  The function can stop execution somewhere in the middle of some function
 *  or subfunction if the number of executed steps exceeds the given limit.
 *  @retval O71_OK
 *      function executed and the result is stored in flow_p->value_r
 *  @retval O71_PENDING
 *      the function did not finish execution in the given amount of steps
 *      a new execution context has been pushed into the chain from
 *      flow_p->exe_ctx_p, and the execution can be continued with o71_run()
 *  @retval O71_EXC
 *      an exception was thrown;
 *      the exception object is stored in flow_p->exc_r
 *  @retval O71_BAD_FUNC_REF
 *      @a func_r is not a valid reference to a function object
 */
O71_API o71_status_t o71_call
(
    o71_flow_t * flow_p,
    o71_ref_t func_r,
    o71_ref_t * arg_ra,
    size_t arg_n,
    uint32_t steps
);

/* o71_kvbag_put ************************************************************/
/**
 *  Puts a value in the bag.
 *
 *  @note 
 *      @a value_r will not get its ref count incremented
 *  @note 
 *      if there was a value set for the given key, that value will get its
 *      ref count decremented
 *  @retval O71_OK
 *  @retval O71_NO_MEM
 *  @retval O71_MEM_LIMIT
 *  @retval O71_ARRAY_LIMIT
 *
 *  @retval O71_UNUSED_MEM_OBJ_SLOT
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_REF_COUNT_OVERFLOW
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
O71_API o71_status_t o71_kvbag_put
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r
);

/* o71_kvbag_get ************************************************************/
/**
 *  @retval O71_OK
 *  @retval O71_MISSING
 */
O71_API o71_status_t o71_kvbag_get
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t * value_rp
);


#endif /* _O71_H */