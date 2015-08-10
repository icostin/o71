/* Internal config options */
#define O71_METHOD_ARRAY_LIMIT 0x80
#define O71_REG_OBJ_FIELD_ARRAY_LIMIT 0x40

#include "o71.h"

#if O71_DEBUG
#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#define P(...) (printf(__VA_ARGS__))
#define MP(...) do { printf("%s():%u: ", __FUNCTION__, __LINE__); \
    printf(__VA_ARGS__); } while (0)
#define M(...) do { printf("%s():%u: ", __FUNCTION__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } while (0)
#else
#define M(...) ((void) 0)
#define MP(...) ((void) 0)
#define P(...) ((void) 0)
#endif

#if O71_DEBUG >= 2
#define M2 M
#else
#define M2(...) ((void) 0)
#endif

#define FIELD_OFS(_type, _field) ((uintptr_t) &((_type *) NULL)->_field)
#define ITEM_COUNT(_array) (sizeof(_array) / sizeof(_array[0]))
#define IS_DIGIT(_ch) ((_ch) >= '0' && (_ch) <= '9')
#define IS_HEX_DIGIT(_ch) \
    (((_ch) >= '0' && (_ch) <= '9') || \
     (((_ch) | 0x20) >= 'a' && ((_ch) | 0x20) <= 'f'))
#define IS_ALPHA(_ch) (((_ch) >= 'a' && (_ch) <= 'z') || \
                       ((_ch) >= 'A' && (_ch) <= 'Z'))
#define IS_ID_START_CHAR(_ch) (IS_ALPHA((_ch)) || (_ch) == '_')
#define IS_ID_BODY_CHAR(_ch) (IS_ID_START_CHAR((_ch)) || IS_DIGIT((_ch)))
#define ALPHANUM_TO_DIGIT(_ch) ((_ch) <= '9' ? (_ch) - '0' : 9 + ((_ch) & 31))

#define FREE_ARRAY(_allocator_p, _array, _length) \
    os = redim((_allocator_p), (void * *) &(_array), &(_length), 0, \
               sizeof((_array)[0])); AOS(os)

#define ALLOC(_os, _allocator_p, _ptr) do { size_t _n = 0; \
    (_os) = redim((_allocator_p), (void * *) &(_ptr), &_n, 1, sizeof(*(_ptr))); \
} while (0)

#define FREE(_allocator_p, _ptr) do { size_t _n = 1; o71_status_t _os; \
    _os = redim((_allocator_p), (void * *) &(_ptr), &_n, 0, sizeof(*(_ptr))); \
    AOS(_os); \
} while (0)

#if O71_CHECKED
#define A(_cond) \
    if ((_cond)) ; \
    else do { M("assert failed: %s", #_cond); return O71_BUG; } while (0)
#define AOS(_os) \
    if ((_os) == O71_OK) ; \
    else do { M("assert status failed: %s", N(_os)); return (_os); } while (0)
#else
#define A(_cond) ((void) 0)
#define AOS(_os) ((void) (_os))
#endif

#define ENCODE_FREE_OBJECT_SLOT(_x) (((_x) << 1) | 1)
#define DECODE_FREE_OBJECT_SLOT(_v) ((_v) >> 1)
#define IS_FREE_OBJECT_SLOT(_v) ((uintptr_t) (_v) & 1)
#define IS_OBJ_PTR_ALIGNED(_p) (!((_p) & (sizeof(void *) - 1)))

#define N(_x) (o71_status_name(_x))

#define TN(_x) ((o71_kvnode_t *) ((_x) & (intptr_t) -2))
#define GET_CHILD(_node, _side) (TN((_node)->clr[(_side)]))
#define IS_RED(_node) ((_node) && ((_node)->clr[0] & 1))
#define SET_RED(_node, _red) \
    ((_node)->clr[0] = ((_node)->clr[0] & (intptr_t) -2) | (_red))
#define SET_CHILD(_node, _side, _child) \
    ((_node)->clr[(_side)] = ((_node)->clr[(_side)] & 1) | (uintptr_t) (_child))
#define OTHER_SIDE(_side) ((_side) ^ 1)

typedef struct grammar_rule_s grammar_rule_t;
#define RTLEN 10
struct grammar_rule_s
{
    o71_status_t (* handler) (o71_code_t * code_p);
    uint8_t target;
    uint8_t sources[RTLEN];
#if _DEBUG
    char * text;
#endif
};

/*  log2_rounded_up  */
/**
 *  Returns the smallest non-negative power of 2 greater or equal to n.
 */
static uint8_t log2_rounded_up
(
    uintptr_t n
);

/*  utf8_codepoint_length  */
/**
 *  Computes the length in bytes of the UTF8 encoding for the given *valid*
 *  Unicode codepoint.
 */
static unsigned int utf8_codepoint_length
(
    uint32_t codepoint
);

/*  utf8_codepoint_encode  */
/**
 *  Encodes a single Unicode codepoint into UTF8.
 *  @warning @a out must be large enough to hold the encoded char; one can use
 *  utf8_codepoint_length() to determine the necessary size.
 */
static unsigned int utf8_codepoint_encode
(
    uint8_t * out,
    uint32_t codepoint
);

/* redim */
/**
 *  Redim, steadym, gom! Redim your soul!
 *  Redimensionate a memory block.
 *  @retval O71_OK
 *  @retval O71_NO_MEM
 *  @retval O71_MEM_LIMIT
 *  @retval O71_ARRAY_LIMIT
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
static o71_status_t redim
(
    o71_allocator_t * allocator_p,
    void * * data_pp,
    size_t * crt_count_p,
    size_t new_count,
    size_t item_size
);

/*  extend_array  */
/**
 *  Extend an array with a given number of items.
 */
static o71_status_t extend_array
(
    o71_allocator_t * allocator_p,
    void * * extra_ap,
    void * * data_ap,
    size_t * crt_alloc_count_p,
    size_t * crt_used_count_p,
    size_t extra_count,
    size_t item_size
);

/* flow_init */
static void flow_init
(
    o71_world_t * world_p,
    o71_flow_t * flow_p
);

/* extend_object_table */
/**
 *  @retval O71_OK
 *  @retval O71_NO_MEM.
 *  @retval O71_MEM_LIMIT,
 *  @retval O71_ARRAY_LIMIT,
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
static o71_status_t extend_object_table
(
    o71_world_t * world_p
);

/*  alloc_object_index  */
/**
 *  Allocates an index in the object table.
 *  @retval O71_OK
 *  @retval O71_NO_MEM.
 *  @retval O71_MEM_LIMIT,
 *  @retval O71_ARRAY_LIMIT,
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
static o71_status_t alloc_object_index
(
    o71_world_t * world_p,
    o71_obj_index_t * obj_xp
);

/*  free_object_index  */
/**
 *  Adds the given index to the list of free object indexes.
 *  @retval O71_OK
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
static o71_status_t free_object_index
(
    o71_world_t * world_p,
    o71_obj_index_t obj_x
);

/*  alloc_object  */
/**
 *  Allocates memory for an object according to the size specified in its
 *  class and allocates an entry in the object table for it.
 *  @retval O71_OK
 *  @retval O71_NO_MEM
 *  @retval O71_MEM_LIMIT
 *  @retval O71_ARRAY_LIMIT
 *  @retval O71_REF_COUNT_OVERFLOW
 *      too many references to the object's class
 *      thic code can be returned only in checked or debug builds
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *      @a class_r does not point to a proper class
 *  @retval O71_TODO
 */
static o71_status_t alloc_object
(
    o71_world_t * world_p,
    o71_ref_t class_r,
    o71_obj_index_t * obj_xp
);

/*  free_object  */
/**
 *  Reverts alloc_object().
 *  @note object's class finish() is not called
 */
static o71_status_t free_object
(
    o71_world_t * world_p,
    o71_ref_t obj_x
);

/*  alloc_exc  */
/**
 *  Allocates and initializes basic fields in an exception object of given
 *  class.
 */
static o71_status_t alloc_exc
(
    o71_world_t * world_p,
    o71_ref_t class_r,
    o71_obj_index_t * obj_xp
);

/*  noop_object_finish  */
/**
 *  @retval O71_OK
 */
o71_status_t noop_object_finish
(
    o71_world_t * world_p,
    o71_ref_t obj_r
);

/*  get_missing_field  */
/**
 *  @retval O71_MISSING
 */
static o71_status_t get_missing_field
(
    o71_flow_t * flow_p,
    o71_ref_t obj_r,
    o71_ref_t field_r,
    o71_ref_t * value_p
);

/* set_missing_field */
/**
 *  Decrements ref count for value and returns missing field status.
 *  @retval O71_MISSING
 *  @retval O71_TODO
 */
static o71_status_t set_missing_field
(
    o71_flow_t * flow_p,
    o71_ref_t obj_r,
    o71_ref_t field_r,
    o71_ref_t value
);

/*  get_reg_obj_field  */
/**
 *
 */
static o71_status_t get_reg_obj_field
(
    o71_flow_t * flow_p,
    o71_ref_t obj_r,
    o71_ref_t field_r,
    o71_ref_t * value_rp
);

/* set_reg_obj_field */
/**
 *  @retval O71_TODO
 */
static o71_status_t set_reg_obj_field
(
    o71_flow_t * flow_p,
    o71_ref_t obj_r,
    o71_ref_t field_r,
    o71_ref_t value
);

/* null_func_run */
/**
 *  Run function that returns null.
 */
static o71_status_t null_func_run
(
    o71_flow_t * flow_p
);

/*  int_add_call  */
/**
 *  The handler for a call to the function to add ints.
 */
static o71_status_t int_add_call
(
    o71_flow_t * flow_p,
    o71_ref_t func_r,
    o71_ref_t * arg_ra,
    size_t arg_n
);

/*  str_intern_cmp  */
/**
 *  Compares two strings as ordered in the intern bag.
 */
static o71_status_t str_intern_cmp
(
    o71_world_t * world_p,
    o71_ref_t a_r,
    o71_ref_t b_r,
    void * ctx
);

/*  str_finish  */
/**
 *  Uninitializer for strings.
 */
static o71_status_t str_finish
(
    o71_world_t * world_p,
    o71_ref_t obj_r
);

/*  sfunc_call  */
/**
 *  Handler for calls to scripted functions.
 */
static o71_status_t sfunc_call
(
    o71_flow_t * flow_p,
    o71_ref_t func_r,
    o71_ref_t * arg_ra,
    size_t arg_n
);

/*  sfunc_run  */
/**
 *  Handler for the run stage of a scripted function.
 */
static o71_status_t sfunc_run
(
    o71_flow_t * flow_p
);

/*  sfunc_alloc_code  */
/**
 *  Allocates code structures.
 */
static o71_status_t sfunc_alloc_code
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    o71_insn_t * * insn_ap,
    uint32_t * * opnd_ap,
    size_t insn_n,
    size_t opnd_n
);

/*  sfunc_add_const  */
/**
 *  Adds a constant to the function.
 */
static o71_status_t sfunc_add_const
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t * const_xp,
    o71_ref_t obj_r
);

/* sfunc_append_opc_val_obj_name */
/**
 *  @retval O71_OK alles gut
 */
static o71_status_t sfunc_append_opc_val_obj_name
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint8_t opcode,
    uint32_t value_vx,
    uint32_t obj_vx,
    uint32_t name_istr_vx
);

/*  ref_cmp  */
/**
 *  Compares 2 references by their value as integers not by the content
 *  referenced.
 */
static o71_status_t ref_cmp
(
    o71_world_t * world_p,
    o71_ref_t a_r,
    o71_ref_t b_r,
    void * ctx
);

/*  kvbag_init  */
/**
 *  Inits a key value bag.
 */
static void kvbag_init
(
    o71_kvbag_t * kvbag_p,
    uint8_t array_limit
);

/*  kvbag_array_search  */
/**
 *
 */
static o71_status_t kvbag_array_search
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_cmp_f cmp,
    void * ctx,
    o71_kvbag_loc_t * loc_p
);

/*  kvbag_array_delete  */
/**
 *
 */
static o71_status_t kvbag_array_delete
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p
);

/*  kvbag_rbtree_search  */
/**
 *
 */
static o71_status_t kvbag_rbtree_search
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_cmp_f cmp,
    void * ctx,
    o71_kvbag_loc_t * loc_p
);

/*  kvbag_rbtree_add  */
/**
 *
 */
static o71_status_t kvbag_rbtree_add
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r
);

/* kvbag_rbtree_multi_add */
/**
 *  Adds multiple key-value items.
 *  @note this function is intended for switching a bag from the array form
 *  to red/black tree.
 */
static o71_status_t kvbag_rbtree_multi_add
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kv_t * kv_a,
    size_t kv_n
);

/* kvbag_rbtree_free */
/**
 * Deletes all nodes in the tree.
 */
static o71_status_t kvbag_rbtree_free
(
    o71_world_t * world_p,
    o71_kvnode_t * kvnode_p
);

/*  kvbag_rbtree_node_alloc  */
/**
 *
 */
static o71_status_t kvbag_rbtree_node_alloc
(
    o71_world_t * world_p,
    o71_kvnode_t * * kvnode_pp
);

/*  kvbag_rbtree_node_free  */
/**
 *
 */
static o71_status_t kvbag_rbtree_node_free
(
    o71_world_t * world_p,
    o71_kvnode_t * kvnode_p
);

/*  kvbag_search  */
/**
 *
 */
static o71_status_t kvbag_search
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_cmp_f cmp,
    void * ctx,
    o71_kvbag_loc_t * loc_p
);

/*  kvbag_rbtree_insert  */
/**
 *
 */
static o71_status_t kvbag_rbtree_insert
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r,
    o71_kvbag_loc_t * loc_p
);

/*  kvbag_rbtree_delete  */
/**
 *
 */
static o71_status_t kvbag_rbtree_delete
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p
);

/*  kvbag_rbtree_np  */
/**
 *
 */
static o71_kvnode_t * kvbag_rbtree_np
(
    o71_kvbag_loc_t * loc_p,
    unsigned int side
);

/* kvbag_insert */
/**
 *  Inserts a new key-value at the given location.
 *  @param key_r [in]
 *      key to insert; this will get its ref count incremented
 *  @oaram value_r [in]
 *      value to insert; this will not get its ref count incremented
 *  @param loc_p [in]
 *      location from a previous kvbag_search() that returned O71_MISSING
 */
static o71_status_t kvbag_insert
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r,
    o71_kvbag_loc_t * loc_p
);

/* kvbag_delete */
/**
 *  Deletes the item located.
 *  Ref counts are not affected - this just deletes the item from the bag.
 *  @retval O71_OK
 *  @returns error only in checked builds when discovering a bug
 */
static o71_status_t kvbag_delete
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p
);

/*  kvbag_get_loc_value  */
/**
 *
 */
static o71_ref_t kvbag_get_loc_value
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p
);

/*  kvbag_set_loc_value  */
/**
 *
 */
static void kvbag_set_loc_value
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p,
    o71_ref_t value_r
);

/* kvbag_put */
/**
 *  Adds/replaces a value in the bag for the given key.
 *  @param key_r [in]
 *      key for which to associate @a value_r;
 *      the key gets its ref count incremented only if the bag did not contain
 *      the key
 *  @param value_r [in]
 *      value to associate with @a key_r;
 *      the reference is borrowed (value's ref count is not incremented)
 *  @retval O71_OK
 *  @retval O71_NO_MEM
 *  @retval O71_MEM_LIMIT
 *  @retval O71_ARRAY_LIMIT
 *  @retval O71_MEM_CORRUPTED
 *  @retval O71_BUG
 *  @retval O71_TODO
 */
static o71_status_t kvbag_put
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r,
    o71_cmp_f cmp,
    void * ctx
);

/*  merge_sorted_refs  */
/**
 *  Reallocates the destination array of sorted refs and inserts source
 *  elements keeping the sort order.
 *  @note dest and src must be sorted
 *  @note this will keep duplicates
 */
static o71_status_t merge_sorted_refs
(
    o71_world_t * world_p,
    o71_ref_t * * dest_rap,
    size_t * dest_rnp,
    o71_ref_t * src_ra,
    size_t src_n
);

/*  ref_qsort  */
/**
 *  Sorts an array of references.
 */
static void ref_qsort
(
    o71_ref_t * ref_a,
    size_t ref_n
);

/*  refkv_qsort  */
/**
 *  Sorts an array of kv pairs by comparing the key reference.
 */
static void refkv_qsort
(
    o71_kv_t * kv_a,
    size_t n
);

/*  ref_search  */
/**
 *  Binary search for refs
 */
static ptrdiff_t ref_search
(
    o71_ref_t * ra,
    size_t rn,
    o71_ref_t kr
);

/*  class_super_extend  */
/**
 *  Extends the array of superclasses.
 *  @param world_p [in]
 *  @param class_p [in, out]
 *  @param super_ra [in, out]
 *      has the list of classes to add (references are borrowed);
 *      clobbered on exit
 *  @param super_n
 */
static o71_status_t class_super_extend
(
    o71_world_t * world_p,
    o71_class_t * class_p,
    o71_ref_t * super_ra,
    size_t super_n
);

/*  set_var  */
/**
 *  Dereferences old value stored in destination and references the source
 *  value and stores it in destination.
 *  @retval O71_OK only value for release builds
 *  @retval O71_REF_COUNT_OVERFLOW
 *      adding a reference causes int overflow;
 *      this should only happen due to a bug from some component;
 *      this code can be returned only in checked or debug builds
 *  @retval O71_OBJ_DESTRUCTING
 *      attempt to increase the ref count of an object from the destroy chain;
 *      this code can be returned only in checked or debug builds
 *  @retval O71_UNUSED_MEM_OBJ_SLOT
 *      bad reference to unused memory object slot;
 *      this code can be returned only in checked or debug builds
  * @retval O71_MEM_CORRUPTED
  *     allocator detects some corruption while freeing memory for destroyed
  *     objects
  * @retval O71_BUG
  *     some assertion failed
  *     this code can be returned only in checked or debug builds
 *
 */
O71_INLINE o71_status_t set_var
(
    o71_world_t * world_p,
    o71_ref_t * dest_rp,
    o71_ref_t src_r
);

/*  free_token  */
/**
 *  Frees one token
 */
static o71_status_t free_token
(
    o71_code_t * code_p,
    o71_token_t * token_p,
    unsigned int min_tt
);

/*  tokenize_source  */
/**
 *  Tokenizes source.
 */
static o71_status_t tokenize_source
(
    o71_code_t * code_p
);

static o71_status_t match_stmt
(
    o71_code_t * code_p,
    o71_token_t * in_p
);

#if O71_DEBUG
/*  obj_dump  */
/**
 *
 */
static void obj_dump
(
    o71_world_t * world_p,
    o71_ref_t obj_r
);

/*  kvbag_dump  */
/**
 *
 */
static void kvbag_dump
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p
);

/*  kvbag_array_dump  */
/**
 *
 */
static void kvbag_array_dump
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p
);

/*  kvbag_rbtree_dump  */
/**
 *  Dumps to stdout the tree
 */
static void kvbag_rbtree_dump
(
    o71_world_t * world_p,
    o71_kvnode_t * kvnode_p,
    unsigned int depth
);

/*  dump_token_list  */
/**
 *  Prints a list token types
 */
static void dump_token_list
(
    o71_token_t * token_p,
    size_t n
);

#else
#define kvbag_dump(_w, _k) ((void) 0)
#define dump_token_list(_tp, _n) ((void) 0)
#endif

static o71_status_t rule_nop (o71_code_t * code_p);

static uint32_t init_exc_chain_start_xa[2] = { 0, 0 };


/* grammar ******************************************************************/
#if _DEBUG
#define R(_h, _t, ...) { _h, (_t), { __VA_ARGS__ }, #_h ": " #_t " := " #__VA_ARGS__ }
#else
#define R(_h, _t, ...) { _h, (_t), { __VA_ARGS__ } }
#endif

static char const * ttname_a[O71_TT__COUNT] =
{
    "<rule_end>",
    "END",
    "'~'",
    "'!'",
    "'?'",
    "'%'",
    "'^'",
    "'&'",
    "'|'",
    "'*'",
    "'/'",
    "'('",
    "')'",
    "'['",
    "']'",
    "'{'",
    "'}'",
    "'='",
    "'+'",
    "'-'",
    "'<'",
    "'>'",
    "'.'",
    "','",
    "';'",
    "':'",
    "'++'",
    "'--'",
    "'**'",
    "'<<'",
    "'>>'",
    "'&&'",
    "'||'",
    "'=='",
    "'!='",
    "'+='",
    "'-='",
    "'*='",
    "'/='",
    "'%='",
    "'<='",
    "'>='",
    "'<<='",
    "'>>='",
    "'&='",
    "'|='",
    "'^='",
    "'**='",
    "'&&='",
    "'||='",
    "'return'",
    "'break'",
    "'goto'",
    "'if'",
    "'else'",
    "'while'",
    "'do'",
    "'for'",
    "'switch'",
    "'case'",
    "integer",
    "string",
    "identifier",
    "source",
    "stmt_seq",
    "stmt",
    "decl_start",
    "block_stmt",
    "if_stmt_start",
    "do_stmt_start",
    "while_cond",
    "unary_operator",
    "exp_operator",
    "mul_operator",
    "add_operator",
    "cmp_operator",
    "equ_operator",
    "asg_operator",
    "var_init",
    "var_init_list",
    "arg_decl",
    "arg_decl_list",
    "expr",
    "expr_list",
    "atom_expr",
    "postfix_expr",
    "prefix_expr",
    "exp_expr",
    "and_expr",
    "or_expr",
    "xor_expr",
    "mul_expr",
    "add_expr",
    "cmp_expr",
    "equ_expr",
    "logic_and_expr",
    "logic_xor_expr",
    "logic_or_expr",
    "cond_expr"
};

/* utf8_codepoint_length ****************************************************/
static unsigned int utf8_codepoint_length
(
    uint32_t codepoint
)
{
    if (codepoint < 0x80) return 1;
    if (codepoint < 0x800) return 2;
    if (codepoint < 0x10000) return 3;
    return 4;
}

/* utf8_codepoint_encode ****************************************************/
static unsigned int utf8_codepoint_encode
(
    uint8_t * out,
    uint32_t codepoint
)
{
    if (codepoint < 0x80)
    {
        out[0] = (uint8_t) codepoint;
        return 1;
    }
    if (codepoint < 0x800)
    {
        out[0] = 0xC0 | (uint8_t) (codepoint >> 6);
        out[1] = 0x80 | (uint8_t) (codepoint & 0x3F);
        return 2;
    }
    if (codepoint < 0x10000)
    {
        out[0] = 0xE0 | (uint8_t) (codepoint >> 12);
        out[1] = 0x80 | (uint8_t) ((codepoint >> 6) & 0x3F);
        out[2] = 0x80 | (uint8_t) (codepoint & 0x3F);
        return 3;
    }
    out[0] = 0xF0 | (uint8_t) (codepoint >> 16);
    out[1] = 0x80 | (uint8_t) ((codepoint >> 12) & 0x3F);
    out[2] = 0x80 | (uint8_t) ((codepoint >> 6) & 0x3F);
    out[3] = 0x80 | (uint8_t) (codepoint & 0x3F);
    return 4;
}

/* o71_status_name **********************************************************/
O71_API char const * o71_status_name
(
    o71_status_t status
)
{
#define X(_x) case _x: return #_x
    switch (status)
    {
        X(O71_OK);
        X(O71_PENDING);
        X(O71_EXC);

        X(O71_MISSING);

        X(O71_NO_MEM);
        X(O71_MEM_LIMIT);
        X(O71_ARRAY_LIMIT);

        X(O71_NOT_MEM_OBJ_REF);
        X(O71_UNUSED_MEM_OBJ_SLOT);
        X(O71_MODEL_MISMATCH);
        X(O71_OBJ_DESTRUCTING);
        X(O71_BAD_FUNC_REF);
        X(O71_BAD_OPCODE);
        X(O71_BAD_OPERAND_INDEX);
        X(O71_BAD_CONST_INDEX);
        X(O71_BAD_VAR_INDEX);
        X(O71_BAD_INSN_INDEX);
        X(O71_BAD_EXC_HANDLER_INDEX);
        X(O71_BAD_EXC_CHAIN_INDEX);
        X(O71_BAD_ARG_COUNT);
        X(O71_CMP_ERROR);
        X(O71_BAD_STRING_REF);
        X(O71_BAD_RO_STRING_REF);
        X(O71_BAD_INTERN_STRING_REF);
        X(O71_COMPILE_ERROR);
        X(O71_NO_MATCH);

        X(O71_MEM_CORRUPTED);
        X(O71_REF_COUNT_OVERFLOW);
        X(O71_BUG);
        X(O71_TODO);
    default:
        return "O71_unknown";
    }
#undef X
}

/* o71_allocator_init *******************************************************/
O71_API o71_allocator_t * o71_allocator_init
(
    o71_allocator_t * allocator_p,
    o71_realloc_f realloc,
    void * context,
    size_t mem_limit
)
{
    allocator_p->realloc = realloc;
    allocator_p->context = context;
    allocator_p->mem_usage = 0;
    allocator_p->mem_peak = 0;
    allocator_p->mem_limit = mem_limit;
    return allocator_p;
}

/* o71_world_init ***********************************************************/
O71_API o71_status_t o71_world_init
(
    o71_world_t * world_p,
    o71_allocator_t * allocator_p
)
{
    o71_status_t os;

    world_p->allocator_p = allocator_p;
    world_p->flow_id_seed = 0;
    world_p->cleaning = 0;
    world_p->free_list_head_x = 0;
    world_p->destroy_list_head_x = 0;
    world_p->obj_pa = NULL;
    world_p->obj_n = 0;

    os = extend_object_table(world_p);
    if (os) return os;

    kvbag_init(&world_p->istr_bag, 0x10);

    world_p->obj_pa[O71X_NULL] = &world_p->null_object;
    world_p->obj_pa[O71X_OBJECT_CLASS] = &world_p->object_class;
    world_p->obj_pa[O71X_NULL_CLASS] = &world_p->null_class;
    world_p->obj_pa[O71X_CLASS_CLASS] = &world_p->class_class;
    world_p->obj_pa[O71X_STRING_CLASS] = &world_p->string_class;
    world_p->obj_pa[O71X_SMALL_INT_CLASS] = &world_p->small_int_class;
    world_p->obj_pa[O71X_REG_OBJ_CLASS] = &world_p->reg_obj_class;
    world_p->obj_pa[O71X_FUNCTION_CLASS] =
        &world_p->function_class;
    world_p->obj_pa[O71X_SCRIPT_FUNCTION_CLASS] =
        &world_p->script_function_class;
    world_p->obj_pa[O71X_EXCEPTION_CLASS] = &world_p->exception_class;
    world_p->obj_pa[O71X_TYPE_EXC_CLASS] = &world_p->type_exc_class;
    world_p->obj_pa[O71X_ARITY_EXC_CLASS] = &world_p->arity_exc_class;
    world_p->obj_pa[O71X_INT_ADD_FUNC] = &world_p->int_add_func;

    world_p->null_object.class_r = O71R_NULL_CLASS;
    world_p->null_object.ref_n = 1; // permanent object

    world_p->object_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->object_class.hdr.ref_n = 1;
    world_p->object_class.finish = noop_object_finish;
    world_p->object_class.get_field = get_missing_field;
    world_p->object_class.set_field = set_missing_field;
    world_p->object_class.object_size = sizeof(o71_mem_obj_t);
    world_p->object_class.model = O71MI_MEM_OBJ;
    world_p->object_class.super_ra = NULL;
    world_p->object_class.super_n = 0;
    world_p->object_class.dyn_field_ofs = 0;
    world_p->object_class.fix_field_n = 0;
    kvbag_init(&world_p->object_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->null_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->null_class.hdr.ref_n = 1;
    world_p->null_class.finish = noop_object_finish;
    world_p->null_class.get_field = get_missing_field;
    world_p->null_class.set_field = set_missing_field;
    world_p->null_class.object_size = sizeof(o71_mem_obj_t);
    world_p->null_class.model = O71MI_MEM_OBJ;
    world_p->null_class.super_ra = NULL;
    world_p->null_class.super_n = 0;
    world_p->null_class.dyn_field_ofs = 0;
    world_p->null_class.fix_field_n = 0;
    kvbag_init(&world_p->null_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->class_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->class_class.hdr.ref_n = 1;
    world_p->class_class.finish = noop_object_finish;
    world_p->class_class.get_field = get_missing_field;
    world_p->class_class.set_field = set_missing_field;
    world_p->class_class.object_size = sizeof(o71_class_t);
    world_p->class_class.model = O71MI_CLASS;
    world_p->class_class.super_ra = NULL;
    world_p->class_class.super_n = 0;
    world_p->class_class.dyn_field_ofs = 0;
    world_p->class_class.fix_field_n = 0;
    kvbag_init(&world_p->class_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->string_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->string_class.hdr.ref_n = 1;
    world_p->string_class.finish = str_finish;
    world_p->string_class.get_field = get_missing_field;
    world_p->string_class.set_field = set_missing_field;
    world_p->string_class.object_size = sizeof(o71_string_t);
    world_p->string_class.model = O71MI_STRING;
    world_p->string_class.super_ra = NULL;
    world_p->string_class.super_n = 0;
    world_p->string_class.dyn_field_ofs = 0;
    world_p->string_class.fix_field_n = 0;
    kvbag_init(&world_p->string_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->small_int_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->small_int_class.hdr.ref_n = 1;
    world_p->small_int_class.finish = noop_object_finish;
    world_p->small_int_class.get_field = get_missing_field;
    world_p->small_int_class.set_field = set_missing_field;
    world_p->small_int_class.object_size = 0; // not a memory object
    world_p->small_int_class.model = O71MI_SMALL_INT;
    world_p->small_int_class.super_ra = NULL;
    world_p->small_int_class.super_n = 0;
    world_p->small_int_class.dyn_field_ofs = 0;
    world_p->small_int_class.fix_field_n = 0;
    kvbag_init(&world_p->small_int_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->reg_obj_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->reg_obj_class.hdr.ref_n = 1;
    world_p->reg_obj_class.finish = noop_object_finish;
    world_p->reg_obj_class.get_field = get_reg_obj_field;
    world_p->reg_obj_class.set_field = set_reg_obj_field;
    world_p->reg_obj_class.object_size = sizeof(o71_reg_obj_t);
    world_p->reg_obj_class.model = O71MI_MEM_OBJ;
    world_p->reg_obj_class.super_ra = NULL;
    world_p->reg_obj_class.super_n = 0;
    world_p->reg_obj_class.dyn_field_ofs =
        FIELD_OFS(o71_reg_obj_t, dyn_field_bag);
    world_p->reg_obj_class.fix_field_n = 0;
    kvbag_init(&world_p->reg_obj_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->function_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->function_class.hdr.ref_n = 1;
    world_p->function_class.finish = noop_object_finish;
    world_p->function_class.get_field = get_missing_field;
    world_p->function_class.set_field = set_missing_field;
    world_p->function_class.object_size = sizeof(o71_function_t);
    world_p->function_class.model = O71MI_FUNCTION;
    world_p->function_class.super_ra = NULL;
    world_p->function_class.super_n = 0;
    world_p->function_class.dyn_field_ofs = 0;
    world_p->function_class.fix_field_n = 0;
    kvbag_init(&world_p->function_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->script_function_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->script_function_class.hdr.ref_n = 1;
    world_p->script_function_class.finish = noop_object_finish;
    world_p->script_function_class.get_field = get_missing_field;
    world_p->script_function_class.set_field = set_missing_field;
    world_p->script_function_class.object_size = sizeof(o71_script_function_t);
    world_p->script_function_class.model = O71MI_SCRIPT_FUNCTION;
    world_p->script_function_class.super_ra = NULL;
    world_p->script_function_class.super_n = 0;
    world_p->script_function_class.dyn_field_ofs = 0;
    world_p->script_function_class.fix_field_n = 0;
    kvbag_init(&world_p->script_function_class.method_bag,
               O71_METHOD_ARRAY_LIMIT);

    world_p->exception_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->exception_class.hdr.ref_n = 1;
    world_p->exception_class.finish = noop_object_finish;
    world_p->exception_class.get_field = get_reg_obj_field;
    world_p->exception_class.set_field = set_reg_obj_field;
    world_p->exception_class.object_size = sizeof(o71_exception_t);
    world_p->exception_class.model = O71MI_EXCEPTION;
    world_p->exception_class.super_ra = NULL;
    world_p->exception_class.super_n = 0;
    world_p->exception_class.dyn_field_ofs =
        FIELD_OFS(o71_reg_obj_t, dyn_field_bag);
    world_p->exception_class.fix_field_n = 0;
    kvbag_init(&world_p->exception_class.method_bag,
               O71_METHOD_ARRAY_LIMIT);

    world_p->type_exc_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->type_exc_class.hdr.ref_n = 1;
    world_p->type_exc_class.finish = noop_object_finish;
    world_p->type_exc_class.get_field = get_missing_field;
    world_p->type_exc_class.set_field = set_missing_field;
    world_p->type_exc_class.object_size = sizeof(o71_exception_t);
    world_p->type_exc_class.model = O71MI_EXCEPTION;
    world_p->type_exc_class.super_ra = NULL;
    world_p->type_exc_class.super_n = 0;
    world_p->type_exc_class.dyn_field_ofs = 0;
    world_p->type_exc_class.fix_field_n = 0;
    kvbag_init(&world_p->type_exc_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->arity_exc_class.hdr.class_r = O71R_CLASS_CLASS;
    world_p->arity_exc_class.hdr.ref_n = 1;
    world_p->arity_exc_class.finish = noop_object_finish;
    world_p->arity_exc_class.get_field = get_missing_field;
    world_p->arity_exc_class.set_field = set_missing_field;
    world_p->arity_exc_class.object_size = sizeof(o71_exception_t);
    world_p->arity_exc_class.model = O71MI_EXCEPTION;
    world_p->arity_exc_class.super_ra = NULL;
    world_p->arity_exc_class.super_n = 0;
    world_p->arity_exc_class.dyn_field_ofs = 0;
    world_p->arity_exc_class.fix_field_n = 0;
    kvbag_init(&world_p->arity_exc_class.method_bag, O71_METHOD_ARRAY_LIMIT);

    world_p->int_add_func.cls.hdr.class_r = O71R_FUNCTION_CLASS;
    world_p->int_add_func.cls.hdr.ref_n = 1;
    world_p->int_add_func.cls.finish = noop_object_finish;
    world_p->int_add_func.cls.get_field = get_missing_field;
    world_p->int_add_func.cls.set_field = set_missing_field;
    world_p->int_add_func.cls.object_size = 0; // instances are not created
    world_p->int_add_func.cls.model = O71MI_FUNCTION;
    world_p->int_add_func.cls.super_ra = NULL;
    world_p->int_add_func.cls.super_n = 0;
    kvbag_init(&world_p->int_add_func.cls.method_bag, O71_METHOD_ARRAY_LIMIT);
    world_p->int_add_func.call = int_add_call;
    world_p->int_add_func.run = null_func_run;

    flow_init(world_p, &world_p->root_flow);

    do
    {
        o71_ref_t int_add_str_r;
        o71_ref_t exe_ctx_isr;

        o71_ref_t super_ra[3];
        os = o71_ics(world_p, &int_add_str_r, "add");
        if (os) { M("fail: %s", N(os)); break; }
        A(o71_obj_ptr(world_p, int_add_str_r));

        os = kvbag_put(world_p, &world_p->small_int_class.method_bag,
                       int_add_str_r, O71R_INT_ADD_FUNC, ref_cmp, NULL);
        if (os) { M("fail: %s", N(os)); break; }
        os = o71_deref(world_p, int_add_str_r);
        AOS(os);
        A(((o71_mem_obj_t *) o71_obj_ptr(world_p, int_add_str_r))->ref_n == 1);

        super_ra[0] = O71R_OBJECT_CLASS;
        os = class_super_extend(world_p, &world_p->null_class, super_ra, 1);
        if (os) { M("fail: %s", N(os)); break; }

        super_ra[0] = O71R_OBJECT_CLASS;
        os = class_super_extend(world_p, &world_p->class_class, super_ra, 1);
        if (os) { M("fail: %s", N(os)); break; }

        super_ra[0] = O71R_OBJECT_CLASS;
        os = class_super_extend(world_p, &world_p->string_class, super_ra, 1);
        if (os) { M("fail: %s", N(os)); break; }

        super_ra[0] = O71R_OBJECT_CLASS;
        os = class_super_extend(world_p, &world_p->small_int_class,
                                super_ra, 1);
        if (os) { M("fail: %s", N(os)); break; }

        super_ra[0] = O71R_OBJECT_CLASS;
        os = class_super_extend(world_p, &world_p->reg_obj_class, super_ra, 1);
        if (os) { M("fail: %s", N(os)); break; }

        super_ra[0] = O71R_OBJECT_CLASS;
        super_ra[1] = O71R_CLASS_CLASS;
        os = class_super_extend(world_p, &world_p->function_class, super_ra, 2);
        if (os) { M("fail: %s", N(os)); break; }

        /* add function class as superclass of script_function class */
        super_ra[0] = O71R_OBJECT_CLASS;
        super_ra[1] = O71R_CLASS_CLASS;
        super_ra[2] = O71R_FUNCTION_CLASS;
        os = class_super_extend(world_p, &world_p->script_function_class,
                                super_ra, 3);
        if (os) { M("fail: %s", N(os)); break; }

        super_ra[0] = O71R_OBJECT_CLASS;
        super_ra[1] = O71R_REG_OBJ_CLASS;
        os = class_super_extend(world_p, &world_p->exception_class,
                                super_ra, 2);
        if (os) { M("fail: %s", N(os)); break; }

        os = o71_ics(world_p, &exe_ctx_isr, "exe_ctx");
        if (os) { M("fail: %s", N(os)); break; }
        os = redim(world_p->allocator_p,
                   (void * *) &world_p->exception_class.fix_field_ofs_a,
                   &world_p->exception_class.fix_field_n,
                   1, sizeof(o71_ref_t));
        if (os) { M("fail: %s", N(os)); break; }
        world_p->exception_class.fix_field_ofs_a[0].key_r = exe_ctx_isr;
        world_p->exception_class.fix_field_ofs_a[0].value_r =
            FIELD_OFS(o71_exception_t, exe_ctx_r);

        super_ra[0] = O71R_OBJECT_CLASS;
        super_ra[1] = O71R_EXCEPTION_CLASS;
        os = class_super_extend(world_p, &world_p->type_exc_class, super_ra, 2);
        if (os) { M("fail: %s", N(os)); break; }

        os = O71_OK;
        M("hello world %p!", world_p);
    }
    while (0);

    if (os)
    {
        o71_status_t osf = o71_world_finish(world_p);
    }

    return os;
}

/* o71_world_finish *********************************************************/
O71_API o71_status_t o71_world_finish
(
    o71_world_t * world_p
)
{
    o71_status_t os;
    o71_obj_index_t obj_x;

    M("finishing world (obj_n=%lu)", world_p->obj_n);
    for (obj_x = 1; obj_x < world_p->obj_n; ++obj_x)
    {
        /* skip unused slots */
        if (IS_FREE_OBJECT_SLOT(world_p->obj_pa[obj_x])) continue;
        /* skip items already queued for destruction */
        if (world_p->mem_obj_pa[obj_x]->ref_n < 0) continue;
        world_p->mem_obj_pa[obj_x]->enc_destroy_next_x =
            ~world_p->destroy_list_head_x;
        world_p->destroy_list_head_x = obj_x;
        M2("queueing for destruction: obref_%lX", (long) O71_MOX_TO_REF(obj_x));
    }
    os = o71_cleanup(world_p);
    AOS(os);

    os = redim(world_p->allocator_p,
               (void * *) &world_p->obj_pa, &world_p->obj_n, 0,
               sizeof(void *));
    if (os) return os;
    M("goodbye cruel world %p!", world_p);
    return O71_OK;
}

/* o71_check_mem_obj_ref ****************************************************/
O71_API o71_status_t o71_check_mem_obj_ref
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    o71_obj_index_t obj_x;

    if (!O71_IS_REF_TO_MO(obj_r)) return O71_NOT_MEM_OBJ_REF;
    obj_x = O71_REF_TO_MOX(obj_r);
    if (obj_x >= world_p->obj_n) return O71_UNUSED_MEM_OBJ_SLOT;
    if (IS_FREE_OBJECT_SLOT(world_p->obj_pa[obj_x]))
        return O71_UNUSED_MEM_OBJ_SLOT;
    return O71_OK;
}

/* o71_model ****************************************************************/
O71_API uint32_t o71_model
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    o71_obj_index_t obj_x, class_x;
    o71_ref_t class_r;
    o71_class_t * class_p;
    if (O71_IS_REF_TO_SINT(obj_r)) return O71M_SMALL_INT;
    obj_x = O71_REF_TO_MOX(obj_r);
    if (obj_x >= world_p->obj_n || IS_FREE_OBJECT_SLOT(world_p->obj_pa[obj_x]))
        return O71M_INVALID;
    class_r = world_p->mem_obj_pa[obj_x]->class_r;
    class_x = O71_REF_TO_MOX(class_r);
    class_p = world_p->obj_pa[class_x];
    return class_p->model;
}

/* o71_class ****************************************************************/
O71_API o71_class_t * o71_class
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    o71_obj_index_t obj_x, class_x;
    o71_ref_t class_r;
    if (O71_IS_REF_TO_SINT(obj_r)) return &world_p->small_int_class;
    obj_x = O71_REF_TO_MOX(obj_r);
    if (obj_x >= world_p->obj_n || IS_FREE_OBJECT_SLOT(world_p->obj_pa[obj_x]))
        return NULL;
    class_r = world_p->mem_obj_pa[obj_x]->class_r;
    class_x = O71_REF_TO_MOX(class_r);
    return world_p->obj_pa[class_x];
}

/* o71_ref ******************************************************************/
O71_API o71_status_t o71_ref
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    o71_obj_index_t obj_x;
    if (!O71_IS_REF_TO_MO(obj_r)) return O71_OK;
    obj_x = O71_REF_TO_MOX(obj_r);
#if O71_CHECKED
    {
        o71_status_t os;
        os = o71_check_mem_obj_ref(world_p, obj_r);
        if (os) return os;
        if (world_p->mem_obj_pa[obj_x]->ref_n <= 0)
            return O71_OBJ_DESTRUCTING;
    }
    if (world_p->mem_obj_pa[obj_x]->ref_n + 1 < 0)
        return O71_REF_COUNT_OVERFLOW;
#endif
    world_p->mem_obj_pa[obj_x]->ref_n += 1;
    M2("obref_%lX.ref -> %lX", (long) obj_r,
       (long) world_p->mem_obj_pa[obj_x]->ref_n);
    return O71_OK;
}

/* o71_deref ****************************************************************/
O71_API o71_status_t o71_deref
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    o71_obj_index_t obj_x;
    if (!O71_IS_REF_TO_MO(obj_r)) return O71_OK;
    obj_x = O71_REF_TO_MOX(obj_r);
#if O71_CHECKED
    {
        o71_status_t os;
        os = o71_check_mem_obj_ref(world_p, obj_r);
        if (os) return os;
    }
#endif
    if (world_p->mem_obj_pa[obj_x]->ref_n > 1)
    {
        world_p->mem_obj_pa[obj_x]->ref_n -= 1;
        M2("obref_%lX.deref -> %lX", (long) obj_r,
          (long) world_p->mem_obj_pa[obj_x]->ref_n);
        return O71_OK;
    }
    A(world_p->mem_obj_pa[obj_x]->ref_n != 0);
    /* ignore derefs for objects in the destroy chain */
    if ((world_p->mem_obj_pa[obj_x]->ref_n | (intptr_t) -!obj_x) < 0)
    {
        M2("obref_%lX.deref -> already in destroy list", (long) obj_r);
        return O71_OK;
    }
    /* chain the object to the destroy list */
    world_p->mem_obj_pa[obj_x]->enc_destroy_next_x =
        ~world_p->destroy_list_head_x;
    world_p->destroy_list_head_x = obj_x;
    M2("obref_%lX.deref -> queue for destruction", (long) obj_r);
    //M("obj_x=%lX", (long) obj_x);
    return o71_cleanup(world_p);
}

/* o71_cleanup **************************************************************/
O71_API o71_status_t o71_cleanup
(
    o71_world_t * world_p
)
{
    o71_mem_obj_t * mo_p;
    o71_class_t * class_p;
    o71_status_t os;
    o71_obj_index_t obj_x;
    o71_obj_index_t free_head_x = 0;
    size_t obj_size;
    if (world_p->cleaning) return O71_OK;
    world_p->cleaning = 1;
    M("cleanup start");
    os = O71_OK;
    while ((obj_x = world_p->destroy_list_head_x))
    {
        //M("obref_%lX", (long) O71_MOX_TO_REF(obj_x));
        mo_p = world_p->mem_obj_pa[obj_x];
        //M("mo_p = %p", mo_p);
        world_p->destroy_list_head_x = ~mo_p->enc_destroy_next_x;
        //M("next to destroy: %lX", (long) world_p->destroy_list_head_x);
        //A(o71_model(world_p, mo_p->class_r) & O71M_CLASS);
        /* get a pointer to the class; the class object might have been already
         * finalized but it must keep the object_size and finish fields
         * correct */
        class_p = world_p->obj_pa[O71_REF_TO_MOX(mo_p->class_r)];
        A(class_p); A(class_p->finish);
        os = class_p->finish(world_p, O71_MOX_TO_REF(obj_x));
        M2("obref_%lX(class:obref_%lX) finish: %s",
           (long) O71_MOX_TO_REF(obj_x), (long) mo_p->class_r, N(os));
        if (os) break;
        os = o71_deref(world_p, mo_p->class_r);
        if (os)
        {
            M("deref obj class(obref_%lX): %s", (long) mo_p->class_r, N(os));
            break;
        }
        if (obj_x < O71X__COUNT) continue;
        mo_p->object_size = class_p->object_size;
        mo_p->enc_destroy_next_x = ~free_head_x;
        free_head_x = obj_x;
        M2("queue for mem free: obref_%lX", (long) O71_MOX_TO_REF(obj_x));
    }
    M("cleanup mem");
    while (os == O71_OK && free_head_x)
    {
        obj_x = free_head_x;
        mo_p = world_p->mem_obj_pa[obj_x];
        free_head_x = ~mo_p->enc_destroy_next_x;
        //class_p = world_p->obj_pa[O71_REF_TO_MOX(mo_p->class_r)];
        obj_size = mo_p->object_size;
        os = redim(world_p->allocator_p, world_p->obj_pa + obj_x, &obj_size, 0,
                   1);
        if (os)
        {
            M("obj mem free failed: %s", N(os));
            break;
        }
    os = free_object_index(world_p, obj_x);
#if O71_CHECKED
    if (os)
    {
        M("free_object_index(obref_%lX) failed: %s",
          (long) O71_MOX_TO_REF(obj_x), N(os));
        return os;
    }
#endif
    }
    M("cleanup done");
    world_p->cleaning = 0;
    return os;
}

/* o71_cstring **************************************************************/
O71_API o71_status_t o71_cstring
(
    o71_world_t * world_p,
    o71_ref_t * str_rp,
    char const * cstr_a
)
{
    o71_obj_index_t str_x;
    o71_status_t os, osf;
    o71_string_t * str_p;
    size_t i, n;
    os = alloc_object(world_p, O71R_STRING_CLASS, &str_x);
    if (os)
    {
        M("failed to allocate string object: %s", N(os));
        return os;
    }
    A(str_x < world_p->obj_n);
    str_p = world_p->obj_pa[str_x];
    str_p->n = str_p->m = 0;
    str_p->mode = O71_SM_MODIFIABLE;
    for (n = 0; cstr_a[n]; ++n);
    os = redim(world_p->allocator_p, (void * *) &str_p->a, &str_p->m, n, 1);
    if (os)
    {
        M("failed to allocate string data: n=%zu os=%s", n, N(os));
        osf = free_object(world_p, str_x);
        if (osf)
        {
            M("FATAL: free object index: os=%s", N(osf));
            return osf;
        }
        return os;
    }
    for (i = 0; i < n; ++i)
        str_p->a[i] = cstr_a[i];
    str_p->n = str_p->m = n;
    *str_rp = O71_MOX_TO_REF(str_x);
    return O71_OK;
}

/* o71_rocs *****************************************************************/
O71_API o71_status_t o71_rocs
(
    o71_world_t * world_p,
    o71_ref_t * str_rp,
    char const * cstr_a
)
{
    o71_obj_index_t str_x;
    o71_status_t os, osf;
    o71_string_t * str_p;
    size_t i, n;
    os = alloc_object(world_p, O71R_STRING_CLASS, &str_x);
    if (os)
    {
        M("failed to allocate string object: %s", N(os));
        return os;
    }
    A(str_x < world_p->obj_n);
    str_p = world_p->obj_pa[str_x];
    str_p->n = str_p->m = 0;
    str_p->mode = O71_SM_READ_ONLY;
    for (n = 0; cstr_a[n]; ++n);
    str_p->a = (uint8_t *) cstr_a;
    str_p->n = n;
    str_p->m = 0;
    *str_rp = O71_MOX_TO_REF(str_x);
    M("allocated ro string '%s' -> obref_%lX", cstr_a, (long) *str_rp);
    return O71_OK;
}

/* o71_str_freeze ***********************************************************/
O71_API o71_status_t o71_str_freeze
(
    o71_world_t * world_p,
    o71_ref_t str_r
)
{
    o71_string_t * str_p;

    if (!(o71_model(world_p, str_r) & O71M_STRING))
    {
        M("obref_%lX is not a string (model: 0x%X)",
          (long) str_r, o71_model(world_p, str_r));
        return O71_BAD_STRING_REF;
    }
    str_p = o71_obj_ptr(world_p, str_r);
    if (str_p->mode == O71_SM_MODIFIABLE)
        str_p->mode = O71_SM_READ_ONLY;
    return 0;
}

/* o71_str_intern ***********************************************************/
O71_API o71_status_t o71_str_intern
(
    o71_world_t * world_p,
    o71_ref_t str_r,
    o71_ref_t * intern_str_rp
)
{
    o71_kvbag_loc_t loc;
    o71_string_t * str_p;
    o71_ref_t istr_r;
    o71_status_t os;
    M2("str_intern(obref_%lX)", (long) str_r);
    str_p = o71_obj_ptr(world_p, str_r);

    if (!(o71_model(world_p, str_r) & O71M_STRING))
    {
        M2("obref_%lX is not a string (model: 0x%X)",
           (long) str_r, o71_model(world_p, str_r));
        return O71_BAD_STRING_REF;
    }
    str_p = o71_obj_ptr(world_p, str_r);
    if (str_p->mode == O71_SM_INTERN)
    {
        *intern_str_rp = str_r;
        return o71_ref(world_p, str_r);
    }

    os = kvbag_search(world_p, &world_p->istr_bag, str_r,
                      str_intern_cmp, NULL, &loc);
    if (os == O71_OK)
    {
        *intern_str_rp = kvbag_get_loc_value(world_p, &world_p->istr_bag, &loc);
        return o71_ref(world_p, *intern_str_rp);
    }
    /* no matching intern string already present */
    A(os == O71_MISSING);
    if (str_p->mode == O71_SM_MODIFIABLE)
    {
        M("TODO: dup the key to make it read-only");
        return O71_TODO;
    }
    A(str_p->mode == O71_SM_READ_ONLY);
    os = kvbag_insert(world_p, &world_p->istr_bag, str_r, str_r, &loc);
    if (os)
    {
        M("failed to insert string in intern bag: %s", N(os));
        return os;
    }
    str_p->mode = O71_SM_INTERN;
    *intern_str_rp = str_r;
#if O71_DEBUG >= 2
    printf("intern bag:\n");
    kvbag_dump(world_p, &world_p->istr_bag);
#endif

    return O71_OK;
}

/* o71_ics ******************************************************************/
O71_API o71_status_t o71_ics
(
    o71_world_t * world_p,
    o71_ref_t * str_rp,
    char const * cstr_a
)
{
    o71_status_t os, dos;
    o71_ref_t rostr_r;

    M("ics('%s')", cstr_a);
    os = o71_rocs(world_p, &rostr_r, cstr_a);
    if (os) return os;

    os = o71_str_intern(world_p, rostr_r, str_rp);
    M("str(obref_%lX)=rocs('%s') -> is(obref_%lX): %s",
      (long) rostr_r, cstr_a, (long) *str_rp, N(os));
    dos = o71_deref(world_p, rostr_r);
#if O71_CHECKED
    if (dos) return dos;
#else
    (void) dos;
#endif

    return os;
}

/* o71_prep_call *****************************************************************/
O71_API o71_status_t o71_prep_call
(
    o71_flow_t * flow_p,
    o71_ref_t func_r,
    o71_ref_t * arg_ra,
    size_t arg_n
)
{
    o71_status_t os;
    o71_world_t * world_p = flow_p->world_p;
    o71_function_t * func_p;

    if (!(o71_model(flow_p->world_p, func_r) & O71M_FUNCTION))
    {
        /* TODO: prepare exception */
        return O71_BAD_FUNC_REF;
    }
    func_p = world_p->obj_pa[O71_REF_TO_MOX(func_r)];
    os = func_p->call(flow_p, func_r, arg_ra, arg_n);
    return os;
}

/* o71_run ******************************************************************/
O71_API o71_status_t o71_run
(
    o71_flow_t * flow_p,
    unsigned int min_depth,
    uint32_t steps
)
{
    o71_exe_ctx_t * exe_ctx_p;
    o71_function_t * func_p;
    o71_status_t os;
    o71_ref_t exe_ctx_r;

    A(steps <= O71_STEPS_MAX);
    flow_p->crt_steps = 0;
    flow_p->max_steps = steps;
    while (flow_p->depth > min_depth)
    {
        /*
        if (flow_p->exc_r != O71R_NULL)
        {
            M("TODO: call run in exc handling mode");
            return O71_TODO;
        }
        */

        if (flow_p->crt_steps >= flow_p->max_steps)
        {
            M("reached max steps: crt=%u, max=%u",
              flow_p->crt_steps, flow_p->max_steps);
            return O71_PENDING;
        }

        exe_ctx_r = flow_p->exe_ctx_r;
        exe_ctx_p = o71_obj_ptr(flow_p->world_p, exe_ctx_r);
        func_p = o71_obj_ptr(flow_p->world_p, exe_ctx_p->hdr.class_r);
        os = func_p->run(flow_p);
        M("flow=%p, func=%p => run -> %s", flow_p, func_p, N(os));
        A(exe_ctx_r == flow_p->exe_ctx_r);
        switch (os)
        {
        case O71_EXC:
            A(flow_p->exc_r != O71R_NULL);
            /* fall to ok to unwind stack */
        case O71_OK:
            {
                //size_t size;
                flow_p->exe_ctx_r = exe_ctx_p->caller_r;
                flow_p->depth -= 1;
                //size = exe_ctx_p->size;
                //os = redim(flow_p->world_p, (void * *) &exe_ctx_p, &size, 0, 1);
                os = o71_deref(flow_p->world_p, exe_ctx_r);
                AOS(os);
            }
            break;
        case O71_PENDING:
            break;
        default:
            M("flow_%u: function returned status %s", flow_p->flow_id, N(os));
            return os;
        }
    }
    return flow_p->exc_r == O71R_NULL ? O71_OK : O71_EXC;
}

/* o71_sfunc_create *********************************************************/
O71_API o71_status_t o71_sfunc_create
(
    o71_world_t * world_p,
    o71_ref_t * sfunc_rp,
    uint32_t arg_n
)
{
    o71_status_t os, osf;
    o71_obj_index_t sfunc_x;
    o71_script_function_t * sfunc_p;
    size_t i;

    os = alloc_object(world_p, O71R_SCRIPT_FUNCTION_CLASS, &sfunc_x);
    if (os)
    {
        M("failed to allocate sfunc object: %s", N(os));
        return os;
    }
    sfunc_p = world_p->obj_pa[sfunc_x];
    sfunc_p->func.cls.finish = noop_object_finish;
    sfunc_p->func.cls.super_ra = NULL;
    sfunc_p->func.cls.fix_field_ofs_a = NULL;
    sfunc_p->func.cls.get_field = get_missing_field;
    sfunc_p->func.cls.set_field = set_missing_field;
    kvbag_init(&sfunc_p->func.cls.method_bag, O71_METHOD_ARRAY_LIMIT);
    sfunc_p->func.cls.super_n = 0;
    sfunc_p->func.cls.object_size = 0; // this will be set by sfunc_validate
    sfunc_p->func.cls.dyn_field_ofs = 0;
    sfunc_p->func.cls.fix_field_n = 0;
    sfunc_p->func.cls.model = O71MI_EXE_CTX;
    sfunc_p->func.call = sfunc_call;
    sfunc_p->func.run = sfunc_run;
    sfunc_p->insn_a = NULL;
    sfunc_p->opnd_a = NULL;
    sfunc_p->arg_xa = NULL;
    sfunc_p->const_ra = NULL;
    sfunc_p->exc_handler_a = NULL;
    sfunc_p->exc_chain_start_xa = init_exc_chain_start_xa;
    sfunc_p->var_n = 0;
    sfunc_p->arg_n = 0;
    sfunc_p->insn_n = 0;
    sfunc_p->insn_m = 0;
    sfunc_p->opnd_n = 0;
    sfunc_p->opnd_m = 0;
    sfunc_p->const_n = 0;
    sfunc_p->const_m = 0;
    sfunc_p->exc_handler_n = 0;
    sfunc_p->exc_handler_m = 0;
    sfunc_p->exc_chain_n = 1;
    sfunc_p->exc_chain_m = 0;
    sfunc_p->valid = 0;
    os = redim(world_p->allocator_p, (void * *) &sfunc_p->arg_xa,
               &sfunc_p->arg_n, arg_n, sizeof(uint32_t));
    if (os)
    {
        M("failed to allocate arg index array: %s", N(os));
        osf = free_object(world_p, sfunc_x);
        if (osf)
        {
            M("FATAL: free object index: os=%s", N(osf));
            return osf;
        }
        return os;
    }

    for (i = 0; i < arg_n; ++i)
        sfunc_p->arg_xa[i] = (uint32_t) i;

    *sfunc_rp = O71_MOX_TO_REF(sfunc_x);

    return O71_OK;
}

/* o71_sfunc_append_init ****************************************************/
O71_API o71_status_t o71_sfunc_append_init
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t dest_vx,
    o71_ref_t const_r
)
{
    o71_insn_t * insn_p;
    uint32_t * opnd_a;
    o71_status_t os;
    uint32_t cx;
    uint16_t opnd_x;

    os = sfunc_add_const(world_p, sfunc_p, &cx, const_r);
    if (os)
    {
        M("failed to add constant for INIT insn for sfunc %p: %s",
          sfunc_p, N(os));
        return os;
    }

    opnd_x = (uint16_t) sfunc_p->opnd_n;
    os = sfunc_alloc_code(world_p, sfunc_p, &insn_p, &opnd_a, 1, 2);
    if (os)
    {
        M("failed to allocate code for INIT insn in sfunc %p: %s",
          sfunc_p, N(os));
        return os;
    }

    insn_p->opcode = O71O_INIT;
    insn_p->exc_chain_x = 0;
    insn_p->opnd_x = opnd_x;
    opnd_a[0] = dest_vx;
    opnd_a[1] = cx;

    return O71_OK;
}

/* o71_sfunc_append_ret *****************************************************/
O71_API o71_status_t o71_sfunc_append_ret
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t value_vx
)
{
    o71_insn_t * insn_p;
    uint32_t * opnd_p;
    uint16_t opnd_x;
    o71_status_t os;

    opnd_x = sfunc_p->opnd_n;
    os = sfunc_alloc_code(world_p, sfunc_p, &insn_p, &opnd_p, 1, 1);
    if (os)
    {
        M("sfunc=%p: alloc code failed: %s", sfunc_p, N(os));
        return os;
    }
    insn_p->opcode = O71O_RETURN;
    insn_p->exc_chain_x = 0;
    insn_p->opnd_x = opnd_x;
    *opnd_p = value_vx;

    return O71_OK;
}

/* o71_sfunc_append_call ****************************************************/
O71_API o71_status_t o71_sfunc_append_call
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t dest_vx,
    uint32_t func_vx,
    uint32_t arg_n,
    uint32_t * * arg_vxap
)
{
    o71_insn_t * insn_p;
    uint32_t * opnd_a;
    o71_status_t os;
    uint16_t opnd_x;

    opnd_x = (uint16_t) sfunc_p->opnd_n;
    if (arg_n + 3 < 3)
    {
        M("sfunc=%p: bad arg_n=0x%X", sfunc_p, arg_n);
        return O71_ARRAY_LIMIT;
    }

    os = sfunc_alloc_code(world_p, sfunc_p, &insn_p, &opnd_a, 1, arg_n + 3);
    if (os) return os;
    insn_p->opcode = O71O_CALL;
    insn_p->exc_chain_x = 0;
    insn_p->opnd_x = opnd_x;
    opnd_a[0] = dest_vx;
    opnd_a[1] = func_vx;
    opnd_a[2] = arg_n;
    *arg_vxap = opnd_a + 3;
    return O71_OK;
}

/* o71_sfunc_append_get_method **********************************************/
O71_API o71_status_t o71_sfunc_append_get_method
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t value_vx,
    uint32_t obj_vx,
    uint32_t name_istr_vx
)
{
    return sfunc_append_opc_val_obj_name(world_p, sfunc_p, O71O_GET_METHOD,
                                         value_vx, obj_vx, name_istr_vx);
}

/* o71_sfunc_append_get_field ***********************************************/
O71_API o71_status_t o71_sfunc_append_get_field
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t value_vx,
    uint32_t obj_vx,
    uint32_t name_istr_vx
)
{
    return sfunc_append_opc_val_obj_name(world_p, sfunc_p, O71O_GET_FIELD,
                                         value_vx, obj_vx, name_istr_vx);
}

/* o71_sfunc_append_set_field ***********************************************/
O71_API o71_status_t o71_sfunc_append_set_field
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t value_vx,
    uint32_t obj_vx,
    uint32_t name_istr_vx
)
{
    return sfunc_append_opc_val_obj_name(world_p, sfunc_p, O71O_SET_FIELD,
                                         value_vx, obj_vx, name_istr_vx);
}

/* o71_alloc_exc_chain ******************************************************/
O71_API o71_status_t o71_alloc_exc_chain
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t * exc_chain_xp,
    o71_exc_handler_t * * exc_handler_ap,
    size_t exc_handler_n
)
{
    o71_status_t os;
    size_t ecn, ehn;
    *exc_chain_xp = sfunc_p->exc_chain_n;
    ecn = sfunc_p->exc_chain_n + 1;
    if (ecn >= sfunc_p->exc_chain_m)
    {
        size_t new_m;
        new_m = (ecn + 4) & ~3;
        os = redim(world_p->allocator_p,
                   (void * *) &sfunc_p->exc_chain_start_xa,
                   &sfunc_p->exc_chain_m, new_m,
                   sizeof(sfunc_p->exc_chain_start_xa[0]));
        if (os) { M("ouch: %s", N(os)); return os; }
        if (sfunc_p->exc_chain_n == 1)
        {
            sfunc_p->exc_chain_start_xa[0] = 0;
            sfunc_p->exc_chain_start_xa[1] = 0;
        }
    }

    ehn = sfunc_p->exc_handler_n + exc_handler_n;
    if (ehn > sfunc_p->exc_handler_m)
    {
        size_t ehm = (ehn + 3) &~3;
        os = redim(world_p->allocator_p, (void * *) &sfunc_p->exc_handler_a,
                   &sfunc_p->exc_handler_m, ehm,
                   sizeof(sfunc_p->exc_handler_a[0]));
        if (os) { M("ouch: %s", N(os)); return os; }
    }

    M("ecsxa[%zu]=%u, ehn=%zu", sfunc_p->exc_chain_n,
      sfunc_p->exc_chain_start_xa[sfunc_p->exc_chain_n],
      sfunc_p->exc_handler_n);
    A(sfunc_p->exc_chain_start_xa[sfunc_p->exc_chain_n]
      == sfunc_p->exc_handler_n);

    *exc_handler_ap = &sfunc_p->exc_handler_a[sfunc_p->exc_handler_n];
    sfunc_p->exc_handler_n += exc_handler_n;

    sfunc_p->exc_chain_n++;
    sfunc_p->exc_chain_start_xa[sfunc_p->exc_chain_n] = sfunc_p->exc_handler_n;

    return O71_OK;
}

/* o71_set_exc_chain ********************************************************/
O71_API o71_status_t o71_set_exc_chain
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t first_insn_x,
    uint32_t last_insn_x,
    uint32_t exc_chain_x
)
{
    uint32_t i;
    M("ix:[%u, %u], ecx=%u", first_insn_x, last_insn_x, exc_chain_x);
    if (first_insn_x >= sfunc_p->insn_n || last_insn_x >= sfunc_p->insn_n
        || first_insn_x > last_insn_x)
        return O71_BAD_INSN_INDEX;
    if (exc_chain_x >= sfunc_p->exc_chain_n)
        return O71_BAD_EXC_CHAIN_INDEX;
    for (i = first_insn_x; i <= last_insn_x; ++i)
        sfunc_p->insn_a[i].exc_chain_x = exc_chain_x;
    return O71_OK;
}

/* o71_sfunc_validate *******************************************************/
O71_API o71_status_t o71_sfunc_validate
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p
)
{
    size_t i, j, nv, nvv = 0, nc, opnd_x, last_opnd_x, vx, var_n = 0;
    uint8_t has_var_list;

    /*
    M("validating sfunc=%p, an=%zu, vn=%zu, in=%zu, on=%zu, cn=%zu",
      sfunc_p, sfunc_p->arg_n, sfunc_p->var_n, sfunc_p->insn_n, sfunc_p->opnd_n,
      sfunc_p->const_n);
      */
    for (i = 0; i < sfunc_p->arg_n; ++i)
    {
        vx = sfunc_p->arg_xa[i];
        if (vx >= O71_VAR_LIMIT)
        {
            M("sfunc=%p: arg %zu has invalid var index 0x%zu",
              sfunc_p, i, vx);
            return O71_BAD_VAR_INDEX;
        }
        if (vx >= var_n) var_n = vx + 1;
    }

    if (!sfunc_p->insn_n)
    {
        M("sfunc=%p: no instructions to execute!", sfunc_p);
        return O71_BAD_INSN_INDEX;
    }

    if (sfunc_p->insn_a[sfunc_p->insn_n - 1].opcode < O71O__NO_FALL)
    {
        M("sfunc=%p: last insn does not guarantee changing the flow "
          "(last opcode = 0x%02X)!", sfunc_p,
          sfunc_p->insn_a[sfunc_p->insn_n - 1].opcode);
        return O71_BAD_OPCODE;
    }

    for (i = 0; i < sfunc_p->insn_n; ++i)
    {
        M("sfunc=%p: insn %zu: opcode=0x%02X, ox=%u", sfunc_p, i,
          sfunc_p->insn_a[i].opcode, sfunc_p->insn_a[i].opnd_x);
#define X(_o, _v, _c, _has_vlist) \
    case _o: nv = _v; nc = _c; has_var_list = _has_vlist; break;
        switch (sfunc_p->insn_a[i].opcode)
        {
            X(O71O_NOP, 0, 0, 0);
            X(O71O_INIT, 1, 1, 0);
            X(O71O_RETURN, 1, 0, 0);
            X(O71O_CALL, 2, 0, 1);
            X(O71O_GET_METHOD, 3, 0, 0);
            X(O71O_GET_FIELD, 3, 0, 0);
            X(O71O_SET_FIELD, 3, 0, 0);
        default:
            M("sfunc=%p: insn %zu: bad opcode 0x%X",
              sfunc_p, i, sfunc_p->insn_a[i].opcode);
            return O71_BAD_OPCODE;
        }
#undef X
        opnd_x = sfunc_p->insn_a[i].opnd_x;
        last_opnd_x = opnd_x + nv + nc + has_var_list - 1;
        if (last_opnd_x != (uint16_t) last_opnd_x ||
            last_opnd_x >= sfunc_p->opnd_n)
        {
            //M("sfunc=%p: insn %zu: bad operand index 0x%zu", sfunc_p, i, opnd_x);
            return O71_BAD_OPERAND_INDEX;
        }
        if (has_var_list)
        {
            nvv = sfunc_p->opnd_a[last_opnd_x];
            if (last_opnd_x + 1 + nvv <= last_opnd_x)
                return O71_BAD_OPERAND_INDEX;
            last_opnd_x += nvv + 1;
            if (last_opnd_x != (uint16_t) last_opnd_x ||
                last_opnd_x >= sfunc_p->opnd_n)
            {
                M("sfunc=%p: insn %zu: bad operand index %zu",
                  sfunc_p, i, last_opnd_x);
                return O71_BAD_OPERAND_INDEX;
            }
        }
        for (j = 0; j < nv; ++j, ++opnd_x)
        {
            vx = sfunc_p->opnd_a[opnd_x];
            if (vx >= O71_VAR_LIMIT)
            {
                M("sfunc=%p: operand %zu has invalid var index 0x%zX",
                  sfunc_p, opnd_x, vx);
                return O71_BAD_VAR_INDEX;
            }
            if (vx >= var_n) var_n = vx + 1;
        }
        for (j = 0; j < nc; ++j, ++opnd_x)
        {
            if (sfunc_p->opnd_a[opnd_x] >= sfunc_p->const_n)
            {
                M("sfunc=%p: operand %zu has invalid const index 0x%X "
                  "(0x%zX limit)",
                  sfunc_p, opnd_x, sfunc_p->opnd_a[opnd_x], sfunc_p->const_n);
                return O71_BAD_CONST_INDEX;
            }
        }
        if (has_var_list)
        {
            A(nvv == sfunc_p->opnd_a[opnd_x]);
            ++opnd_x;
            for (j = 0; j < nvv; ++j, ++opnd_x)
            {
                vx = sfunc_p->opnd_a[opnd_x];
                if (vx >= O71_VAR_LIMIT)
                {
                    M("sfunc=%p: operand %zu has invalid var index 0x%zX",
                      sfunc_p, opnd_x, vx);
                    return O71_BAD_VAR_INDEX;
                }
                if (vx >= var_n) var_n = vx + 1;
            }
        }
    }
    sfunc_p->var_n = var_n;
    sfunc_p->func.cls.object_size = sizeof(o71_script_exe_ctx_t)
        + sizeof(o71_ref_t) * sfunc_p->var_n;
    M("computed var count: %zu; object size: %zu",
      var_n, sfunc_p->func.cls.object_size);
    sfunc_p->valid = 1;
    return O71_OK;
}

/* o71_superclass_search ****************************************************/
O71_API ptrdiff_t o71_superclass_search
(
    o71_world_t * world_p,
    o71_ref_t class_r,
    o71_ref_t superclass_r
)
{
    o71_class_t * class_p;
    ptrdiff_t a, b, c;
    class_p = o71_obj_ptr(world_p, class_r);
    for (a = 0, b = class_p->super_n - 1; a <= b; )
    {
        c = (a + b) >> 1;
        if (superclass_r == class_p->super_ra[c]) return c;
        if (superclass_r < class_p->super_ra[c]) b = c - 1;
        else a = c + 1;
    }
    return -1;
}

/* o71_reg_obj_create *******************************************************/
O71_API o71_status_t o71_reg_obj_create
(
    o71_world_t * world_p,
    o71_ref_t class_r,
    o71_ref_t * reg_obj_rp
)
{
    o71_status_t os;
    o71_obj_index_t reg_obj_x;
    o71_reg_obj_t * reg_obj_p;
    o71_class_t * class_p;
    unsigned int i;

    class_p = o71_obj_ptr(world_p, class_r);
    A(class_p->model & O71M_MEM_OBJ);
    os = alloc_object(world_p, class_r, &reg_obj_x);
    if (os)
    {
        M("alloc_object failed: %s", N(os));
        return os;
    }
    *reg_obj_rp = O71_MOX_TO_REF(reg_obj_x);
    reg_obj_p = world_p->obj_pa[reg_obj_x];
    M("class obref_%lX: dfo=0x%lX", class_r, class_p->dyn_field_ofs);
    if (class_p->dyn_field_ofs)
    {
        M("init dyn field bag %p at ofs 0x%lX in dyn obj at obref_%lX",
          ((uint8_t *) reg_obj_p + class_p->dyn_field_ofs),
          class_p->dyn_field_ofs, *reg_obj_rp);
        kvbag_init((o71_kvbag_t *)
                   ((uint8_t *) reg_obj_p + class_p->dyn_field_ofs), 0x10);
    }

    for (i = 0; i < class_p->fix_field_n; ++i)
        *(o71_ref_t *) ((uint8_t *) reg_obj_p +
                        O71_REF_TO_SINT(class_p->fix_field_ofs_a[i].value_r))
            = O71R_NULL;

    return O71_OK;
}

/* o71_reg_obj_get_field ****************************************************/
O71_API o71_status_t o71_reg_obj_get_field
(
    o71_world_t * world_p,
    o71_ref_t obj_r,
    o71_ref_t field_istr_r,
    o71_ref_t * value_rp
)
{
    o71_kvbag_loc_t loc;
    o71_kvbag_t * kvbag_p;
    o71_mem_obj_t * obj_p;
    o71_class_t * class_p;
    intptr_t a, b, c;
    o71_status_t os;

    A(o71_istr_check(world_p, field_istr_r) == O71_OK);

    class_p = o71_class(world_p, obj_r);
    if (!(class_p->model & O71M_MEM_OBJ))
    {
        /* non-memory objects don't have fields */
        return O71_MISSING;
    }

    obj_p = o71_obj_ptr(world_p, obj_r);
    a = 0;
    b = (intptr_t) class_p->fix_field_n - 1;
    while (a <= b)
    {
        o71_ref_t key_r;
        c = (a + b) >> 1;
        key_r = class_p->fix_field_ofs_a[c].key_r;
        if (field_istr_r == key_r)
        {
            /* found fixed field */
            *value_rp = *(o71_ref_t *) ((uint8_t *) obj_p +
                                        class_p->fix_field_ofs_a[c].value_r);
            return O71_OK;
        }
        if (field_istr_r < key_r) b = c - 1;
        else a = c + 1;
    }
    /* put field in the dynamic bag */
    if (!class_p->dyn_field_ofs)
    {
        /* no bag for dynamic fields; return field is missing */
        return O71_MISSING;
    }

    kvbag_p = (o71_kvbag_t *) ((uint8_t *) obj_p + class_p->dyn_field_ofs);
    M2("obref_%lX dfo=0x%lX", (long) obj_r, (long) class_p->dyn_field_ofs);
    os = kvbag_search(world_p, kvbag_p, field_istr_r, ref_cmp, NULL, &loc);
    if (os == O71_OK)
    {
        *value_rp = kvbag_get_loc_value(world_p, kvbag_p, &loc);
        M2("obref_%lX.obref_%lX -> obref_%lX", obj_r, field_istr_r, *value_rp);
    }

    return os;
}

/* o71_reg_obj_set_field ****************************************************/
O71_API o71_status_t o71_reg_obj_set_field
(
    o71_world_t * world_p,
    o71_ref_t obj_r,
    o71_ref_t field_istr_r,
    o71_ref_t value_r
)
{
    o71_mem_obj_t * obj_p;
    o71_class_t * class_p;
    intptr_t a, b, c;
    o71_status_t os;

    A(o71_istr_check(world_p, field_istr_r) == O71_OK);

    class_p = o71_class(world_p, obj_r);
    if (!(class_p->model & O71M_MEM_OBJ))
    {
        /* non-memory objects don't have fields */
        return O71_MISSING;
    }

    obj_p = o71_obj_ptr(world_p, obj_r);
    a = 0;
    b = (intptr_t) class_p->fix_field_n - 1;
    while (a <= b)
    {
        o71_ref_t key_r;
        c = (a + b) >> 1;
        key_r = class_p->fix_field_ofs_a[c].key_r;
        if (field_istr_r == key_r)
        {
            /* found fixed field */
            o71_ref_t * value_rp;
            M("storing obref_%lX into fixed field at ofs %lX of obref_%lX",
              (long) value_r, (long) class_p->fix_field_ofs_a[c].value_r,
              (long) obj_r);
            value_rp = (o71_ref_t *) ((uint8_t *) obj_p
                + (intptr_t) class_p->fix_field_ofs_a[c].value_r);
            os = set_var(world_p, value_rp, value_r);
            AOS(os);
            return O71_OK;
        }
        if (field_istr_r < key_r) b = c - 1;
        else a = c + 1;
    }
    /* put field in the dynamic bag */
    if (!class_p->dyn_field_ofs)
    {
        /* no bag for dynamic fields; return field is missing */
        return O71_MISSING;
    }

    M2("obref_%lX dfo=0x%lX", (long) obj_r, (long) class_p->dyn_field_ofs);
    os = kvbag_put(world_p, (o71_kvbag_t *)
                   ((uint8_t *) obj_p + class_p->dyn_field_ofs),
                   field_istr_r, value_r, ref_cmp, NULL);
    return os;
}

/* o71_istr_check ***********************************************************/
O71_API o71_status_t o71_istr_check
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    uint32_t model;
    model = o71_model(world_p, obj_r);
    if ((model & O71M_STRING))
    {
        o71_string_t * str_p;
        str_p = o71_obj_ptr(world_p, obj_r);
        return str_p->mode == O71_SM_INTERN
            ? O71_OK : O71_BAD_INTERN_STRING_REF;
    }
    if (model == O71M_INVALID) return O71_UNUSED_MEM_OBJ_SLOT;
    return (model & O71M_MEM_OBJ) ? O71_BAD_STRING_REF : O71_NOT_MEM_OBJ_REF;
}

/* o71_reg_class_create *****************************************************/
O71_API o71_status_t o71_reg_class_create
(
    o71_world_t * world_p,
    o71_ref_t * fix_field_istr_ra,
    size_t fix_field_n,
    o71_ref_t * class_rp
)
{
    o71_obj_index_t class_x;
    o71_class_t * class_p;
    o71_status_t os;
    size_t i;
    o71_ref_t super_ra[2] = { O71R_OBJECT_CLASS, O71R_REG_OBJ_CLASS };
    os = alloc_object(world_p, O71R_CLASS_CLASS, &class_x);
    if (os) { M("fail: %s", N(os)); return os; }
    *class_rp = O71_MOX_TO_REF(class_x);
    class_p = world_p->obj_pa[class_x];
    class_p->model = O71MI_MEM_OBJ;
    class_p->super_ra = NULL;
    class_p->super_n = 0;
    class_p->fix_field_ofs_a = NULL;
    class_p->fix_field_n = 0;
    class_p->finish = noop_object_finish;
    class_p->get_field = get_reg_obj_field;
    class_p->set_field = set_reg_obj_field;
    class_p->object_size = sizeof(o71_reg_obj_t)
        + sizeof(o71_ref_t) * fix_field_n;
    class_p->dyn_field_ofs = FIELD_OFS(o71_reg_obj_t, dyn_field_bag);
    kvbag_init(&class_p->method_bag, 0x10);
    os = class_super_extend(world_p, class_p, super_ra, ITEM_COUNT(super_ra));
    if (os)
    {
        M("fail: %s", N(os));
        free_object(world_p, class_x);
        return os;
    }
    os = redim(world_p->allocator_p, (void * *) &class_p->fix_field_ofs_a,
               &class_p->fix_field_n, fix_field_n, sizeof(o71_kv_t));
    if (os)
    {
        o71_status_t os2;
        M("fail: %s", N(os));
        os2 = redim(world_p->allocator_p,
                    (void * *) &class_p->super_ra, &class_p->super_n,
                    0, sizeof(o71_ref_t));
        AOS(os2);
        free_object(world_p, class_x);
        return os;
    }
    for (i = 0; i < fix_field_n; ++i)
    {
        class_p->fix_field_ofs_a[i].key_r = fix_field_istr_ra[i];
        class_p->fix_field_ofs_a[i].value_r =
            FIELD_OFS(o71_reg_obj_t, fix_field_a[i]);
    }
    refkv_qsort(class_p->fix_field_ofs_a, class_p->fix_field_n);
    return O71_OK;
}

/* o71_compile **************************************************************/
O71_API o71_status_t o71_compile
(
    o71_code_t * code_p,
    o71_allocator_t * allocator_p,
    char const * src_name,
    uint8_t const * src_a,
    size_t src_n
)
{
    o71_status_t os;
    o71_token_t * crt_token_p;

    code_p->allocator_p = allocator_p;
    code_p->src_name = src_name;
    code_p->src_a = src_a;
    code_p->src_n = src_n;
    code_p->token_list = NULL;
    code_p->token_tail = &code_p->token_list;

    os = tokenize_source(code_p);
    if (os) return os;

    code_p->ce_ofs = 0;

    for (crt_token_p = code_p->token_list; crt_token_p->type != O71_TT_END;)
    {
        os = match_stmt(code_p, crt_token_p);
        if (os) return os;
    }

    return O71_OK;
}

/* match_stmt ***************************************************************/
static o71_status_t match_stmt
(
    o71_code_t * code_p,
    o71_token_t * in_p
)
{
    MP("tokens: "); dump_token_list(in_p, 3); P("\n");
    return O71_TODO;
}


/* o71_code_free ************************************************************/
O71_API o71_status_t o71_code_free
(
    o71_code_t * code_p
)
{
    o71_status_t os;
    size_t i;
    o71_token_t * t, * n;
    for (t = code_p->token_list; t; t = n)
    {
        n = t->next;
        os = free_token(code_p, t, 0);
        AOS(os);
    }
    return O71_OK;
}


/* log2_rounded_up **********************************************************/
static uint8_t log2_rounded_up
(
    uintptr_t n
)
{
    uint8_t i;
    /* TODO: change this to lookup tables */
    for (i = 0; n; ++i, n >>= 1);
    return i;
}

/* redim ********************************************************************/
static o71_status_t redim
(
    o71_allocator_t * allocator_p,
    void * * data_pp,
    size_t * crt_count_p,
    size_t new_count,
    size_t item_size
)
{
    size_t old_count;
    o71_status_t os;
    if (new_count > SIZE_MAX / item_size) return O71_ARRAY_LIMIT;
    old_count = *crt_count_p;
    if (new_count > old_count
        && (new_count - old_count) * item_size
            > allocator_p->mem_limit - allocator_p->mem_usage)
    {
        M("reached mem limit");
        return O71_MEM_LIMIT;
    }
    os = allocator_p->realloc(data_pp, old_count * item_size,
                              new_count * item_size, allocator_p->context);
    A(os == O71_OK || os == O71_NO_MEM || os >= O71_FATAL);
    if (os)
    {
        M("realloc failed: %u", os);
        return os;
    }
    /* allocated block must be pointer aligned */
    A(((uintptr_t) *data_pp & (sizeof(uintptr_t) - 1)) == 0);
    *crt_count_p = new_count;
    allocator_p->mem_usage += (new_count - old_count) * item_size;
    if (allocator_p->mem_usage > allocator_p->mem_peak)
        allocator_p->mem_peak = allocator_p->mem_usage;
    return O71_OK;
}

/* extend_array *************************************************************/
static o71_status_t extend_array
(
    o71_allocator_t * allocator_p,
    void * * extra_ap,
    void * * data_ap,
    size_t * crt_alloc_count_p,
    size_t * crt_used_count_p,
    size_t extra_count,
    size_t item_size
)
{
    size_t used_n, new_n, new_m;
    o71_status_t os;

    used_n = *crt_used_count_p;
    new_n = extra_count + used_n;
    if (new_n < used_n || (ptrdiff_t) new_n < 0) return O71_ARRAY_LIMIT;

    if (new_n > *crt_alloc_count_p)
    {
        new_m = 1 << log2_rounded_up(new_n);
        os = redim(allocator_p, data_ap, crt_alloc_count_p, new_m, item_size);
        if (os) return os;
    }
    *extra_ap = ((uint8_t *) *data_ap) + used_n * item_size;
    *crt_used_count_p += extra_count;

    return O71_OK;
}

/* flow_init ****************************************************************/
static void flow_init
(
    o71_world_t * world_p,
    o71_flow_t * flow_p
)
{
    flow_p->world_p = world_p;
    flow_p->exe_ctx_r = O71R_NULL;
    flow_p->value_r = O71R_NULL;
    flow_p->exc_r = O71R_NULL;
    flow_p->depth = 0;
    flow_p->flow_id = world_p->flow_id_seed++;
}

/* extend_object_table ******************************************************/
static o71_status_t extend_object_table
(
    o71_world_t * world_p
)
{
    size_t i, n, m;
    o71_status_t os;
    n = world_p->obj_n;
    if (n > (SIZE_MAX >> 1)) return O71_ARRAY_LIMIT;
    m = n ? n << 1 : 0x20;
    os = redim(world_p->allocator_p,
               (void * *) &world_p->obj_pa, &world_p->obj_n, m, sizeof(void *));
    if (os)
    {
        M("failed extending object table from %zu to %zu items", n, m);
        return os;
    }
    if (!n) n = O71X__COUNT;
    for (i = n; i < m; ++i)
        world_p->enc_next_free_xa[i] = ENCODE_FREE_OBJECT_SLOT(i + 1);
    //world_p->enc_next_free_xa[i] =
    //    ENCODE_FREE_OBJECT_SLOT(world_p->free_list_head_x);
    world_p->free_list_head_x = n;
    return O71_OK;
}

/* alloc_object_index *******************************************************/
static o71_status_t alloc_object_index
(
    o71_world_t * world_p,
    o71_obj_index_t * obj_xp
)
{
    o71_status_t os;
    if (world_p->free_list_head_x >= world_p->obj_n)
    {
        //M("free list head index: %lX", world_p->free_list_head_x);
        //M("obj num: %lX", world_p->obj_n);
        A(world_p->free_list_head_x == world_p->obj_n);
        os = extend_object_table(world_p);
        if (os) return os;
    }
    A(world_p->free_list_head_x < world_p->obj_n);
    *obj_xp = world_p->free_list_head_x;
    world_p->free_list_head_x = DECODE_FREE_OBJECT_SLOT(
        world_p->enc_next_free_xa[world_p->free_list_head_x]);
    M2("allocated=obref_%lX, free_head=obref_%lX, obj_n=%lX",
       (long) O71_MOX_TO_REF(*obj_xp),
       (long) O71_MOX_TO_REF(world_p->free_list_head_x), (long) world_p->obj_n);

    return O71_OK;
}

/* free_object_index ********************************************************/
static o71_status_t free_object_index
(
    o71_world_t * world_p,
    o71_obj_index_t obj_x
)
{
    A(obj_x < world_p->obj_n);
    world_p->enc_next_free_xa[obj_x] = ENCODE_FREE_OBJECT_SLOT(
        world_p->free_list_head_x);
    world_p->free_list_head_x = obj_x;
    return O71_OK;
}

/* alloc_object *************************************************************/
static o71_status_t alloc_object
(
    o71_world_t * world_p,
    o71_ref_t class_r,
    o71_obj_index_t * obj_xp
)
{
    o71_status_t os;
    o71_class_t * class_p;
    size_t obj_size;
    o71_obj_index_t obj_x;

    A(o71_model(world_p, class_r) & O71M_CLASS);

    os = alloc_object_index(world_p, obj_xp);
    if (os)
    {
        M("failed to alloc obj index: %s", N(os));
        return os;
    }
    obj_x = *obj_xp;
    class_p = world_p->obj_pa[O71_REF_TO_MOX(class_r)];
    obj_size = 0;
    os = redim(world_p->allocator_p, world_p->obj_pa + obj_x,
               &obj_size, class_p->object_size, 1);
    if (os)
    {
        M("failed to allocate memory for object instance: %s", N(os));
        free_object_index(world_p, obj_x);
        return (os);
    }

    os = o71_ref(world_p, class_r);
#if O71_CHECKED
    if (os)
    {
        M("ref(class=obref_%lX): %s", (long) class_r, N(os));
        return os;
    }
#else
    (void) os;
#endif

    world_p->mem_obj_pa[obj_x]->class_r = class_r;
    world_p->mem_obj_pa[obj_x]->ref_n = 1;

    return O71_OK;
}

/* free_object **************************************************************/
static o71_status_t free_object
(
    o71_world_t * world_p,
    o71_ref_t obj_x
)
{
    o71_class_t * class_p;
    o71_mem_obj_t * obj_p;
    size_t obj_size;
    o71_ref_t class_r;
    o71_status_t os;
    obj_p = world_p->obj_pa[obj_x];
    class_r = obj_p->class_r;
    class_p = world_p->obj_pa[O71_REF_TO_MOX(class_r)];
    obj_size = class_p->object_size;
    os = redim(world_p->allocator_p, world_p->obj_pa + obj_x, &obj_size, 0, 1);
    if (os)
    {
        M("*** BUG *** error freeing object memory: %s", N(os));
        return os;
    }
    os = free_object_index(world_p, obj_x);
#if O71_CHECKED
    if (os)
    {
        M("free_object_index(obref_%lX) failed: %s",
          (long) O71_MOX_TO_REF(obj_x), N(os));
        return os;
    }
#endif
    os = o71_deref(world_p, class_r);
    AOS(os);
    return O71_OK;
}

/* noop_object_finish *******************************************************/
o71_status_t noop_object_finish
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    M2("finished obref_%lX", (long) obj_r);
    return O71_OK;
}

/* alloc_exc ****************************************************************/
static o71_status_t alloc_exc
(
    o71_world_t * world_p,
    o71_ref_t class_r,
    o71_obj_index_t * obj_xp
)
{
    o71_class_t * class_p;
    o71_exception_t * exc_p;
    o71_status_t os;

    class_p = o71_obj_ptr(world_p, class_r);
    (void) class_p;
    A((class_p->model & O71M_EXCEPTION));
    os = alloc_object(world_p, class_r, obj_xp);
    if (os) return os;
    exc_p = world_p->obj_pa[*obj_xp];
    kvbag_init(&exc_p->dyn_field_bag, 0x10);
    exc_p->exe_ctx_r = O71R_NULL;

    return O71_OK;
}

/* get_missing_field ********************************************************/
static o71_status_t get_missing_field
(
    o71_flow_t * flow_p,
    o71_ref_t obj_r,
    o71_ref_t field_r,
    o71_ref_t * value_p
)
{
    return O71_MISSING;
}

/* set_missing_field ********************************************************/
static o71_status_t set_missing_field
(
    o71_flow_t * flow_p,
    o71_ref_t obj_r,
    o71_ref_t field_r,
    o71_ref_t value
)
{
    return O71_TODO;
}

/* null_func_run ************************************************************/
static o71_status_t null_func_run
(
    o71_flow_t * flow_p
)
{
    flow_p->value_r = O71R_NULL;
    return O71_OK;
}

/* int_add_call *************************************************************/
static o71_status_t int_add_call
(
    o71_flow_t * flow_p,
    o71_ref_t func_r,
    o71_ref_t * arg_ra,
    size_t arg_n
)
{
    o71_obj_index_t exc_ox;
    o71_status_t os;
    if (arg_n != 2)
    {
        // throw arity exception
        os = alloc_exc(flow_p->world_p, O71R_ARITY_EXC_CLASS, &exc_ox);
        if (os) return os;
        flow_p->exc_r = O71_MOX_TO_REF(exc_ox);
        flow_p->value_r = O71R_NULL;
        return O71_EXC;
    }
    if (O71_IS_REF_TO_SINT(arg_ra[0]) && O71_IS_REF_TO_SINT(arg_ra[1]))
    {
        // TODO: handle overflow by generating a big_int
        flow_p->value_r = (arg_ra[0] - 1 + arg_ra[1]);
        return O71_OK;
    }
    os = alloc_exc(flow_p->world_p, O71R_TYPE_EXC_CLASS, &exc_ox);
    flow_p->exc_r = O71_MOX_TO_REF(exc_ox);
    if (os) return os;
    return O71_EXC;
    // // TODO: handle int + big_int or big_int + int
    // flow_p->value_r = O71R_NULL;
    // return O71_OK;
}

/* sfunc_call ***************************************************************/
static o71_status_t sfunc_call
(
    o71_flow_t * flow_p,
    o71_ref_t func_r,
    o71_ref_t * arg_ra,
    size_t arg_n
)
{
    o71_world_t * world_p = flow_p->world_p;
    o71_script_function_t * sfunc_p;
    o71_script_exe_ctx_t * sec_p;
    o71_status_t os;
    o71_obj_index_t ec_ox;
    size_t i;

    A(o71_model(world_p, func_r) & O71M_SCRIPT_FUNCTION);
    sfunc_p = o71_obj_ptr(world_p, func_r);
    if (!sfunc_p->valid)
    {
        os = o71_sfunc_validate(world_p, sfunc_p);
        if (os)
        {
            M("sfunc_%lX=%p: failed to prepare call due to invalid code: %s",
              (long) func_r, sfunc_p, N(os));
            return os;
        }
    }
    if (arg_n != sfunc_p->arg_n)
    {
        M("sf%lX called with %lu args but needs %lu!",
          func_r, (long) arg_n, (long) sfunc_p->arg_n);
        return O71_BAD_ARG_COUNT;
    }

    os = alloc_object(world_p, func_r, &ec_ox);
    if (os)
    {
        M("failed to create exe_ctx for sfunc=obref_%lX: %s",
          (long) func_r, N(os));
        return os;
    }
    sec_p = world_p->obj_pa[ec_ox];
    sec_p->exe_ctx.caller_r = flow_p->exe_ctx_r;
    sec_p->ret_value_vx = -1;
    sec_p->insn_x = 0;
    for (i = 0; i < sfunc_p->var_n; ++i)
        sec_p->var_ra[i] = O71R_NULL;

    /* exe context borrows references from arg_ra */
    for (i = 0; i < sfunc_p->arg_n; ++i)
        sec_p->var_ra[sfunc_p->arg_xa[i]] = arg_ra[i];

    flow_p->exe_ctx_r = O71_MOX_TO_REF(ec_ox);
    flow_p->depth += 1;

    return O71_PENDING;
}

/* set_var ******************************************************************/
O71_INLINE o71_status_t set_var
(
    o71_world_t * world_p,
    o71_ref_t * dest_rp,
    o71_ref_t src_r
)
{
    o71_status_t os;
    A(o71_model(world_p, *dest_rp) != O71M_INVALID);
    A(o71_model(world_p, src_r) != O71M_INVALID);
    os = o71_ref(world_p, src_r);
#if O71_CHECKED
    if (os)
    {
        M("failed referencing src=obref_%lX: %s", (long) src_r, N(os));
        return os;
    }
#endif
    os = o71_deref(world_p, *dest_rp);
#if O71_CHECKED
    if (os)
    {
        M("failed dereferencing existing ref in dest=obref_%lX: %s",
          (long) *dest_rp, N(os));
        return os;
    }
#endif
    *dest_rp = src_r;
    return os;
}

/* sfunc_run ****************************************************************/
static o71_status_t sfunc_run
(
    o71_flow_t * flow_p
)
{
    o71_world_t * world_p = flow_p->world_p;
    o71_script_exe_ctx_t * sec_p;
    o71_script_function_t * sfunc_p;
    unsigned int ix, ox, ecx, ehx, ehx_lim;
    o71_exception_t * exc_p;
    o71_status_t os;
    o71_ref_t laa[0x40];
    o71_ref_t * aa;
    uint32_t ret_value_vx;

    A(flow_p->exe_ctx_r != O71R_NULL);
    sec_p = o71_obj_ptr(world_p, flow_p->exe_ctx_r);
    sfunc_p = o71_obj_ptr(world_p, sec_p->exe_ctx.hdr.class_r);
    ix = (unsigned int) sec_p->insn_x;
    if (flow_p->exc_r != O71R_NULL) goto l_exc;
    switch (sec_p->mode)
    {
    case O71_SECM_STORE_RET_VAL:
        ret_value_vx = sec_p->ret_value_vx;
        sec_p->mode = O71_SECM_RUN;
        goto l_store_ret_val;
    case O71_SECM_IGNORE_RET_VAL:
        sec_p->mode = O71_SECM_RUN;
        os = o71_deref(world_p, flow_p->value_r);
        AOS(os);
        ++ix;
        break;
    }

    for (;;)
    {
        if (sfunc_p->insn_a[ix].opcode >= O71O__CHFLOW)
        {
            flow_p->crt_steps += ix - sec_p->insn_x;
            sec_p->insn_x = ix;
            if (flow_p->crt_steps >= flow_p->max_steps)
            {
                M("reached max steps");
                return O71_PENDING;
            }
        }

        ox = sfunc_p->insn_a[ix].opnd_x;
        switch (sfunc_p->insn_a[ix].opcode)
        {
        case O71O_NOP:
            M("EXEC %04X: nop", ix);
            break;

        case O71O_INIT:
            {
                uint32_t dvx, cx;
                dvx = sfunc_p->opnd_a[ox];
                cx = sfunc_p->opnd_a[ox + 1];
                M("EXEC %04X: init v%X, c%X=obref_%lX",
                  ix, dvx, cx, sfunc_p->const_ra[cx]);
                os = set_var(world_p, &sec_p->var_ra[dvx],
                             sfunc_p->const_ra[cx]);
                AOS(os);
                break;
            }

        case O71O_GET_METHOD:
            {
                uint32_t dest_vx, obj_vx, name_istr_vx;
                o71_ref_t obj_r, name_istr_r, value_r;
                o71_class_t * class_p;
                o71_kvbag_loc_t loc;
                loc.rbtree.last_x = 0; // grr, to silence maybe-uninitialized

                dest_vx = sfunc_p->opnd_a[ox];
                obj_vx = sfunc_p->opnd_a[ox + 1];
                name_istr_vx = sfunc_p->opnd_a[ox + 2];
                A(name_istr_vx < sfunc_p->var_n);
                A(obj_vx < sfunc_p->var_n);
                name_istr_r = sec_p->var_ra[name_istr_vx];
                M("EXEC %04X: get_method dest:v%X, obj:v%X=obref_%lX, name:v%X=obref_%lX",
                  ix, dest_vx, obj_vx, sec_p->var_ra[obj_vx], name_istr_vx,
                  name_istr_r);
                if (!(o71_model(world_p, name_istr_r) & O71M_STRING))
                {
                    M("var_%X points to obref_%lX which is not a string",
                      name_istr_vx, name_istr_r);
                    M("TODO: throw exception");
                    return O71_TODO;
                }
                obj_r = sec_p->var_ra[obj_vx];
                class_p = o71_class(world_p, obj_r);
                A(class_p);
                os = kvbag_search(world_p, &class_p->method_bag, name_istr_r,
                                  ref_cmp, NULL, &loc);
                if (os)
                {
                    A(os == O71_MISSING);
                    M("class of obref_%lX does not have method name "
                      "str=obref_%lX", (long) obj_r, (long) name_istr_r);
                    M("TODO: throw exception");
                    return O71_TODO;
                }
                value_r = kvbag_get_loc_value(world_p, &class_p->method_bag,
                                              &loc);
                M("v%X <- method=obref_%lX", dest_vx, value_r);
                os = set_var(world_p, &sec_p->var_ra[dest_vx], value_r);
                AOS(os);
                break;
            }

        case O71O_RETURN:
            {
                uint32_t svx;
                svx = sfunc_p->opnd_a[ox];
                /* move the variable in flow's value slot, then erase the
                 * var so that when we clear the execution context we don't
                 * decrement the ref count for the returned reference */
                flow_p->value_r = sec_p->var_ra[svx];
                sec_p->var_ra[svx] = O71R_NULL;
                M("EXEC %04X: return v%X=obref_%lX", ix, svx, flow_p->value_r);
                // fall into clear context
            }
        //l_finish_context:
            {
                size_t i;
                for (i = 0; i < sfunc_p->var_n; ++i)
                {
                    os = o71_deref(world_p, sec_p->var_ra[i]);
                    AOS(os);
                }
                return O71_OK;
            }

        case O71O_CALL:
            {
                uint32_t fvx, an, i;
                fvx = sfunc_p->opnd_a[ox + 1];
                an = sfunc_p->opnd_a[ox + 2];
                M("EXEC %04X: call dest:v%X, func:v%X=obref_%lX, args:%u",
                  ix, sfunc_p->opnd_a[ox], fvx, sec_p->var_ra[fvx], an);
                if (an <= sizeof(laa) / sizeof(laa[0])) aa = &laa[0];
                else
                {
                    M("TODO: alloc arg table");
                    return O71_TODO;
                }
                for (i = 0; i < an; ++i)
                {
                    aa[i] = sec_p->var_ra[sfunc_p->opnd_a[ox + 3 + i]];
                    os = o71_ref(world_p, aa[i]);
                    M("arg[%u]: v%X=obref_%lX",
                      i, sfunc_p->opnd_a[ox + 3 + i], aa[i]);
                    AOS(os);
                }
                os = o71_prep_call(flow_p, sec_p->var_ra[fvx], aa, an);
                switch (os)
                {
                case O71_OK:
                    ret_value_vx = sfunc_p->opnd_a[ox];
                    break; // fall into l_store_ret_val
                case O71_PENDING:
                    sec_p->ret_value_vx = sfunc_p->opnd_a[ox];
                    sec_p->mode = O71_SECM_STORE_RET_VAL;
                    return O71_PENDING;
                case O71_EXC:
                    // exception thrown; try to handle it
                    goto l_exc;
                default:
                    M("unhandled prep_call status: %s", N(os));
                    return O71_BUG;
                }
            }

        l_store_ret_val:
            {
                M("storing return value obref_%lX into v%X",
                  (long) flow_p->value_r, ret_value_vx);
                os = o71_deref(world_p, sec_p->var_ra[ret_value_vx]);
                AOS(os);
                sec_p->var_ra[ret_value_vx] = flow_p->value_r;
                break;
            }

        case O71O_GET_FIELD:
            {
                uint32_t vvx, ovx, fvx;
                o71_class_t * class_p;
                o71_ref_t obj_r, value_r;
                vvx = sfunc_p->opnd_a[ox + 0];
                ovx = sfunc_p->opnd_a[ox + 1];
                fvx = sfunc_p->opnd_a[ox + 2];
                M("EXEC %04X: get_field "
                  "val:v%X=obref_%lX, obj:v%X=obref_%lX, field:v%X=obref_%lX",
                  ix, vvx, sec_p->var_ra[vvx], ovx, sec_p->var_ra[ovx],
                  fvx, sec_p->var_ra[fvx]);
                obj_r = sec_p->var_ra[ovx];
                class_p = o71_class(world_p, obj_r);
                os = class_p->get_field(flow_p, obj_r,
                                        sec_p->var_ra[fvx], &value_r);
                switch (os)
                {
                case O71_OK:
                    os = o71_deref(world_p, sec_p->var_ra[vvx]);
                    AOS(os);
                    sec_p->var_ra[vvx] = value_r;
                    M("store obref_%lX into v%X", value_r, vvx);
                    break;
                case O71_PENDING:
                    sec_p->mode = O71_SECM_STORE_RET_VAL;
                    sec_p->ret_value_vx = vvx;
                    return O71_PENDING;
                case O71_EXC:
                    goto l_exc;
                default:
                    M("unhandled set_field status: %s", N(os));
                    return O71_BUG;
                }
                break;
            }


        case O71O_SET_FIELD:
            {
                uint32_t vvx, ovx, fvx;
                o71_class_t * class_p;
                o71_ref_t obj_r;
                vvx = sfunc_p->opnd_a[ox + 0];
                ovx = sfunc_p->opnd_a[ox + 1];
                fvx = sfunc_p->opnd_a[ox + 2];
                M("EXEC %04X: set_field "
                  "val:v%X=obref_%lX, obj:v%X=obref_%lX, field:v%X=obref_%lX",
                  ix, vvx, sec_p->var_ra[vvx], ovx, sec_p->var_ra[ovx],
                  fvx, sec_p->var_ra[fvx]);
                obj_r = sec_p->var_ra[ovx];
                class_p = o71_class(world_p, obj_r);
                os = class_p->set_field(flow_p, obj_r,
                                        sec_p->var_ra[fvx], sec_p->var_ra[vvx]);
                switch (os)
                {
                case O71_OK:
                    break;
                case O71_PENDING:
                    sec_p->mode = O71_SECM_IGNORE_RET_VAL;
                    return O71_PENDING;
                case O71_EXC:
                    goto l_exc;
                default:
                    M("unhandled set_field status: %s", N(os));
                    return O71_BUG;
                }
                break;
            }

        default:
            M("unhandled opcode 0x%X", sfunc_p->insn_a[ix].opcode);
            return O71_TODO;

        l_exc:
        /* exception thrown; check to see if there's a handler for it
         * that covers current instruction */
            M("got exc=obref_%lX", (long) flow_p->exc_r);
            A(o71_model(world_p, flow_p->exc_r) & O71M_EXCEPTION);
            exc_p = o71_obj_ptr(world_p, flow_p->exc_r);
            ecx = sfunc_p->insn_a[ix].exc_chain_x;
            M("ix=%u, ecx=%u, ehx=[%u, %u)",
              ix, ecx, sfunc_p->exc_chain_start_xa[ecx],
              sfunc_p->exc_chain_start_xa[ecx + 1]);
            for (ehx = sfunc_p->exc_chain_start_xa[ecx],
                 ehx_lim = sfunc_p->exc_chain_start_xa[ecx + 1];
                 ehx < ehx_lim;
                 ++ehx)
                if (exc_p->hdr.class_r
                    == sfunc_p->exc_handler_a[ehx].exc_type_r
                    || o71_superclass_search(world_p, exc_p->hdr.class_r,
                                             sfunc_p->exc_handler_a[ehx]
                                                .exc_type_r) >= 0)
                    break;
            if (ehx == ehx_lim)
            {
                M("exc not handled. unwinding...");
                return O71_EXC;
            }
            /* store the exception */
            sec_p->var_ra[sfunc_p->exc_handler_a[ehx].exc_var_x]
                = flow_p->exc_r;
            flow_p->exc_r = O71R_NULL;
            ix = sfunc_p->exc_handler_a[ehx].insn_x;
            M("found handler %u; jump to ix=0x%X", ehx, ix);
            continue;
        }
        /* move to next instruction */
        ++ix;
    }
}

/* sfunc_alloc_code *********************************************************/
static o71_status_t sfunc_alloc_code
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    o71_insn_t * * insn_ap,
    uint32_t * * opnd_ap,
    size_t insn_n,
    size_t opnd_n
)
{
    o71_status_t os;
    size_t n;

    n = sfunc_p->insn_n + insn_n - 1;
    if (n != (uint32_t) n)
    {
        M("sfunc=%p: too many instructions to be referenced by operands",
          sfunc_p);
        return O71_ARRAY_LIMIT;
    }

    n = sfunc_p->opnd_n + opnd_n - 1;
    if (n != (uint16_t) n)
    {
        M("sfunc=%p: too many operands in to be accessible from instructions",
          sfunc_p);
        return O71_ARRAY_LIMIT;
    }

    os = extend_array(world_p->allocator_p,
                      (void * *) insn_ap, (void * *) &sfunc_p->insn_a,
                      &sfunc_p->insn_m, &sfunc_p->insn_n, insn_n,
                      sizeof(o71_insn_t));
    if (os)
    {
        M("sfunc=%p: failed extending insn array: %s", sfunc_p, N(os));
        return os;
    }
    M("sfunc=%p; insn: a=%p, n=%zu, m=%zu",
      sfunc_p, sfunc_p->insn_a, sfunc_p->insn_n, sfunc_p->insn_m);

    os = extend_array(world_p->allocator_p,
                      (void * *) opnd_ap, (void * *) &sfunc_p->opnd_a,
                      &sfunc_p->opnd_m, &sfunc_p->opnd_n, opnd_n,
                      sizeof(uint32_t));
    if (os)
    {
        M("sfunc=%p: failed extending operand array: %s", sfunc_p, N(os));
        sfunc_p->insn_n -= insn_n;
        return os;
    }
    M("sfunc=%p; opnd: a=%p, n=%zu, m=%zu",
      sfunc_p, sfunc_p->opnd_a, sfunc_p->opnd_n, sfunc_p->opnd_m);
    sfunc_p->valid = 0;
    return O71_OK;
}

/* sfunc_add_const **********************************************************/
static o71_status_t sfunc_add_const
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint32_t * const_xp,
    o71_ref_t obj_r
)
{
    size_t i;
    o71_ref_t * const_rp;
    o71_status_t os;

    for (i = 0; i < sfunc_p->const_n; ++i)
        if (sfunc_p->const_ra[i] == obj_r)
        {
            *const_xp = i;
            return O71_OK;
        }

    *const_xp = sfunc_p->const_n;
    os = extend_array(world_p->allocator_p, (void * *) &const_rp,
                      (void * *) &sfunc_p->const_ra,
                      &sfunc_p->const_m, &sfunc_p->const_n, 1,
                      sizeof(o71_ref_t));
    if (os) return os;
    *const_rp = obj_r;
    os = o71_ref(world_p, obj_r);
    return os;
}

/* sfunc_append_opc_val_obj_name ********************************************/
static o71_status_t sfunc_append_opc_val_obj_name
(
    o71_world_t * world_p,
    o71_script_function_t * sfunc_p,
    uint8_t opcode,
    uint32_t value_vx,
    uint32_t obj_vx,
    uint32_t name_istr_vx
)
{
    o71_insn_t * insn_p;
    uint32_t * opnd_a;
    uint16_t opnd_x;
    o71_status_t os;

    opnd_x = sfunc_p->opnd_n;
    os = sfunc_alloc_code(world_p, sfunc_p, &insn_p, &opnd_a, 1, 3);
    if (os)
    {
        M("sfunc=%p: alloc code failed: %s", sfunc_p, N(os));
        return os;
    }
    insn_p->opcode = opcode;
    insn_p->exc_chain_x = 0;
    insn_p->opnd_x = opnd_x;
    opnd_a[0] = value_vx;
    opnd_a[1] = obj_vx;
    opnd_a[2] = name_istr_vx;
    return O71_OK;
}

/* ref_cmp ******************************************************************/
static o71_status_t ref_cmp
(
    o71_world_t * world_p,
    o71_ref_t a_r,
    o71_ref_t b_r,
    void * ctx
)
{
    return a_r == b_r ? O71_EQUAL : (a_r > b_r ? O71_MORE : O71_LESS);
}

/* str_finish ***************************************************************/
static o71_status_t str_finish
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    o71_string_t * str_p;
    o71_status_t os;
    A(o71_model(world_p, obj_r) & O71M_STRING);
    str_p = o71_obj_ptr(world_p, obj_r);
    M2("remove string '%.*s' (mode=%u, m=%zu)",
       (int) str_p->n, str_p->a, str_p->mode, str_p->m);
#if O71_DEBUG >= 2
    kvbag_dump(world_p, &world_p->istr_bag);
#endif
    if (str_p->mode == O71_SM_INTERN)
    {
        o71_kvbag_loc_t loc;
        os = kvbag_search(world_p, &world_p->istr_bag, obj_r,
                          str_intern_cmp, NULL, &loc);
        AOS(os);
        os = kvbag_delete(world_p, &world_p->istr_bag, &loc);
        AOS(os);
#if O71_DEBUG >= 2
        kvbag_dump(world_p, &world_p->istr_bag);
        M("===================");
#endif
    }
    if (str_p->m)
    {
        os = redim(world_p->allocator_p, (void * *) &str_p->a, &str_p->m, 0, 1);
        AOS(os);
    }
    return O71_OK;
}


/* str_intern_cmp ***********************************************************/
static o71_status_t str_intern_cmp
(
    o71_world_t * world_p,
    o71_ref_t a_r,
    o71_ref_t b_r,
    void * ctx
)
{
    o71_string_t const * a_p;
    o71_string_t const * b_p;
    size_t i;

    a_p = o71_obj_ptr(world_p, a_r);
    b_p = o71_obj_ptr(world_p, b_r);
    if (a_p->n != b_p->n)
    {
        // M("'%.*s' %s '%.*s'", (int) a_p->n, a_p->a,
        //   a_p->n > b_p->n ? ">" : "<",
        //   (int) b_p->n, b_p->a);
        return (a_p->n > b_p->n ? O71_MORE : O71_LESS);
    }
    for (i = 0; i < a_p->n; ++i)
        if (a_p->a[i] != b_p->a[i])
        {
            // M("'%.*s' %s '%.*s'", (int) a_p->n, a_p->a,
            //   a_p->a[i] > b_p->a[i] ? ">" : "<",
            //   (int) b_p->n, b_p->a);
            return (a_p->a[i] > b_p->a[i] ? O71_MORE : O71_LESS);
        }
    // M("'%.*s' == '%.*s'", (int) a_p->n, a_p->a, (int) b_p->n, b_p->a);
    return O71_EQUAL;
}

/* kvbag_init ***************************************************************/
static void kvbag_init
(
    o71_kvbag_t * kvbag_p,
    uint8_t array_limit
)
{
    kvbag_p->kv_a = NULL;
    kvbag_p->n = 0;
    kvbag_p->m = 0;
    kvbag_p->l = array_limit;
    kvbag_p->mode = O71_BAG_ARRAY;
}

#if O71_DEBUG
/* kvbag_dump ***************************************************************/
static void kvbag_dump
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p
)
{
    if (kvbag_p->mode == O71_BAG_ARRAY) kvbag_array_dump(world_p, kvbag_p);
    else kvbag_rbtree_dump(world_p, kvbag_p->tree_p, 0);
}
#endif

/* kvbag_search *************************************************************/
static o71_status_t kvbag_search
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_cmp_f cmp,
    void * ctx,
    o71_kvbag_loc_t * loc_p
)
{
    M2("bag=%p, mode=%s, key=obref_%lX", kvbag_p,
       kvbag_p->mode ? "rbtree" : "array", key_r);
    if (kvbag_p->mode == O71_BAG_ARRAY)
        return kvbag_array_search(world_p, kvbag_p, key_r, cmp, ctx, loc_p);
    A(kvbag_p->mode == O71_BAG_RBTREE);
    return kvbag_rbtree_search(world_p, kvbag_p, key_r, cmp, ctx, loc_p);
}

/* kvbag_get_loc_value ******************************************************/
static o71_ref_t kvbag_get_loc_value
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p
)
{
#if O71_CHECKED
    if (loc_p->status != O71_OK)
    {
        M("*** BUG *** trying to get value after key match failed (%s)!!!!",
          N(loc_p->status));
        return O71_MOX_TO_REF(-1);
    }
#endif

    if (kvbag_p->mode == O71_BAG_ARRAY)
        return kvbag_p->kv_a[loc_p->array.index].value_r;
    return loc_p->rbtree.node_a[loc_p->rbtree.last_x]->kv.value_r;
}

/* kvbag_set_loc_value ******************************************************/
static void kvbag_set_loc_value
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p,
    o71_ref_t value_r
)
{
    if (kvbag_p->mode == O71_BAG_ARRAY)
        kvbag_p->kv_a[loc_p->array.index].value_r = value_r;
    else loc_p->rbtree.node_a[loc_p->rbtree.last_x]->kv.value_r = value_r;
}

/* kvbag_insert *************************************************************/
static o71_status_t kvbag_insert
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r,
    o71_kvbag_loc_t * loc_p
)
{
    while (kvbag_p->mode == O71_BAG_ARRAY)
    {
        int i;
        o71_status_t os;
        if (kvbag_p->n == kvbag_p->m)
        {
            size_t m, nm;
            /* array full; reallocate or switch to tree */
            A((kvbag_p->m & (kvbag_p->m - 1)) == 0);
            A((kvbag_p->l & (kvbag_p->l - 1)) == 0);
            if (kvbag_p->m == kvbag_p->l)
            {
                o71_kv_t * kv_a = kvbag_p->kv_a;

                M("switch bag from array to tree");
                kvbag_p->tree_p = NULL;
                os = kvbag_rbtree_multi_add(world_p, kvbag_p, kv_a, kvbag_p->n);
                if (os)
                {
                    M("rbtree multi add failed: %s", N(os));
                    if (kvbag_p->tree_p)
                    {
                        o71_status_t osf;
                        osf = kvbag_rbtree_free(world_p, kvbag_p->tree_p);
                        if (osf) return osf;
                    }

                    kvbag_p->kv_a = kv_a;
                    return os;
                }
                kvbag_p->mode = O71_BAG_RBTREE;
                // fall into the rbtree branch
                os = kvbag_rbtree_search(world_p, kvbag_p, key_r,
                                         str_intern_cmp, NULL, loc_p);
                A(os == O71_MISSING);
                break;
            }
            m = kvbag_p->m;
            nm = m ? m << 1 : (kvbag_p->l < 2 ? kvbag_p->l : 2);
            os = redim(world_p->allocator_p, (void * *) &kvbag_p->kv_a, &m, nm,
                       sizeof(o71_kv_t));
            if (os)
            {
                M("failed to extend bag array: %s", N(os));
                return os;
            }
            kvbag_p->m = (uint8_t) nm;
        }
        A(kvbag_p->n < kvbag_p->m);
        os = o71_ref(world_p, key_r);
        AOS(os);
        for (i = (int) kvbag_p->n - 1; i >= (int) loc_p->array.index; --i)
            kvbag_p->kv_a[i + 1] = kvbag_p->kv_a[i];
        kvbag_p->kv_a[loc_p->array.index].key_r = key_r;
        kvbag_p->kv_a[loc_p->array.index].value_r = value_r;
        kvbag_p->n += 1;
        return O71_OK;
    }
    A(kvbag_p->mode == O71_BAG_RBTREE);

    return kvbag_rbtree_insert(world_p, kvbag_p, key_r, value_r, loc_p);
}

/* kvbag_delete *************************************************************/
static o71_status_t kvbag_delete
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p
)
{
    return (kvbag_p->mode == O71_BAG_ARRAY)
        ? kvbag_array_delete(world_p, kvbag_p, loc_p)
        : kvbag_rbtree_delete(world_p, kvbag_p, loc_p);
}

/* kvbag_put ****************************************************************/
static o71_status_t kvbag_put
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r,
    o71_cmp_f cmp,
    void * ctx
)
{
    o71_status_t os;
    o71_kvbag_loc_t loc;

    os = kvbag_search(world_p, kvbag_p, key_r, cmp, ctx, &loc);
    if (os == O71_OK)
    {
        o71_ref_t old_value_r;
        // key already in the bag
        old_value_r = kvbag_get_loc_value(world_p, kvbag_p, &loc);
        os = o71_deref(world_p, old_value_r);
        AOS(os);
        kvbag_set_loc_value(world_p, kvbag_p, &loc, value_r);
        return O71_OK;
    }
    if (os != O71_MISSING) return os;
    os = kvbag_insert(world_p, kvbag_p, key_r, value_r, &loc);
    return os;
}

/* kvbag_rbtree_node_alloc **************************************************/
static o71_status_t kvbag_rbtree_node_alloc
(
    o71_world_t * world_p,
    o71_kvnode_t * * kvnode_pp
)
{
    size_t n = 0;
    return redim(world_p->allocator_p, (void * *) kvnode_pp, &n, 1,
                 sizeof(o71_kvnode_t));
}

/* kvbag_rbtree_node_free ***************************************************/
static o71_status_t kvbag_rbtree_node_free
(
    o71_world_t * world_p,
    o71_kvnode_t * kvnode_p
)
{
    size_t n = 1;
    return redim(world_p->allocator_p, (void * *) &kvnode_p, &n, 0,
                 sizeof(o71_kvnode_t));
}

/* kvbag_rbtree_free ********************************************************/
static o71_status_t kvbag_rbtree_free
(
    o71_world_t * world_p,
    o71_kvnode_t * kvnode_p
)
{
    o71_status_t os;
    A(kvnode_p);
    if (kvnode_p->clr[0])
    {
        os = kvbag_rbtree_free(world_p, GET_CHILD(kvnode_p, 0));
        if (os) return os;
    }
    if (kvnode_p->clr[1])
    {
        os = kvbag_rbtree_free(world_p, GET_CHILD(kvnode_p, 1));
        if (os) return os;
    }
    return kvbag_rbtree_node_free(world_p, kvnode_p);
}

#if O71_DEBUG
/* kvbag_rbtree_dump ********************************************************/
static void kvbag_rbtree_dump
(
    o71_world_t * world_p,
    o71_kvnode_t * kvnode_p,
    unsigned int depth
)
{
    if (!kvnode_p) return;
    kvbag_rbtree_dump(world_p, GET_CHILD(kvnode_p, O71_LESS), depth + 1);
    printf("%.*sk=", depth * 2,
           "                                                                ");
    obj_dump(world_p, kvnode_p->kv.key_r);
    printf(" -> v=");
    obj_dump(world_p, kvnode_p->kv.value_r);
    printf("\n");
    // printf("%.*sk=%lX=:v=%lX= (%s)\n",
    //        depth * 2,
    //        kvnode_p->kv.key_r, kvnode_p->kv.value_r,
    //        IS_RED(kvnode_p) ? "red" : "black");
    kvbag_rbtree_dump(world_p, GET_CHILD(kvnode_p, O71_MORE), depth + 1);
}
#endif

/* kvbag_rbtree_add *********************************************************/
static o71_status_t kvbag_rbtree_add
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r
)
{
    o71_kvbag_loc_t loc;
    o71_status_t os;
    M2("kvbag_p=%p, key=obref_%lX, value=obref_%lX", kvbag_p, key_r, value_r);
    os = kvbag_rbtree_search(world_p, kvbag_p, key_r,
                             str_intern_cmp, NULL, &loc);
    if (os == O71_OK)
    {
        o71_kvnode_t * n = loc.rbtree.node_a[loc.rbtree.last_x];
        M2("discarding old_value=obref_%lX for key=obref_%lX", value_r, key_r);
        os = o71_deref(world_p, n->kv.value_r);
        AOS(os);
        n->kv.value_r = value_r;
        M2("updated value to obref_%lX for key obref_%lX", value_r, key_r);
        return O71_OK;
    }
    if (os != O71_MISSING)
    {
        M("error searching for key obref_%lX: %s", key_r, N(os));
        return os;
    }

    return kvbag_rbtree_insert(world_p, kvbag_p, key_r, value_r, &loc);
}


/* kvbag_rbtree_multi_add ***************************************************/
static o71_status_t kvbag_rbtree_multi_add
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kv_t * kv_a,
    size_t kv_n
)
{
    o71_status_t os;
    size_t x;
    if (!kv_n) return O71_OK;
    x = kv_n / 2;
    os = kvbag_rbtree_add(world_p, kvbag_p, kv_a[x].key_r, kv_a[x].value_r);
    if (os) return os;
    os = kvbag_rbtree_multi_add(world_p, kvbag_p, kv_a, x);
    if (os) return os;
    ++x;
    os = kvbag_rbtree_multi_add(world_p, kvbag_p, kv_a + x, kv_n - x);
    return os;
}

/* kvbag_rbtree_insert ******************************************************/
static o71_status_t kvbag_rbtree_insert
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_ref_t value_r,
    o71_kvbag_loc_t * loc_p
)
{
    unsigned int i, gs, ps, ts;
    o71_kvnode_t * parent;
    o71_kvnode_t * grandpa;
    o71_kvnode_t * uncle;
    o71_kvnode_t * tmp;
    o71_kvnode_t * node;
    o71_status_t os;

    M2("rbtree insert (k=obref_%lX, v=obref_%lX)", key_r, value_r);
    os = kvbag_rbtree_node_alloc(world_p, &node);
    if (os)
    {
        M("failed to allocate rbtree node: %s", N(os));
        return os;
    }
    os = o71_ref(world_p, key_r);
    AOS(os);
    node->kv.key_r = key_r;
    node->kv.value_r = value_r;

    i = loc_p->rbtree.last_x;
    node->clr[0] = 1;
    node->clr[1] = 0;
    ts = loc_p->rbtree.side_a[i];
    M2("ts=%u", ts);
    A(ts < 2);
    SET_CHILD(loc_p->rbtree.node_a[i], ts, node);

    while (i)
    {
        parent = loc_p->rbtree.node_a[i];
        if (!IS_RED(parent)) return O71_OK;
        // parent is red (thus it is not the root)
        grandpa = loc_p->rbtree.node_a[--i];
        uncle = GET_CHILD(grandpa, OTHER_SIDE(loc_p->rbtree.side_a[i]));
        if (uncle && IS_RED(uncle))
        {
            SET_RED(grandpa, 1);
            SET_RED(parent, 0);
            SET_RED(uncle, 0);
            node = grandpa;
            --i;
            continue;
        }
        // parent is red, uncle is black or NULL
        if ((gs = loc_p->rbtree.side_a[i])
            != (ps = loc_p->rbtree.side_a[i + 1]))
        {
            // node and parent are on different sides
            SET_CHILD(grandpa, gs, node);
            tmp = GET_CHILD(node, gs);
            SET_CHILD(parent, ps, tmp);
            SET_CHILD(node, gs, parent);

            tmp = node;
            node = parent;
            parent = tmp;
        }
        else ps = OTHER_SIDE(gs);
        // node and parent are on same side: gs; ps is set to the 'other' side
        tmp = GET_CHILD(parent, ps);
        SET_CHILD(parent, ps, grandpa);
        SET_RED(parent, 0);
        SET_CHILD(grandpa, gs, tmp);
        SET_RED(grandpa, 1);
        tmp = loc_p->rbtree.node_a[i - 1];
        ts = loc_p->rbtree.side_a[i - 1];
        SET_CHILD(tmp, ts, parent);
        return O71_OK;
    }
    // if we processed 'til the top of the path then root is changed
    SET_RED(node, 0);
    return O71_OK;
}

#if O71_DEBUG
/* kvbag_array_dump *********************************************************/
static void kvbag_array_dump
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p
)
{
    unsigned int i;
    printf("[array]");
    for (i = 0; i < kvbag_p->n; ++i)
    {
        printf(" k=%lX:v=%lX",
               kvbag_p->kv_a[i].key_r, kvbag_p->kv_a[i].value_r);
    }

    printf("\n");
}
#endif

/* kvbag_array_search *******************************************************/
static o71_status_t kvbag_array_search
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_cmp_f cmp,
    void * ctx,
    o71_kvbag_loc_t * loc_p
)
{
    int a, b;
    o71_status_t os;
    a = 0;
    b = kvbag_p->n - 1;
    while (a <= b)
    {
        int c = (a + b) >> 1;
        unsigned int r = cmp(world_p, key_r, kvbag_p->kv_a[c].key_r, ctx);
        // M("intern_cmp(key=%lX, bag_key=%lX) -> %s",
        //   key_r, kvbag_p->kv_a[c].key_r,
        //   r == O71_EQUAL ? "equal" : (r ? "more" : "less"));
        switch (r)
        {
        case O71_LESS: b = c - 1; break;
        case O71_MORE: a = c + 1; break;
        case O71_EQUAL:
            loc_p->array.index = c;
#if O71_CHECKED
            loc_p->status = O71_OK;
#endif
            // M("match: key_r=%lX -> index=%u", key_r, a);
            return O71_OK;
        default:
            M("compare error: %s", N(r));
            loc_p->cmp_error = r;
            return O71_CMP_ERROR;
        }
    }
    // a contains the position where to insert the key
    loc_p->array.index = a;
#if O71_CHECKED
            loc_p->status = O71_MISSING;
#endif
    M2("miss: key_r=obref_%lX -> pos=%u", key_r, a);
    return O71_MISSING;
}

/* kvbag_array_delete *******************************************************/
static o71_status_t kvbag_array_delete
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p
)
{
    unsigned int i;
    A(kvbag_p->n > 0);
    kvbag_p->n--;
    A(loc_p->array.index <= kvbag_p->n);
    for (i = loc_p->array.index; i < kvbag_p->n; ++i)
        kvbag_p->kv_a[i] = kvbag_p->kv_a[i + 1];
    return O71_OK;
}


/* kvbag_rbtree_search ******************************************************/
static o71_status_t kvbag_rbtree_search
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_ref_t key_r,
    o71_cmp_f cmp,
    void * ctx,
    o71_kvbag_loc_t * loc_p
)
{
    o71_kvnode_t * kvnode_p;
    unsigned int i = 0;
    loc_p->rbtree.node_a[0] = (o71_kvnode_t *) &kvbag_p->tree_p;
    loc_p->rbtree.side_a[0] = 0;
    //loc_p->rbtree.last_x = 0;
    kvnode_p = kvbag_p->tree_p;
    while (kvnode_p)
    {
        unsigned int cr;
        loc_p->rbtree.node_a[++i] = kvnode_p;
        cr = cmp(world_p, key_r, kvnode_p->kv.key_r, ctx);
        switch (cr)
        {
        case O71_LESS:
        case O71_MORE:
            loc_p->rbtree.side_a[i] = cr;
            kvnode_p = GET_CHILD(kvnode_p, cr);
            break;
        case O71_EQUAL:
            loc_p->rbtree.side_a[i] = cr;
            loc_p->rbtree.last_x = i;
#if O71_CHECKED
            loc_p->status = O71_OK;
#endif
            return O71_OK;
        default:
            loc_p->rbtree.last_x = i;
#if O71_CHECKED
            loc_p->status = O71_CMP_ERROR;
#endif
            return O71_CMP_ERROR;
        }
    }
    loc_p->rbtree.last_x = i;
#if O71_CHECKED
    loc_p->status = O71_MISSING;
#endif
    return O71_MISSING;
}

/* kvbag_rbtree_np **********************************************************/
static o71_kvnode_t * kvbag_rbtree_np
(
    o71_kvbag_loc_t * loc_p,
    unsigned int side
)
{
  o71_kvnode_t * n;
  o71_kvnode_t * m;
  unsigned int d;

  d = loc_p->rbtree.last_x;
  n = loc_p->rbtree.node_a[d];
  m = GET_CHILD(n, side);
  //if (!m) return NULL;
  if (m)
  {
      loc_p->rbtree.side_a[d] = side;
      side = OTHER_SIDE(side);
      for (; m; m = GET_CHILD(m, side))
      {
        ++d;
        loc_p->rbtree.node_a[d] = m;
        loc_p->rbtree.side_a[d] = side;
      }
  }
  else
  {
      do { --d; } while (d && loc_p->rbtree.side_a[d] == side);
      if (!d) return NULL;
  }


  loc_p->rbtree.side_a[d] = O71_EQUAL;
  loc_p->rbtree.last_x = d;
  return loc_p->rbtree.node_a[d];
}

/* kvbag_rbtree_delete ******************************************************/
static o71_status_t kvbag_rbtree_delete
(
    o71_world_t * world_p,
    o71_kvbag_t * kvbag_p,
    o71_kvbag_loc_t * loc_p
)
{
    o71_kvnode_t * o;
    o71_kvnode_t * d;
    o71_kvnode_t * p;
    o71_kvnode_t * c;
    o71_kvnode_t * s;
    o71_kvnode_t * sl;
    o71_kvnode_t * sr;
    o71_kvnode_t * t;
    uintptr_t tmp[2];
    unsigned int cs, ds, od, dd, ns, pd, ps, ss, os;

    do
    {
        od = loc_p->rbtree.last_x;
        o = loc_p->rbtree.node_a[od]; // the node we want to delete
        if (GET_CHILD(o, O71_LESS) && GET_CHILD(o, O71_MORE))
        {
            ds = loc_p->rbtree.side_a[od - 1];
            d = kvbag_rbtree_np(loc_p, (uint8_t) ds);
            // now must delete d which has at most 1 non-null child
            dd = loc_p->rbtree.last_x;
            tmp[0] = o->clr[0]; tmp[1] = o->clr[1];
            o->clr[0] = d->clr[0]; o->clr[1] = d->clr[1];
            d->clr[0] = tmp[0]; d->clr[1] = tmp[1];
            loc_p->rbtree.node_a[od] = d;
            t = loc_p->rbtree.node_a[od - 1];
            os = loc_p->rbtree.side_a[od - 1];
            SET_CHILD(t, os, d);
        }
        else { dd = od; }

        d = o;

        cs = GET_CHILD(d, 0) ? O71_LESS : O71_MORE;
        c = GET_CHILD(d, cs); // c is the only child of d or NULL

        ds = loc_p->rbtree.side_a[dd - 1];
        p = loc_p->rbtree.node_a[dd - 1];
        if (IS_RED(d))
        {
            // d has no children since it has at most 1 non-null child and
            // both paths must have same ammount of black node_a
            SET_CHILD(p, ds, NULL);
            break;
        }
        // d is black; it has either no children, or a single red child with no
        // children of its own
        if (c) // same as: (c && c->red)
        {
            SET_CHILD(p, ds, c);
            SET_RED(c, 0);
            break;
        }

        // d is black and has no children (if c is black and its sibling is NULL then
        // it must be NULL itself)

        SET_CHILD(p, ds, NULL);

        for (pd = dd - 1; pd; p = loc_p->rbtree.node_a[--pd])
        {
            // n is always black (in first iteration it is NULL)
            ns = loc_p->rbtree.side_a[pd];
            ss = OTHER_SIDE(ns);
            s = GET_CHILD(p, ss); // this is a non-NULL node
            if (IS_RED(s)) // implies p is black
            {
                /* del case 2 */
                t = loc_p->rbtree.node_a[pd - 1];
                ps = loc_p->rbtree.side_a[pd - 1];
                SET_CHILD(t, ps, s);
                sl = GET_CHILD(s, ns);
                SET_CHILD(p, ss, sl);
                SET_RED(p, 1);
                SET_CHILD(s, ns, p);
                SET_RED(s, 0);
                loc_p->rbtree.node_a[pd] = s;
                ++pd;
                s = sl; /* new s is black */
            }
            else
            {
                // s is black
                if (!IS_RED(p) && !IS_RED(GET_CHILD(s, 0)) && !IS_RED(GET_CHILD(s, 1)))
                {
                    /* del case 3 */
                    SET_RED(s, 1);
                    continue;
                }
            }
            // s must be black
            sl = GET_CHILD(s, ns);
            sr = GET_CHILD(s, ss);
            if (IS_RED(p) && !IS_RED(sl) && !IS_RED(sr))
            {
                /* del case 4 */
                SET_RED(p, 0);
                SET_RED(s, 1);
                break;
            }
            if (IS_RED(sl) && !IS_RED(sr))
            {
                SET_CHILD(p, ss, sl);
                SET_CHILD(s, ns, GET_CHILD(sl, ss));
                SET_RED(s, 1);
                SET_CHILD(sl, ss, s);
                SET_RED(sl, 0);
                s = sl; // again new s is black
                sl = GET_CHILD(s, ns);
                sr = GET_CHILD(s, ss);
            }
            if (IS_RED(sr))
            {
                t = loc_p->rbtree.node_a[pd - 1];
                ps = loc_p->rbtree.side_a[pd - 1];
                SET_CHILD(t, ps, s);
                SET_CHILD(p, ss, sl);
                SET_RED(s, IS_RED(p));
                SET_RED(p, 0);
                SET_RED(sr, 0);
                SET_CHILD(s, ns, p);
            }
            break;
        }
    }
    while (0);
    kvbag_rbtree_node_free(world_p, o);
    return O71_OK;
}

#if O71_DEBUG
static void obj_dump
(
    o71_world_t * world_p,
    o71_ref_t obj_r
)
{
    o71_obj_index_t obj_x;
    unsigned int om;
    if (O71_IS_REF_TO_SINT(obj_r))
    {
        printf("%ld", (long) O71_REF_TO_SINT(obj_r));
        return;
    }
    obj_x = O71_REF_TO_MOX(obj_r);
    if (obj_x >= world_p->obj_n || IS_FREE_OBJECT_SLOT(world_p->obj_pa[obj_x]))
    {
        printf("BAD_REF_%lX", (long) obj_r);
        return;
    }
    om = o71_model(world_p, obj_r);
    if ((om & O71M_STRING))
    {
        o71_string_t * str_p = world_p->obj_pa[obj_x];
        printf("%sstr_%lX(\"%.*s\")",
               str_p->mode == O71_SM_MODIFIABLE ? "" :
               (str_p->mode == O71_SM_READ_ONLY ? "ro" : "i"),
               (long) obj_r, (int) str_p->n, str_p->a);
        return;
    }
    printf("obj_%lX", obj_r);
}

#endif

/* class_super_extend *******************************************************/
static o71_status_t class_super_extend
(
    o71_world_t * world_p,
    o71_class_t * class_p,
    o71_ref_t * super_ra,
    size_t super_n
)
{
    o71_status_t os;
    size_t i, j;
    ptrdiff_t x;

#if O71_DEBUG
    {
        ptrdiff_t i;
        printf("class_super_extend: class_p=%p, existing super_ra=[", class_p);
        if (class_p->super_n)
        {
            for (i = 0; i < (ptrdiff_t) class_p->super_n - 1; ++i)
                printf("o%lX, ", class_p->super_ra[i]);
            printf("o%lX", class_p->super_ra[i]);
        }
        printf("], new_super_ra=[");
        if (super_n)
        {
            for (i = 0; i < (ptrdiff_t) super_n - 1; ++i)
                printf("o%lX, ", super_ra[i]);
            printf("o%lX", super_ra[i]);
        }

        printf("] => ");
    }
#endif
    /* get rid of superclasses already in the list */
    for (i = 0; i < super_n; )
    {
        x = ref_search(class_p->super_ra, class_p->super_n, super_ra[i]);
        if (x >= 0)
        {
            super_ra[i] = super_ra[--super_n];
            continue;
        }
        ++i;
    }

    /* sort the ones to introduce */
    ref_qsort(super_ra, super_n);

    for (i = 1; i < super_n && super_ra[i - 1] < super_ra[i]; ++i);
    if (i < super_n)
    {
        for (j = i - 1; i < super_n; ++i)
            if (super_ra[j] < super_ra[i]) super_ra[++j] = super_ra[i];
        super_n = j;
#if _DEBUG
        {
            ptrdiff_t i;
            printf("new_super_ra after sort & uniq =[");
            if (super_n)
            {
                for (i = 0; i < (ptrdiff_t) super_n - 1; ++i)
                    printf("o%lX, ", super_ra[i]);
                printf("o%lX", super_ra[i]);
            }

            printf("] => ");
        }
#endif
    }

    os = merge_sorted_refs(world_p, &class_p->super_ra, &class_p->super_n,
                           super_ra, super_n);
#if O71_DEBUG
    {
        ptrdiff_t i;
        printf("status: %s, output super_ra=[", N(os));
        if (class_p->super_n)
        {
            for (i = 0; i < (ptrdiff_t) class_p->super_n - 1; ++i)
                printf("o%lX, ", class_p->super_ra[i]);
            printf("o%lX", class_p->super_ra[i]);
        }
        printf("].\n");
    }
#endif
    return os;
}

/* merge_sorted_refs ********************************************************/
static o71_status_t merge_sorted_refs
(
    o71_world_t * world_p,
    o71_ref_t * * dest_rap,
    size_t * dest_rnp,
    o71_ref_t * src_ra,
    size_t src_n
)
{
    o71_status_t os;
    ptrdiff_t i, j, k;
    o71_ref_t * dest_ra;

    A(dest_rnp + src_n >= dest_rnp);
    i = (ptrdiff_t) *dest_rnp - 1;
    k = (ptrdiff_t) (*dest_rnp + src_n);
    os = redim(world_p->allocator_p, (void * *) dest_rap, dest_rnp, (size_t) k,
               sizeof(o71_ref_t));
    if (os) return os;
    dest_ra = *dest_rap;
    j = (ptrdiff_t) src_n - 1;
    --k;
    while (i >= 0 && i < k)
    {
        dest_ra[k--] = (dest_ra[i] > src_ra[j]) ? dest_ra[i--] : src_ra[j--];
    }
    while (i < k) dest_ra[k--] = src_ra[j--];
    return O71_OK;
}


/* ref_search ***************************************************************/
static ptrdiff_t ref_search
(
    o71_ref_t * ra,
    size_t rn,
    o71_ref_t kr
)
{
    ptrdiff_t a, b, c;
    for (a = 0, b = (ptrdiff_t) rn - 1; a <= b; )
    {
        c = (a + b) >> 1;
        if (kr == ra[c]) return c;
        if (kr < ra[c]) b = c - 1;
        else a = c + 1;
    }
    return ~a;
}

/* ref_qsort ****************************************************************/
static void ref_qsort
(
    o71_ref_t * ref_a,
    size_t ref_n
)
{
    size_t i, j;
    int ii, jj;
    o71_ref_t r;

    for (; ref_n > 1;)
    {
        i = 0;
        j = ref_n - 1;
        ii = 1; jj = 0;
        while (i < j)
        {
            if (ref_a[i] > ref_a[j])
            {
                r = ref_a[i];
                ref_a[i] = ref_a[j];
                ref_a[j] = r;
                ii ^= 1;
                jj ^= -1;
            }
            i += ii;
            j += jj;
        }
        /* launch the short side recursively so that depth is logarithmic */
        if (i < ref_n - i - 1)
        {
            ref_qsort(ref_a, i);
            ref_a += i + 1;
            ref_n -= i + 1;
        }
        else
        {
            ref_qsort(ref_a + i + 1, ref_n - (i + 1));
            ref_n = i;
        }
    }
}

/* refkv_qsort **************************************************************/
static void refkv_qsort
(
    o71_kv_t * kv_a,
    size_t n
)
{
    size_t i, j;
    int ii, jj;
    o71_kv_t r;

    for (; n > 1;)
    {
        i = 0;
        j = n - 1;
        ii = 1; jj = 0;
        while (i < j)
        {
            if (kv_a[i].key_r > kv_a[j].key_r)
            {
                r = kv_a[i];
                kv_a[i] = kv_a[j];
                kv_a[j] = r;
                ii ^= 1;
                jj ^= -1;
            }
            i += ii;
            j += jj;
        }
        /* launch the short side recursively so that depth is logarithmic */
        if (i < n - i - 1)
        {
            refkv_qsort(kv_a, i);
            kv_a += i + 1;
            n -= i + 1;
        }
        else
        {
            refkv_qsort(kv_a + i + 1, n - (i + 1));
            n = i;
        }
    }
}

/* get_reg_obj_field ********************************************************/
static o71_status_t get_reg_obj_field
(
    o71_flow_t * flow_p,
    o71_ref_t obj_r,
    o71_ref_t field_r,
    o71_ref_t * value_rp
)
{
    o71_status_t os;
    A(o71_model(flow_p->world_p, obj_r) != O71M_INVALID);
    A(o71_model(flow_p->world_p, field_r) != O71M_INVALID);
    os = o71_istr_check(flow_p->world_p, field_r);
    if (os)
    {
        M("bad field name obref_%lX: %s", (long) field_r, N(os));
        return os;
    }
    return o71_reg_obj_get_field(flow_p->world_p, obj_r, field_r, value_rp);
}


/* set_reg_obj_field ********************************************************/
static o71_status_t set_reg_obj_field
(
    o71_flow_t * flow_p,
    o71_ref_t obj_r,
    o71_ref_t field_r,
    o71_ref_t value_r
)
{
    o71_status_t os;
    A(o71_model(flow_p->world_p, obj_r) != O71M_INVALID);
    A(o71_model(flow_p->world_p, field_r) != O71M_INVALID);
    A(o71_model(flow_p->world_p, value_r) != O71M_INVALID);
    os = o71_istr_check(flow_p->world_p, field_r);
    if (os)
    {
        M("bad field name obref_%lX: %s", (long) field_r, N(os));
        return os;
    }
    return o71_reg_obj_set_field(flow_p->world_p, obj_r, field_r, value_r);
}

/* free_token ***************************************************************/
static o71_status_t free_token
(
    o71_code_t * code_p,
    o71_token_t * token_p,
    unsigned int min_tt
)
{
    o71_status_t os;
    unsigned int i;
    o71_str_token_t * str_token_p;
    if (token_p->type < min_tt) return O71_OK;
    M("free_token %p(type=%s)", token_p, ttname_a[token_p->type]);
    switch (token_p->type)
    {
    case O71_TT_STRING:
        str_token_p = (o71_str_token_t *) token_p;
        FREE_ARRAY(code_p->allocator_p, str_token_p->a, str_token_p->n);
        return O71_OK;
    case O71_TT_END:
    case O71_TT_TILDE:
    case O71_TT_EXCLAMATION:
    case O71_TT_QUESTION:
    case O71_TT_PERCENT:
    case O71_TT_CARET:
    case O71_TT_AMPERSAND:
    case O71_TT_PIPE:
    case O71_TT_STAR:
    case O71_TT_SLASH:
    case O71_TT_PAREN_OPEN:
    case O71_TT_PAREN_CLOSE:
    case O71_TT_SQUARE_BRACKET_OPEN:
    case O71_TT_SQUARE_BRACKET_CLOSE:
    case O71_TT_CURLY_BRACKET_OPEN:
    case O71_TT_CURLY_BRACKET_CLOSE:
    case O71_TT_EQUAL:
    case O71_TT_PLUS:
    case O71_TT_MINUS:
    case O71_TT_LESS:
    case O71_TT_GREATER:
    case O71_TT_DOT:
    case O71_TT_COMMA:
    case O71_TT_SEMICOLON:
    case O71_TT_COLON:
    case O71_TT_PLUS_PLUS:
    case O71_TT_MINUS_MINUS:
    case O71_TT_STAR_STAR:
    case O71_TT_LESS_LESS:
    case O71_TT_GREATER_GREATER:
    case O71_TT_AMPERSAND_AMPERSAND:
    case O71_TT_PIPE_PIPE:
    case O71_TT_EQUAL_EQUAL:
    case O71_TT_EXCLAMATION_EQUAL:
    case O71_TT_PLUS_EQUAL:
    case O71_TT_MINUS_EQUAL:
    case O71_TT_STAR_EQUAL:
    case O71_TT_SLASH_EQUAL:
    case O71_TT_PERCENT_EQUAL:
    case O71_TT_LESS_EQUAL:
    case O71_TT_GREATER_EQUAL:
    case O71_TT_LESS_LESS_EQUAL:
    case O71_TT_GREATER_GREATER_EQUAL:
    case O71_TT_AMPERSAND_EQUAL:
    case O71_TT_PIPE_EQUAL:
    case O71_TT_CARET_EQUAL:
    case O71_TT_STAR_STAR_EQUAL:
    case O71_TT_AMPERSAND_AMPERSAND_EQUAL:
    case O71_TT_PIPE_PIPE_EQUAL:
    case O71_TT_RETURN:
    case O71_TT_BREAK:
    case O71_TT_GOTO:
    case O71_TT_IF:
    case O71_TT_ELSE:
    case O71_TT_WHILE:
    case O71_TT_DO:
    case O71_TT_FOR:
    case O71_TT_SWITCH:
    case O71_TT_CASE:
    case O71_TT_INTEGER:
    case O71_TT_IDENTIFIER:
    default:
        break;
    }
    return O71_OK;
}

/* rule_nop *****************************************************************/
static o71_status_t rule_nop (o71_code_t * code_p)
{
    (void) code_p;
    return 0;
}

/* tokenize_source **********************************************************/
static o71_status_t tokenize_source
(
    o71_code_t * code_p
)
{
    size_t ofs;
    uint32_t row, col, ch, nch;
    int chlen, ce, digit;
    o71_status_t os;
    unsigned int base;
    size_t str_n, i;
    o71_allocator_t * allocator_p;
    uint8_t const * src_a;
    size_t src_n, src_ofs;
    o71_token_t * token_p;
    unsigned int type;

    allocator_p = code_p->allocator_p;
    src_a = code_p->src_a;
    src_n = code_p->src_n;
#define CE(_e) { ce = _e; break; }
    ce = O71_COMPILE_OK;
    for (ofs = 0, row = 1, col = 1; ofs < src_n; ofs += chlen)
    {
        if (src_a[ofs] < 0x20)
        {
            if (src_a[ofs] == '\r')
            {
                row += 1;
                col = 1;
                if (ofs + 1 < src_n && src_a[ofs + 1] == '\n')
                {
                    chlen += 2;
                    continue;
                }
                chlen = 1;
                continue;
            }
            if (src_a[ofs] == '\n')
            {
                row += 1;
                col = 1;
                chlen = 1;
                continue;
            }
            CE(O71_CE_PARSE_BAD_CONTROL_CHAR);
        }
        if (src_a[ofs] < 0x80)
        {
            chlen = 1;
            continue;
        }
        if (src_a[ofs] < 0xC0) CE(O71_CE_PARSE_BAD_UTF8_START_BYTE);
        if (src_a[ofs] < 0xE0)
        {
            if (ofs + 2 > src_n)
                CE(O71_CE_PARSE_TRUNCATED_UTF8_CHAR);
            if ((src_a[ofs + 1] & 0xC0) != 0x80)
                CE(O71_CE_PARSE_BAD_UTF8_CONTINUATION);
            if (src_a[ofs] < 0xC2)
                CE(O71_CE_PARSE_OVERLY_LONG_ENCODED_UTF8_CHAR);
            chlen = 2;
            col += 1;
        }
        if (src_a[ofs] < 0xF0)
        {
            if (ofs + 3 > src_n)
                CE(O71_CE_PARSE_TRUNCATED_UTF8_CHAR);
            if (((src_a[ofs + 1] | src_a[ofs + 2]) & 0xC0) != 0x80)
                CE(O71_CE_PARSE_BAD_UTF8_CONTINUATION);
            if (src_a[ofs] == 0xE0 && src_a[ofs + 1] < 0xA0)
                CE(O71_CE_PARSE_OVERLY_LONG_ENCODED_UTF8_CHAR);
            if (src_a[ofs] == 0xED && src_a[ofs + 1] >= 0xA0)
                CE(O71_CE_PARSE_SURROGATE_CODEPOINT);
            chlen = 3;
            col += 1;
        }
        if (src_a[ofs] < 0xF8)
        {
            if (ofs + 4 > src_n)
                CE(O71_CE_PARSE_TRUNCATED_UTF8_CHAR);
            if (((src_a[ofs + 1] | src_a[ofs + 2] | src_a[ofs + 3]) & 0xC0)
                != 0x80) CE(O71_CE_PARSE_BAD_UTF8_CONTINUATION);
            if (src_a[ofs] == 0xF0 && src_a[ofs + 1] < 0x90)
                CE(O71_CE_PARSE_OVERLY_LONG_ENCODED_UTF8_CHAR);
        }
        CE(O71_CE_PARSE_BAD_UTF8_START_BYTE);
    }

    if (!ce) for (ofs = 0, row = 1, col = 1; ofs < src_n; )
    {
        ch = src_a[ofs];
        if (ch <= 0x20)
        {
            if (ch == ' ') { col += 1; ofs += 1; continue; }
            A(ch == '\n' || ch == '\r');
            if (ch == '\r')
            {
                if (ofs + 1 < src_n && src_a[ofs + 1] == '\n') ++ofs;
            }
            ++ofs;
            ++row;
            col = 1;
            continue;
        }
        /* non-whitespace character */
        if (ch == '#')
        {
            /* skip until EOL */
            for (; ofs < src_n && src_a[ofs] >= 0x20; ++ofs);
            /* don't bother to update the column here as we either reached
             * end of line or end of file */
            continue;
        }

        src_ofs = ofs;
        if (IS_ID_START_CHAR(ch))
        {
            o71_id_token_t * id_token_p;
            for (++ofs; ofs < src_n && IS_ID_BODY_CHAR(src_a[ofs]); ++ofs);
            ALLOC(os, code_p->allocator_p, id_token_p);
            if (os) return os;
            token_p = &id_token_p->base;
            token_p->type = O71_TT_IDENTIFIER;
            id_token_p->a = src_a + src_ofs;
            id_token_p->n = ofs - src_ofs;
        }
        else if (IS_DIGIT(ch))
        {
            o71_int_token_t * int_token_p;
            uint64_t num;
            base = 10;
            if (ch == '0' && ofs + 1 < src_n)
            {
                ch = src_a[++ofs];
                switch (ch)
                {
                case 'b': ++ofs; base = 2; break;
                case 'o': ++ofs; base = 8; break;
                case 'd': ++ofs; base = 10; break;
                case 'x': ++ofs; base = 16; break;
                }
            }
            num = 0;
            for (; ofs < src_n &&
                 (src_a[ofs] == '_' || IS_DIGIT(src_a[ofs]) ||
                  IS_ALPHA(src_a[ofs]));
                 ++ofs)
            {
                if (src_a[ofs] == '_') continue;
                digit = ALPHANUM_TO_DIGIT(src_a[ofs]);
                if (digit >= (int) base) CE(O71_CE_PARSE_BAD_NUMBER);
                num = num * base + digit;
            }
            if (ce) break;
            ALLOC(os, code_p->allocator_p, int_token_p);
            if (os) return os;
            token_p = &int_token_p->base;
            token_p->type = O71_TT_INTEGER;
            int_token_p->val = num;
        }
        else if (ch == '"')
        {
            o71_str_token_t * str_token_p;
            //M("ofs=%zX", ofs);
            ++ofs;
            for (str_n = 0;
                 ofs < src_n && src_a[ofs] != '"' && src_a[ofs] != '\n'; )
            {
                //M("ofs=%zX", ofs);
                if (src_a[ofs++] != '\\') ++str_n;
                else
                {
                    if (ofs == src_n) CE(O71_CE_PARSE_UNFINISHED_STRING);
                    switch (ch = src_a[ofs++])
                    {
                    case '0':
                    case 'n':
                    case 'r':
                    case 'v':
                    case 'e':
                    case 't':
                    case 'a':
                    case 'b':
                    case '\\':
                    case '\'':
                    case '"':
                        ++str_n;
                        continue;
                    case 'x': // \xAB
                        if (ofs + 2 > src_n ||
                            !IS_HEX_DIGIT(src_a[ofs]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 1]))
                            break;
                        ofs += 2;
                        ++str_n;
                        continue;
                    case 'c': // \c12
                        if (ofs + 2 > src_n ||
                            !IS_HEX_DIGIT(src_a[ofs]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 1]))
                            break;
                        ch = (ALPHANUM_TO_DIGIT(src_a[ofs]) << 4)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 1]));
                        ofs += 2;
                        str_n += utf8_codepoint_length(ch);
                        continue;
                    case 'u': // \u1234
                        if (ofs + 4 > src_n ||
                            !IS_HEX_DIGIT(src_a[ofs]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 1]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 2]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 3]))
                            break;
                        ch = (ALPHANUM_TO_DIGIT(src_a[ofs]) << 12)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 1]) << 8)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 2]) << 4)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 3]));
                        ofs += 4;
                        str_n += utf8_codepoint_length(ch);
                        continue;
                    case 'U': // \U123456
                        if (ofs + 6 > src_n ||
                            !IS_HEX_DIGIT(src_a[ofs]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 1]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 2]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 3]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 4]) ||
                            !IS_HEX_DIGIT(src_a[ofs + 5]))
                            break;
                        ch = (ALPHANUM_TO_DIGIT(src_a[ofs]) << 20)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 1]) << 16)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 2]) << 12)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 3]) << 8)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 4]) << 4)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 5]));
                        if (ch >= 0x110000) break;
                        str_n += utf8_codepoint_length(ch);
                        ofs += 6;
                        continue;
                    }
                    CE(O71_CE_PARSE_BAD_STRING_ESCAPE);
                }
            }
            //M("str_n=%zu", str_n);
            if (ce) break;
            if (ofs == src_n || src_a[ofs] == '\n')
                CE(O71_CE_PARSE_UNFINISHED_STRING);
            A(src_a[ofs] == '"');
            ALLOC(os, code_p->allocator_p, str_token_p);
            if (os) return os;
            token_p = &str_token_p->base;
            token_p->type = O71_TT_STRING;
            str_token_p->n = 0;
            os = redim(allocator_p, (void * *) &str_token_p->a, 
                       &str_token_p->n, str_n + 1, 1);
            if (os)
            {
                return os;
            }
            for (i = 0, ofs = src_ofs + 1; src_a[ofs] != '"'; )
            {
                //M("ofs=%zX ch: %c", ofs, src_a[ofs]);
                A(i < str_n);
                if (src_a[ofs] != '\\') str_token_p->a[i++] = src_a[ofs++];
                else
                {
                    ++ofs;
                    switch (ch = src_a[ofs++])
                    {
                    case '0': str_token_p->a[i++] = 0; break;
                    case 'n': str_token_p->a[i++] = '\n'; break;
                    case 'r': str_token_p->a[i++] = '\r'; break;
                    case 'v': str_token_p->a[i++] = '\v'; break;
                    case 'e': str_token_p->a[i++] = '\x1B'; break;
                    case 't': str_token_p->a[i++] = '\t'; break;
                    case 'a': str_token_p->a[i++] = '\a'; break;
                    case 'b': str_token_p->a[i++] = '\b'; break;
                    case '\\': str_token_p->a[i++] = '\\'; break;
                    case '\'': str_token_p->a[i++] = '\''; break;
                    case '"': str_token_p->a[i++] = '\"'; break;
                    case 'x': // \xAB
                        str_token_p->a[i++] 
                            = (ALPHANUM_TO_DIGIT(src_a[ofs]) << 4)
                            | ALPHANUM_TO_DIGIT(src_a[ofs + 1]);
                        ofs += 2;
                        break;
                    case 'c': // \cAB
                        ch = (ALPHANUM_TO_DIGIT(src_a[ofs]) << 4)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 1]));
                        ofs += 2;
                        i += utf8_codepoint_encode(str_token_p->a + i, ch);
                        break;
                    case 'u': // \u1234
                        ch = (ALPHANUM_TO_DIGIT(src_a[ofs]) << 12)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 1]) << 8)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 2]) << 4)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 3]));
                        ofs += 4;
                        i += utf8_codepoint_encode(str_token_p->a + i, ch);
                        break;
                    case 'U': // \U123456
                        ch = (ALPHANUM_TO_DIGIT(src_a[ofs]) << 20)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 1]) << 16)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 2]) << 12)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 3]) << 8)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 4]) << 4)
                            | (ALPHANUM_TO_DIGIT(src_a[ofs + 5]));
                        ofs += 6;
                        i += utf8_codepoint_encode(str_token_p->a + i, ch);
                        break;
                    }
                }
            }
            str_token_p->a[str_n] = 0;
            M("i=%zu, str_n=%zu", i, str_n);
            A(i == str_n);
            A(src_a[ofs] == '"');
            ++ofs;
            //A(ofs == src_ofs + src_len);
        }
        else
        {
            ++ofs;
            nch = ofs < src_n ? src_a[ofs] : 0;
            switch (ch)
            {
            case '~': type = O71_TT_TILDE; break;
            case '?': type = O71_TT_QUESTION; break;
            case '(': type = O71_TT_PAREN_OPEN; break;
            case ')': type = O71_TT_PAREN_CLOSE; break;
            case '[': type = O71_TT_SQUARE_BRACKET_OPEN; break;
            case ']': type = O71_TT_SQUARE_BRACKET_CLOSE; break;
            case '{': type = O71_TT_CURLY_BRACKET_OPEN; break;
            case '}': type = O71_TT_CURLY_BRACKET_CLOSE; break;
            case '.': type = O71_TT_DOT; break;
            case ',': type = O71_TT_COMMA; break;
            case ';': type = O71_TT_SEMICOLON; break;
            case ':': type = O71_TT_COLON; break;
            case '!':
                if (nch == '=') { ++ofs; type = O71_TT_EXCLAMATION_EQUAL; }
                else type = O71_TT_EXCLAMATION;
                break;
            case '%':
                if (nch == '=') { ++ofs; type = O71_TT_PERCENT_EQUAL; }
                else type = O71_TT_PERCENT;
                break;
            case '^':
                if (nch == '=') { ++ofs; type = O71_TT_CARET_EQUAL; }
                else type = O71_TT_CARET;
                break;
            case '&':
                switch (nch)
                {
                case '&':
                    if (++ofs < src_n && src_a[ofs] == '=')
                    {
                        ++ofs;
                        type = O71_TT_AMPERSAND_AMPERSAND_EQUAL;
                        break;
                    }
                    else type = O71_TT_AMPERSAND_AMPERSAND;
                    break;
                case '=': ++ofs; type = O71_TT_AMPERSAND_EQUAL; break;
                }
                break;
            case '|':
                switch (nch)
                {
                case '&':
                    if (++ofs < src_n && src_a[ofs] == '=')
                    {
                        ++ofs;
                        type = O71_TT_PIPE_PIPE_EQUAL;
                        break;
                    }
                    else type = O71_TT_PIPE_PIPE;
                    break;
                case '=': ++ofs; type = O71_TT_PIPE_EQUAL; break;
                }
                break;
            case '+':
                switch (nch)
                {
                case '+': ++ofs; type = O71_TT_PLUS_PLUS; break;
                case '=': ++ofs; type = O71_TT_PLUS_EQUAL; break;
                default: type = O71_TT_PLUS;
                }
                break;
            case '-':
                switch (nch)
                {
                case '-': ++ofs; type = O71_TT_MINUS_MINUS; break;
                case '=': ++ofs; type = O71_TT_MINUS_EQUAL; break;
                default: type = O71_TT_MINUS;
                }
                break;
            case '*':
                switch (nch)
                {
                case '*':
                    if (++ofs < src_n && src_a[ofs] == '=')
                    {
                        ++ofs;
                        type = O71_TT_STAR_STAR_EQUAL;
                    }
                    else type = O71_TT_STAR_STAR;
                    break;
                case '=': ++ofs; type = O71_TT_STAR_EQUAL; break;
                default: type = O71_TT_STAR;
                }
                break;
            case '/':
                if (nch == '=') { ++ofs; type = O71_TT_SLASH_EQUAL; }
                else type = O71_TT_SLASH;
                break;
            case '=':
                if (nch == '=') { ++ofs; type = O71_TT_EQUAL_EQUAL; }
                else type = O71_TT_EQUAL;
                break;
            case '<':
                switch (nch)
                {
                case '<':
                    if (++ofs < src_n && src_a[ofs] == '=')
                    {
                        ++ofs;
                        type = O71_TT_LESS_LESS_EQUAL;
                    }
                    else type = O71_TT_LESS_LESS;
                    break;
                case '=':
                    ++ofs;
                    type = O71_TT_LESS_EQUAL;
                    break;
                default:
                    type = O71_TT_LESS;
                }
                break;
            case '>':
                switch (nch)
                {
                case '>':
                    if (++ofs < src_n && src_a[ofs] == '=')
                    {
                        ++ofs;
                        type = O71_TT_GREATER_GREATER_EQUAL;
                    }
                    else type = O71_TT_GREATER_GREATER;
                    break;
                case '=':
                    ++ofs;
                    type = O71_TT_GREATER_EQUAL;
                    break;
                default:
                    type = O71_TT_GREATER;
                }
                break;
            case '"':
                break;
            default:
                CE(O71_CE_PARSE_BAD_CHAR);
            }
            ALLOC(os, code_p->allocator_p, token_p);
            if (os) return os;
            token_p->type = type;
        }

        if (ce) break;
        token_p->src_ofs = src_ofs;
        token_p->src_row = row;
        token_p->src_col = col;
        col += (token_p->src_len = ofs - src_ofs);
        token_p->next = NULL;
        *code_p->token_tail = token_p;
        code_p->token_tail = &token_p->next;
        M("token type=%u: %s %.*s", token_p->type, ttname_a[token_p->type], 
          (int) token_p->src_len, src_a + token_p->src_ofs);
    }

#undef CE
    if (ce)
    {
        code_p->ce_code = ce;
        code_p->ce_row = row;
        code_p->ce_col = col;
        code_p->ce_ofs = ofs;
        return O71_COMPILE_ERROR;
    }

    ALLOC(os, code_p->allocator_p, token_p);
    if (os) return os;
    token_p->src_ofs = src_n;
    token_p->src_len = 0;
    token_p->src_row = row;
    token_p->src_col = col;
    token_p->type = O71_TT_END;
    token_p->next = NULL;
    *code_p->token_tail = token_p;
    code_p->token_tail = &token_p->next;
    M("tokenization done!");
    return O71_OK;
}

#if _DEBUG
static void dump_token_list
(
    o71_token_t * token_p,
    size_t n
)
{
    for (; token_p && n; token_p = token_p->next, --n)
        printf("%s%*s", ttname_a[token_p->type], (int) !!n, "");
}


#endif

/* O71_MAIN *****************************************************************/
#if O71_MAIN
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define ERR_NONE 0
#define ERR_RUN 1
#define ERR_RES 2
#define ERR_INVOKE 3
#define ERR_BUG 4

/* mem_realloc **************************************************************/
static o71_status_t mem_realloc
(
    void * * data_pp,
    size_t old_size,
    size_t new_size,
    void * realloc_context
)
{
    void * new_data;
    if (old_size == 0)
    {
        if (new_size == 0) { *data_pp = NULL; return O71_OK; }
        *data_pp = malloc(new_size);
        return *data_pp ? O71_OK : O71_NO_MEM;
    }
    if (new_size == 0)
    {
        free(*data_pp);
        return O71_OK;
    }
    new_data = realloc(*data_pp, new_size);
    if (!new_data) return O71_NO_MEM;
    *data_pp = new_data;
    return O71_OK;
}

/* help *********************************************************************/
static void help ()
{
    printf("ostropel scripting tool - ver 0.00 - by Costin Ionescu\n"
           "usage: o71 [OPTIONS] SCRIPT ARGS\n"
           "options:\n"
           "  -h --help     help\n"
           "  -t --test     run self tests\n");
}

#define TE(...) { \
    fprintf(stderr, "test error (line %u): ", __LINE__); \
    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); \
    rc = ERR_RUN; \
    break; }

#define TS(_expr) \
    if (!(os = (_expr))) ; \
    else TE("expr (%s) failed with status %s", #_expr, N(os))

#define ORC(_w, _r) (((o71_mem_obj_t *) o71_obj_ptr(_w, _r))->ref_n)

/* reg_obj_field_test *******************************************************/
static int reg_obj_field_test (o71_world_t * world_p)
{
    o71_ref_t aa_isr, bb_isr, cc_isr;
    o71_ref_t sf_r, obj_r;
    o71_ref_t ra[4];
    o71_script_function_t * sf_p;
    o71_ref_t class_r;
    uint32_t * arg_vxa;
    o71_status_t os;
    int rc = 0;
    do
    {
        TS(o71_ics(world_p, &aa_isr, "aa"));
        TS(o71_ics(world_p, &bb_isr, "bb"));
        TS(o71_ics(world_p, &cc_isr, "cc"));
        ra[0] = cc_isr;
        ra[1] = aa_isr;
        TS(o71_reg_class_create(world_p, ra, 2, &class_r));
        TS(o71_sfunc_create(world_p, &sf_r, 1));
        sf_p = o71_obj_ptr(world_p, sf_r);
        TS(o71_sfunc_append_init(world_p, sf_p, 2, aa_isr));
        TS(o71_sfunc_append_init(world_p, sf_p, 1, O71_SINT_TO_REF(1)));
        TS(o71_sfunc_append_set_field(world_p, sf_p, 1, 0, 2));
        TS(o71_sfunc_append_init(world_p, sf_p, 2, bb_isr));
        TS(o71_sfunc_append_init(world_p, sf_p, 1, O71_SINT_TO_REF(2)));
        TS(o71_sfunc_append_set_field(world_p, sf_p, 1, 0, 2));
        TS(o71_sfunc_append_init(world_p, sf_p, 2, cc_isr));
        TS(o71_sfunc_append_init(world_p, sf_p, 1, O71_SINT_TO_REF(3)));
        TS(o71_sfunc_append_set_field(world_p, sf_p, 1, 0, 2));
        TS(o71_sfunc_append_init(world_p, sf_p, 2, aa_isr));
        TS(o71_sfunc_append_get_field(world_p, sf_p, 3, 0, 2));
        TS(o71_sfunc_append_init(world_p, sf_p, 2, bb_isr));
        TS(o71_sfunc_append_get_field(world_p, sf_p, 4, 0, 2));
        TS(o71_sfunc_append_init(world_p, sf_p, 2, cc_isr));
        TS(o71_sfunc_append_get_field(world_p, sf_p, 5, 0, 2));
        TS(o71_sfunc_append_init(world_p, sf_p, 6, O71R_INT_ADD_FUNC));
        TS(o71_sfunc_append_call(world_p, sf_p, 1, 6, 2, &arg_vxa));
        arg_vxa[0] = 3;
        arg_vxa[1] = 4;
        TS(o71_sfunc_append_call(world_p, sf_p, 1, 6, 2, &arg_vxa));
        arg_vxa[0] = 1;
        arg_vxa[1] = 5;
        TS(o71_sfunc_append_ret(world_p, sf_p, 1));

        TS(o71_reg_obj_create(world_p, class_r, &ra[0]));
        M("dyn obj to test: obref_%lX", (long) ra[0]);
        os = o71_prep_call(&world_p->root_flow, sf_r, ra, 1);
        if (os != O71_PENDING) TE("prep_call failed: %s", N(os));
        TS(o71_run(&world_p->root_flow, 0, O71_STEPS_MAX));
        if (world_p->root_flow.value_r != O71_SINT_TO_REF(1 + 2 + 3))
            TE("got obref_%lX, expecting obref_%lX",
               (long) world_p->root_flow.value_r,
               (long) O71_SINT_TO_REF(1 + 2 + 3));
    }
    while (0);
    printf("reg_obj_field_test: %u\n", rc);
    return rc;
}

/* test *********************************************************************/
static int test ()
{
    o71_allocator_t allocator;
    o71_world_t world;
    o71_status_t os;
    int rc = 0, i;
    o71_ref_t r, add3_r, g1_r, g2_r, g3_r, gi_r, add_istr_r, dyn_r;
    o71_ref_t ra[10];
    o71_script_function_t * add3_p;
    uint32_t * arg_vxa;
    o71_exception_t * exc_p;
    o71_exc_handler_t * eha;
    uint32_t iac_ix, ecx;
    char sb[10];

    o71_allocator_init(&allocator, mem_realloc, NULL, SIZE_MAX);
    os = o71_world_init(&world, &allocator);
    if (os)
    {
        fprintf(stderr, "error: failed initializing scripting instance: %s\n",
                o71_status_name(os));
        return ERR_RUN;
    }

    do
    {
        {
            o71_class_t c;
            size_t n, i;
            size_t N = 50;
            o71_ref_t * ra;
            o71_ref_t l;
            c.super_ra = NULL;
            c.super_n = 0;
            os = redim(world.allocator_p, (void * *) &c.super_ra, &c.super_n, N,
                       sizeof(o71_ref_t));
            if (os) TE("error (line %u): status: %s", __LINE__, N(os));
            for (i = 0; i < c.super_n; ++i)
                c.super_ra[i] = 2 * i + 1;

            ra = NULL;
            n = 0;
            os = redim(world.allocator_p, (void * *) &ra, &n, N,
                       sizeof(o71_ref_t));
            if (os) TE("error (line %u): status: %s", __LINE__, N(os));
            for (i = 0, l = 1; i < n; ++i)
                ra[i] = l = (31409573 * l + 1) % (2 * N);

            os = class_super_extend(&world, &c, ra, n);
            if (os) TE("error (line %u): status: %s", __LINE__, N(os));

            os = redim(world.allocator_p, (void * *) &ra, &n, 0,
                       sizeof(o71_ref_t));
            if (os) TE("error (line %u): status: %s", __LINE__, N(os));

            for (i = 0; i < N; ++i)
            {
                l = 2 * i + 1;
                if (ref_search(c.super_ra, c.super_n, l) < 0)
                    TE("error (line %u): did not find %lX", __LINE__, l);
            }

            for (i = 0, l = 1; i < n; ++i)
            {
                l = (31409573 * l + 1) % (2 * N);
                if (ref_search(c.super_ra, c.super_n, l) < 0)
                    TE("error (line %u): did not find %lX", __LINE__, l);
            }

            os = redim(world.allocator_p, (void * *) &c.super_ra, &c.super_n, 0,
                       sizeof(o71_ref_t));
            if (os) TE("error (line %u): status: %s", __LINE__, N(os));
        }

        os = o71_cstring(&world, &r, "first string");
        if (os) TE("error: failed to create first string object: %s",
                   o71_status_name(os));

        if (!(o71_model(&world, r) & O71M_STRING))
            TE("error: string object created has bad model");

        os = o71_ref(&world, r);
        if (os) TE("referencing first string failed: %s", o71_status_name(os));

        os = o71_deref(&world, r);
        if (os) TE("first dereferencing of first string failed: %s",
                   o71_status_name(os));
        os = o71_deref(&world, r);
        if (os) TE("second dereferencing of first string failed: %s",
                   o71_status_name(os));
        os = o71_deref(&world, O71R_NULL);
        if (os) TE("null deref #1 fail: %s", o71_status_name(os));
        os = o71_deref(&world, O71R_NULL);
        if (os) TE("null deref #2 fail: %s", o71_status_name(os));
        os = o71_deref(&world, O71R_NULL);
        if (os) TE("null deref #3 fail: %s", o71_status_name(os));
        os = o71_ref(&world, O71R_NULL);
        if (os) TE("null ref #1 fail: %s", o71_status_name(os));
        os = o71_deref(&world, O71R_NULL);
        if (os) TE("null deref #4 fail: %s", o71_status_name(os));

        os = o71_rocs(&world, &g1_r, "gargara");
        if (os) TE("failed to create r/o string 'gargara': %s",
                   o71_status_name(os));
        os = o71_str_intern(&world, g1_r, &gi_r);
        if (os) TE("failed to get intern string for 'gargara': %s",
                   o71_status_name(os));
        if (g1_r != gi_r)
            TE("received different string for intern form for 'gargara': %s",
               o71_status_name(os));
        if (ORC(&world, gi_r) != 2)
            TE("bad ref count for newly intern'ed 'gargara'");
        os = o71_ics(&world, &g2_r, "zbenghi");
        if (os) TE("failed to create intern string 'zbenghi': %s",
                   o71_status_name(os));

        os = o71_ics(&world, &g3_r, "atroce");
        if (os) TE("failed to create intern string 'atroce': %s",
                   o71_status_name(os));

        for (i = 0; i < 26 * 26; ++i)
        {
            sb[0] = 'a' + i % 26;
            sb[1] = 'a' + i / 26;
            sb[2] = 0;
            os = o71_cstring(&world, &g1_r, sb);
            if (os) TE("failed to create string '%s': %s",
                       sb, o71_status_name(os));
            //M("ics('%s') -> o%lX", sb, g1_r);
            os = o71_str_freeze(&world, g1_r);
            if (os) TE("failed to freeze string '%s': %s",
                       sb, o71_status_name(os));
            os = o71_str_intern(&world, g1_r, &gi_r);
            if (os) TE("failed to create intern string '%s': %s",
                       sb, o71_status_name(os));
            if (g1_r != gi_r)
                TE("unexpected intern string s%lX from s%lX='%s'",
                   gi_r, g1_r, sb);
            os = o71_deref(&world, g1_r);
            if (os) TE("deref error for s%lX='%s'", g1_r, sb);
        }
        if (i < 26 * 26) break;

        os = o71_reg_obj_create(&world, O71R_REG_OBJ_CLASS, &dyn_r);
        if (os) TE("reg_obj_create failed: %s", o71_status_name(os));

        ra[0] = O71_SINT_TO_REF(1);
        ra[1] = O71_SINT_TO_REF(2);
        os = o71_prep_call(&world.root_flow, O71R_INT_ADD_FUNC, ra, 2);
        if (os) TE("int_add(1+2) failed: %s", o71_status_name(os));
        if (world.root_flow.value_r != O71_SINT_TO_REF(3))
            TE("int_add(1+2) returned ref %lX",
               (long) world.root_flow.value_r);
        os = o71_prep_call(&world.root_flow, O71R_INT_ADD_FUNC, ra, 1);
        if (os != O71_EXC) TE("no exc thrown, boo! (%s)", o71_status_name(os));
        if (!(o71_model(&world, world.root_flow.exc_r) & O71M_EXCEPTION))
            TE("non-exc thrown, boo!");
        exc_p = o71_obj_ptr(&world, world.root_flow.exc_r);
        if (exc_p->hdr.class_r != O71R_ARITY_EXC_CLASS)
            TE("non-arity exc thrown, boo!");
        o71_deref(&world, world.root_flow.exc_r);
        world.root_flow.exc_r = O71R_NULL;

        os = o71_sfunc_create(&world, &add3_r, 3);
        if (os) TE("add3 function create failed: %s", o71_status_name(os));

        if (!(o71_model(&world, add3_r) & O71M_SCRIPT_FUNCTION))
            TE("add3 create bad ref model: %X", o71_model(&world, add3_r));
        add3_p = o71_obj_ptr(&world, add3_r);

        os = o71_sfunc_append_init(&world, add3_p, 3, O71R_INT_ADD_FUNC);
        if (os) TE("add3: init v3, int_add_func: %s", o71_status_name(os));

        iac_ix = add3_p->insn_n;
        os = o71_sfunc_append_call(&world, add3_p, 4, 3, 2, &arg_vxa);
        if (os) TE("add3: call dest=v4, func=v3, arg1, arg2: %s",
                   o71_status_name(os));
        arg_vxa[0] = 0;
        arg_vxa[1] = 1;

        os = o71_ics(&world, &add_istr_r, "add");
        if (os) TE("add3: failed to get istr('add'): %s",
                   o71_status_name(os));

        os = o71_sfunc_append_init(&world, add3_p, 5, add_istr_r);
        if (os) TE("add3: init v3, istr_%lX('add'): %s",
                   add_istr_r, o71_status_name(os));

        os = o71_sfunc_append_get_method(&world, add3_p, 6, 4, 5);
        if (os) TE("add3: get_method dest=v5, obj=v4, name=v5: %s",
                   o71_status_name(os));

        os = o71_sfunc_append_call(&world, add3_p, 4, 6, 2, &arg_vxa);
        if (os) TE("add3: call dest=v4, func=v6, v4, arg3: %s",
                   o71_status_name(os));
        arg_vxa[0] = 4;
        arg_vxa[1] = 2;

        os = o71_sfunc_append_ret(&world, add3_p, 4);
        if (os) TE("add3: ret v4: %s", o71_status_name(os));

        os = o71_sfunc_append_init(&world, add3_p, 0, O71R_NULL);
        if (os) TE("add3: init v0, NULL: %s", o71_status_name(os));
        os = o71_sfunc_append_ret(&world, add3_p, 0);
        if (os) TE("add3: ret v0: %s", o71_status_name(os));

        os = o71_alloc_exc_chain(&world, add3_p, &ecx, &eha, 1);
        if (os) TE("add3: alloc exc chain failed: %s", o71_status_name(os));

        eha[0].exc_type_r = O71R_EXCEPTION_CLASS;
        eha[0].insn_x = add3_p->insn_n - 2;

        os = o71_set_exc_chain(&world, add3_p, iac_ix, add3_p->insn_n - 2, ecx);
        if (os) TE("add3: set exc chain failed: %s", o71_status_name(os));

        ra[0] = O71_SINT_TO_REF(2);
        ra[1] = O71_SINT_TO_REF(3);
        ra[2] = O71_SINT_TO_REF(4);
        os = o71_prep_call(&world.root_flow, add3_r, ra, 3);
        if (os != O71_PENDING)
            TE("calling add3(2, 3, 4) failed: %s", o71_status_name(os));
        os = o71_run(&world.root_flow, 0, O71_STEPS_MAX);
        if (os != O71_OK)
            TE("running add3(2, 3, 4) failed: %s", o71_status_name(os));

        if (world.root_flow.value_r != O71_SINT_TO_REF(2 + 3 + 4))
            TE("add3(2, 3, 4) returned wrong value ref %lX",
               (long) world.root_flow.value_r);

        ra[0] = O71_SINT_TO_REF(2);
        ra[1] = O71_SINT_TO_REF(3);
        ra[2] = O71R_NULL;
        os = o71_prep_call(&world.root_flow, add3_r, ra, 3);
        if (os != O71_PENDING)
            TE("calling add3(2, 3, NULL) failed: %s", o71_status_name(os));
        os = o71_run(&world.root_flow, 0, O71_STEPS_MAX);
        if (os != O71_OK)
            TE("running add3(2, 3, 4) failed: %s", o71_status_name(os));
        if (world.root_flow.value_r != O71R_NULL)
            TE("add3(2, 3, 4) returned wrong value ref %lX",
               (long) world.root_flow.value_r);

        if ((rc = reg_obj_field_test(&world))) break;
    }
    while (0);

    os = o71_world_finish(&world);
    if (os)
    {
        fprintf(stderr, "error: failed finishing scripting instance: %s\n",
                o71_status_name(os));
        return ERR_RUN;
    }

    printf("self test %s!\n", rc ? "FAILED" : "passed");
    return rc;
}

/* compile_error_msg ********************************************************/
static int compile_error_msg (o71_code_t const * code_p, char * buf, size_t len)
{
    switch (code_p->ce_code)
    {
    case O71_CE_PARSE_BAD_CONTROL_CHAR:
        snprintf(buf, len, "%s:%u:%u: error: bad control char 0x%02X",
                 code_p->src_name, code_p->ce_row, code_p->ce_col,
                 code_p->src_a[code_p->ce_ofs]);
        break;
    case O71_CE_PARSE_BAD_UTF8_START_BYTE:
        snprintf(buf, len, "%s:%u:%u: error: bad UTF8 start byte 0x%02X",
                 code_p->src_name, code_p->ce_row, code_p->ce_col,
                 code_p->src_a[code_p->ce_ofs]);
        break;
    case O71_CE_PARSE_OVERLY_LONG_ENCODED_UTF8_CHAR:
        snprintf(buf, len, "%s:%u:%u: error: overly long encoded UTF8 char",
                 code_p->src_name, code_p->ce_row, code_p->ce_col);
        break;
    case O71_CE_PARSE_TRUNCATED_UTF8_CHAR:
        snprintf(buf, len, "%s:%u:%u: error: truncated UTF8 char",
                 code_p->src_name, code_p->ce_row, code_p->ce_col);
        break;
    case O71_CE_PARSE_BAD_UTF8_CONTINUATION:
        snprintf(buf, len, "%s:%u:%u: error: bad UTF8 continuation byte",
                 code_p->src_name, code_p->ce_row, code_p->ce_col);
        break;
    case O71_CE_PARSE_SURROGATE_CODEPOINT:
        snprintf(buf, len, "%s:%u:%u: error: surrogate Unicode codepoint",
                 code_p->src_name, code_p->ce_row, code_p->ce_col);
        break;
    default:
        snprintf(buf, len, "compile error code: %u", code_p->ce_code);
    }
    return 0;
}

/* run_script ***************************************************************/
static int run_script (int ac, char const * const * av)
{
    FILE * f = NULL;
    char const * src_name;
    uint8_t * src_a = NULL;
    long src_size;
    o71_status_t os;
    int rv = ERR_RUN;
    o71_code_t code;
    o71_allocator_t allocator;
    char msg[0x800];

    o71_allocator_init(&allocator, mem_realloc, NULL, SIZE_MAX);
#define E(...) { fprintf(stderr, "error: " __VA_ARGS__); fprintf(stderr, "\n");  break; }
    do
    {
        src_name = av[0];
        f = fopen(src_name, "rb");
        if (!f) E("could not open script file '%s'", src_name);
        if (fseek(f, 0, SEEK_END)
            || (src_size = ftell(f)) < 0
            || fseek(f, 0, SEEK_SET))
            E("seek failed in script file '%s'", src_name);
        src_a = malloc(src_size);
        if (!src_a)
            E("failed to allocate %lu bytes for reading script file '%s'",
              src_size, src_name);
        if (fread(src_a, 1, src_size, f) != (size_t) src_size)
            E("failed to read %lu bytes from script file '%s'",
              src_size, src_name);
        fclose(f);
        f = NULL;
        os = o71_compile(&code, &allocator, src_name, src_a, src_size);
        if (os)
        {
            if (os == O71_COMPILE_ERROR)
            {
                compile_error_msg(&code, msg, sizeof(msg));
                msg[sizeof(msg) - 1] = 0;
                fputs(msg, stderr);
                break;
            }
            else E("failed compiling script file '%s' (%s)",
                   src_name, o71_status_name(os));
        }
        fprintf(stderr, "error: running scripts is not implemented!\n");
        rv = ERR_BUG;
    }
    while (0);
#undef E
    if (f) fclose(f);
    if (code.src_a) o71_code_free(&code);
    if (src_a) free(src_a);
    return rv;
}

#define RUN_SCRIPT 0
#define RUN_HELP 1
#define RUN_TEST 2

/* main *********************************************************************/
int main (int argc, char const * const * argv)
{
    int i, j, n = 0;
    char opt_parse = 1, run = RUN_SCRIPT;
    char const * * a;

    a = malloc(sizeof(char const *) * argc);
    if (!a)
    {
        fprintf(stderr,
                "error: could not allocate memory to parse command line!\n");
        return ERR_RES;
    }

    for (i = 1; i < argc; ++i)
    {
        if (opt_parse && argv[i][0] == '-')
        {
            if (argv[i][1] == '-')
            {
                if (argv[i][2] == 0)
                {
                    opt_parse = 0;
                    continue;
                }
                if (!strcmp(argv[i] + 2, "test"))
                {
                    run = RUN_TEST;
                    continue;
                }
                if (!strcmp(argv[i] + 2, "help"))
                {
                    run = RUN_HELP;
                    return 0;
                }
                fprintf(stderr, "error: unrecognized command line option '%s'"
                        " (arg %u); run again with '-h' for help\n",
                        argv[i], i);
                return ERR_INVOKE;
            }
            for (j = 1; argv[i][j]; ++j)
            {
                switch (argv[i][j])
                {
                case 't':
                    run = RUN_TEST;
                    break;
                case 'h':
                    run = RUN_HELP;
                    break;
                default:
                    fprintf(stderr, "error: unrecognized command line option "
                            "'-%c' (arg %u, pos %u); run again with '-h' "
                            "for help\n\n", argv[i][j], i, j);
                    return ERR_INVOKE;
                }
            }
        }
        a[n++] = argv[i];
        opt_parse = 0;
    }

    switch (run)
    {
    case RUN_SCRIPT:
        if (n) return run_script(n, a);
    case RUN_HELP:
        help();
        return 0;
    case RUN_TEST:
        return test();
    default:
        fprintf(stderr, "bug: unhandled run mode %u\n", run);
        return ERR_BUG;
    }
}

#endif
