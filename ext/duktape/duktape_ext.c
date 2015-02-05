#include "ruby.h"
#include "ruby/encoding.h"
#include "duktape.h"

static VALUE mDuktape;
static VALUE cContext;
static VALUE cComplexObject;
static VALUE oComplexObject;

static VALUE eUnimplementedError;
static VALUE eUnsupportedError;
static VALUE eInternalError;
static VALUE eAllocError;
static VALUE eAssertionError;
static VALUE eAPIError;
static VALUE eUncaughtError;

static VALUE eError;
static VALUE eEvalError;
static VALUE eRangeError;
static VALUE eReferenceError;
static VALUE eSyntaxError;
static VALUE eTypeError;
static VALUE eURIError;
static rb_encoding *utf16enc;

static ID id_complex_object;
static ID id_ruby_bridge;

static void error_handler(duk_context *, int, const char *);
static int ctx_push_hash_element(VALUE key, VALUE val, VALUE extra);
static duk_ret_t rduk_finalize(duk_context *ctx);

static unsigned long
utf8_to_uv(const char *p, long *lenp);

#define clean_raise(ctx, ...) (duk_set_top(ctx, 0), rb_raise(__VA_ARGS__))
#define clean_raise_exc(ctx, ...) (duk_set_top(ctx, 0), rb_exc_raise(__VA_ARGS__))

struct state {
  duk_context *ctx;
  VALUE complex_object;
  int was_complex;
  int has_ruby_bridge;
  struct obj_ref *objs;
};

struct obj_ref {
  VALUE obj;
  struct state *state;
  struct obj_ref *prev;
  struct obj_ref *next;
};

static void ctx_dealloc(void *ptr)
{
  struct state *state = (struct state *)ptr;
  duk_destroy_heap(state->ctx);

  struct obj_ref *ref = state->objs;
  while (ref) {
    struct obj_ref *next_ref = ref->next;
    free(ref);
    ref = next_ref;
  }

  free(state);
}

static void ctx_mark(struct state *state)
{
  rb_gc_mark(state->complex_object);
  struct obj_ref *ref = state->objs;
  while (ref) {
    rb_gc_mark(ref->obj);
    ref = ref->next;
  }
}

static VALUE ctx_alloc(VALUE klass)
{
  duk_context *ctx = duk_create_heap(NULL, NULL, NULL, NULL, error_handler);

  // Undefine require property
  duk_push_global_object(ctx);
  duk_push_string(ctx, "require");
  duk_del_prop(ctx, -2);
  duk_set_top(ctx, 0);

  struct state *state = malloc(sizeof(struct state));
  state->ctx = ctx;
  state->complex_object = oComplexObject;
  state->has_ruby_bridge = 0;
  state->objs = NULL;
  return Data_Wrap_Struct(klass, ctx_mark, ctx_dealloc, state);
}

static VALUE error_code_class(int code) {
  switch (code) {
    case DUK_ERR_UNIMPLEMENTED_ERROR:
      return eUnimplementedError;
    case DUK_ERR_UNSUPPORTED_ERROR:
      return eUnsupportedError;
    case DUK_ERR_INTERNAL_ERROR:
      return eInternalError;
    case DUK_ERR_ALLOC_ERROR:
      return eAllocError;
    case DUK_ERR_ASSERTION_ERROR:
      return eAssertionError;
    case DUK_ERR_API_ERROR:
      return eAPIError;
    case DUK_ERR_UNCAUGHT_ERROR:
      return eUncaughtError;

    case DUK_ERR_ERROR:
      return eError;
    case DUK_ERR_EVAL_ERROR:
      return eEvalError;
    case DUK_ERR_RANGE_ERROR:
      return eRangeError;
    case DUK_ERR_REFERENCE_ERROR:
      return eReferenceError;
    case DUK_ERR_SYNTAX_ERROR:
      return eSyntaxError;
    case DUK_ERR_TYPE_ERROR:
      return eTypeError;
    case DUK_ERR_URI_ERROR:
      return eURIError;

    default:
      return eInternalError;
  }
}

static VALUE error_name_class(const char* name)
{
  if (strcmp(name, "EvalError") == 0) {
    return eEvalError;
  } else if (strcmp(name, "RangeError") == 0) {
    return eRangeError;
  } else if (strcmp(name, "ReferenceError") == 0) {
    return eReferenceError;
  } else if (strcmp(name, "SyntaxError") == 0) {
    return eSyntaxError;
  } else if (strcmp(name, "TypeError") == 0) {
    return eTypeError;
  } else if (strcmp(name, "URIError") == 0) {
    return eURIError;
  } else {
    return eError;
  }
}

static VALUE encode_cesu8(struct state *state, VALUE str)
{
  duk_context *ctx = state->ctx;
  VALUE res = rb_str_new(0, 0);

  VALUE utf16 = rb_str_conv_enc(str, rb_enc_get(str), utf16enc);
  if (utf16 == str && rb_enc_get(str) != utf16enc) {
    clean_raise(ctx, rb_eEncodingError, "cannot convert Ruby string to UTF-16");
  }

  long len = RSTRING_LEN(utf16) / 2;
  unsigned short *bytes = (unsigned short *)RSTRING_PTR(utf16);

  char buf[8];

  for (int i = 0; i < len; i++) {
    int length = rb_uv_to_utf8(buf, bytes[i]);
    rb_str_buf_cat(res, (char*)buf, length);
  }

  return res;
}

static VALUE decode_cesu8(struct state *state, VALUE str)
{
  duk_context *ctx = state->ctx;
  VALUE res = rb_str_new(0, 0);

  const char *ptr = RSTRING_PTR(str);
  const char *end = RSTRING_END(str);
  long len;

  while (ptr < end) {
    len = (end - ptr);
    unsigned short code = utf8_to_uv(ptr, &len);
    rb_str_buf_cat(res, (char*)&code, 2);
    ptr += len;
  }

  rb_enc_associate(res, utf16enc);
  VALUE utf8res = rb_str_conv_enc(res, utf16enc, rb_utf8_encoding());
  if (utf8res == res) {
    clean_raise(ctx, rb_eEncodingError, "cannot convert JavaScript string to UTF-16");
  }

  return utf8res;
}

static VALUE ctx_stack_to_value(struct state *state, int index)
{
  duk_context *ctx = state->ctx;
  size_t len;
  const char *buf;
  int type;

  state->was_complex = 0;

  type = duk_get_type(ctx, index);
  switch (type) {
    case DUK_TYPE_NULL:
    case DUK_TYPE_UNDEFINED:
      return Qnil;

    case DUK_TYPE_NUMBER:
      return rb_float_new(duk_get_number(ctx, index));

    case DUK_TYPE_BOOLEAN:
      return duk_get_boolean(ctx, index) ? Qtrue : Qfalse;

    case DUK_TYPE_STRING:
      buf = duk_get_lstring(ctx, index, &len);
      VALUE str = rb_str_new(buf, len);
      return decode_cesu8(state, str);

    case DUK_TYPE_OBJECT:
      if (state->has_ruby_bridge) {
        duk_get_prop_string(ctx, index, "\xFFruby");
        if (duk_is_pointer(ctx, -1)) {
          struct obj_ref *ref = (struct obj_ref*)duk_get_pointer(ctx, -1);
          duk_pop(ctx);
          return ref->obj;
        }

        duk_pop(ctx);
      }

      if (duk_is_function(ctx, index)) {
        state->was_complex = 1;
        return state->complex_object;
      } else if (duk_is_array(ctx, index)) {
        VALUE ary = rb_ary_new();
        duk_enum(ctx, index, DUK_ENUM_ARRAY_INDICES_ONLY);
        while (duk_next(ctx, -1, 1)) {
          rb_ary_store(ary, duk_to_int(ctx, -2), ctx_stack_to_value(state, -1));
          duk_pop_2(ctx);
        }
        duk_pop(ctx);
        return ary;
      } else if (duk_is_object(ctx, index)) {
        VALUE hash = rb_hash_new();
        duk_enum(ctx, index, DUK_ENUM_OWN_PROPERTIES_ONLY);
        while (duk_next(ctx, -1, 1)) {
          VALUE key = ctx_stack_to_value(state, -2);
          VALUE val = ctx_stack_to_value(state, -1);
          duk_pop_2(ctx);
          if (state->was_complex)
            continue;
          rb_hash_aset(hash, key, val);
        }
        duk_pop(ctx);
        return hash;
      } else {
        state->was_complex = 1;
        return state->complex_object;
      }

    case DUK_TYPE_BUFFER:
    case DUK_TYPE_POINTER:
    default:
      return state->complex_object;
  }

  return Qnil;
}

static void ctx_push_ruby_object(struct state *state, VALUE obj)
{
  duk_context *ctx = state->ctx;
  duk_idx_t arr_idx;
  VALUE str;

  switch (TYPE(obj)) {
    case T_FIXNUM:
      duk_push_int(ctx, NUM2INT(obj));
      return;

    case T_FLOAT:
      duk_push_number(ctx, NUM2DBL(obj));
      return;

    case T_STRING:
      str = encode_cesu8(state, obj);
      duk_push_lstring(ctx, RSTRING_PTR(str), RSTRING_LEN(str));
      return;

    case T_TRUE:
      duk_push_true(ctx);
      return;

    case T_FALSE:
      duk_push_false(ctx);
      return;

    case T_NIL:
      duk_push_null(ctx);
      return;

    case T_ARRAY:
      arr_idx = duk_push_array(ctx);
      for (int idx = 0; idx < RARRAY_LEN(obj); idx++) {
        ctx_push_ruby_object(state, rb_ary_entry(obj, idx));
        duk_put_prop_index(ctx, arr_idx, idx);
      }
      return;

    case T_HASH:
      duk_push_object(ctx);
      rb_hash_foreach(obj, ctx_push_hash_element, (VALUE)state);
      return;

    default:
      break;
  }

  if (state->has_ruby_bridge) {
    struct obj_ref *ref = malloc(sizeof(struct obj_ref));
    ref->obj = obj;
    ref->state = state;
    ref->prev = NULL;
    ref->next = state->objs;
    if (state->objs) {
      state->objs->prev = ref;
    }
    state->objs = ref;

    duk_push_object(ctx);
    duk_push_pointer(ctx, ref);
    duk_put_prop_string(ctx, -2, "\xFFruby");
    duk_push_c_function(ctx, rduk_finalize, 1);
    duk_set_finalizer(ctx, -2);
    return;
  }

  clean_raise(ctx, rb_eTypeError, "cannot convert %s", rb_obj_classname(obj));
}

static int ctx_push_hash_element(VALUE key, VALUE val, VALUE extra)
{
  struct state *state = (struct state*) extra;
  duk_context *ctx = state->ctx;

  Check_Type(key, T_STRING);
  duk_push_lstring(ctx, RSTRING_PTR(key), RSTRING_LEN(key));
  ctx_push_ruby_object(state, val);
  duk_put_prop(ctx, -3);
  return ST_CONTINUE;
}

static void raise_ctx_error(struct state *state)
{
  duk_context *ctx = state->ctx;
  duk_get_prop_string(ctx, -1, "name");
  const char *name = duk_safe_to_string(ctx, -1);

  duk_get_prop_string(ctx, -2, "message");
  const char *message = duk_to_string(ctx, -1);

  VALUE exc_class = error_name_class(name);
  VALUE exc = rb_exc_new2(exc_class, message);
  clean_raise_exc(ctx, exc);
}

static VALUE ctx_eval_string(VALUE self, VALUE source, VALUE filename)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  StringValue(source);
  StringValue(filename);

  ctx_push_ruby_object(state, source);
  ctx_push_ruby_object(state, filename);

  if (duk_pcompile(state->ctx, DUK_COMPILE_EVAL) == DUK_EXEC_ERROR) {
    raise_ctx_error(state);
  }

  if (duk_pcall(state->ctx, 0) == DUK_EXEC_ERROR) {
    raise_ctx_error(state);
  }

  VALUE res = ctx_stack_to_value(state, -1);
  duk_set_top(state->ctx, 0);
  return res;
}

static VALUE ctx_exec_string(VALUE self, VALUE source, VALUE filename)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  StringValue(source);
  StringValue(filename);

  ctx_push_ruby_object(state, source);
  ctx_push_ruby_object(state, filename);

  if (duk_pcompile(state->ctx, 0) == DUK_EXEC_ERROR) {
    raise_ctx_error(state);
  }

  if (duk_pcall(state->ctx, 0) == DUK_EXEC_ERROR) {
    raise_ctx_error(state);
  }

  duk_set_top(state->ctx, 0);
  return Qnil;
}

static void ctx_get_one_prop(struct state *state, VALUE name, int strict)
{
  duk_context *ctx = state->ctx;

  // Don't allow prop access on undefined/null
  if (duk_check_type_mask(ctx, -1, DUK_TYPE_MASK_UNDEFINED | DUK_TYPE_MASK_NULL)) {
    clean_raise(ctx, eTypeError, "invalid base value");
  }

  duk_push_lstring(ctx, RSTRING_PTR(name), RSTRING_LEN(name));
  duk_bool_t exists = duk_get_prop(ctx, -2);

  if (!exists && strict) {
    const char *str = StringValueCStr(name);
    clean_raise(ctx, eReferenceError, "identifier '%s' undefined", str);
  }
}

static void ctx_get_nested_prop(struct state *state, VALUE props)
{
  duk_context *ctx = state->ctx;

  switch (TYPE(props)) {
    case T_STRING:
      duk_push_global_object(ctx);
      ctx_get_one_prop(state, props, 1);
      return;

    case T_ARRAY:
      duk_push_global_object(ctx);

      long len = RARRAY_LEN(props);
      for (int i = 0; i < len; i++) {
        VALUE item = rb_ary_entry(props, i);
        Check_Type(item, T_STRING);

        // Only do a strict check on the first item
        ctx_get_one_prop(state, item, i == 0);
      }
      return;

    default:
      clean_raise(ctx, rb_eTypeError, "wrong argument type %s (expected String or Array)", rb_obj_classname(props));
      return;
  }
}

static VALUE ctx_get_prop(VALUE self, VALUE prop)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  ctx_get_nested_prop(state, prop);

  VALUE res = ctx_stack_to_value(state, -1);
  duk_set_top(state->ctx, 0);
  return res;
}

static
duk_ret_t rduk_send(duk_context *ctx)
{
  duk_get_prop_string(ctx, 0, "\xFFruby");
  struct obj_ref *ref = (struct obj_ref*)duk_require_pointer(ctx, -1);
  duk_pop(ctx);

  VALUE obj = ref->obj;
  struct state *state = ref->state;

  char *name = duk_require_string(ctx, 1);
  ID id_name = rb_intern(name);

  VALUE ruby_argv[10];

  int ruby_argc = duk_get_length(ctx, 2);
  for (int i = 0; i < ruby_argc; i++) {
    duk_get_prop_index(ctx, 2, i);
    ruby_argv[i] = ctx_stack_to_value(state, -1);
    duk_pop(ctx);
  }

  VALUE res = rb_funcall2(obj, id_name, ruby_argc, ruby_argv);

  ctx_push_ruby_object(state, res);
  return 1;
}

static
duk_ret_t rduk_finalize(duk_context *ctx)
{
  duk_get_prop_string(ctx, 0, "\xFFruby");
  struct obj_ref *ref = (struct obj_ref*)duk_require_pointer(ctx, -1);
  duk_pop(ctx);

  if (ref->prev == NULL) {
    ref->state->objs = ref->next;
  } else {
    ref->prev->next = ref->next;
  }

  if (ref->next) {
    ref->next->prev = ref->prev;
  }
}

static
duk_ret_t rduk_isObject(duk_context *ctx)
{
  duk_get_prop_string(ctx, 0, "\xFFruby");
  if (duk_is_pointer(ctx, -1)) {
    duk_push_true(ctx);
  } else {
    duk_push_false(ctx);
  }
  return 1;
}

static VALUE ctx_enable_ruby_bridge(struct state *state)
{
  duk_context *ctx = state->ctx;

  state->has_ruby_bridge = 1;

  duk_push_global_object(ctx);

  duk_push_object(ctx);

  duk_push_c_function(ctx, rduk_send, 3);
  duk_put_prop_string(ctx, -2, "send");

  duk_push_c_function(ctx, rduk_isObject, 1);
  duk_put_prop_string(ctx, -2, "isObject");

  VALUE toplevel_binding = rb_const_get(rb_cObject, rb_intern("TOPLEVEL_BINDING"));
  VALUE main = rb_funcall(toplevel_binding, rb_intern("eval"), 1, rb_str_new_cstr("self"));
  ctx_push_ruby_object(state, main);
  duk_put_prop_string(ctx, -2, "main");

  duk_put_prop_string(ctx, -2, "Ruby");

  duk_pop(ctx);

  return Qnil;
}

static VALUE ctx_call_prop(int argc, VALUE* argv, VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  VALUE prop;
  VALUE *prop_args;
  rb_scan_args(argc, argv, "1*", &prop, &prop_args);

  ctx_get_nested_prop(state, prop);

  // Swap receiver and function
  duk_swap_top(state->ctx, -2);

  // Push arguments
  for (int i = 1; i < argc; i++) {
    ctx_push_ruby_object(state, argv[i]);
  }

  if (duk_pcall_method(state->ctx, (argc - 1)) == DUK_EXEC_ERROR) {
    raise_ctx_error(state);
  }

  VALUE res = ctx_stack_to_value(state, -1);
  duk_set_top(state->ctx, 0);
  return res;
}

// Checks that we are in a fine state
static VALUE ctx_is_valid(VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  if (duk_is_valid_index(state->ctx, -1)) {
    return Qfalse;
  } else {
    return Qtrue;
  }
}

static void error_handler(duk_context *ctx, int code, const char *msg)
{
  clean_raise(ctx, error_code_class(code), "%s", msg);
}

VALUE complex_object_instance(VALUE self)
{
  return oComplexObject;
}

static VALUE ctx_initialize(int argc, VALUE *argv, VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  VALUE options;
  rb_scan_args(argc, argv, ":", &options);
  if (!NIL_P(options)) {
    state->complex_object = rb_hash_lookup2(options, ID2SYM(id_complex_object), state->complex_object);
    if (RTEST(rb_hash_lookup(options, ID2SYM(id_ruby_bridge)))) {
      ctx_enable_ruby_bridge(state);
    }
  }

  return Qnil;
}

static VALUE ctx_complex_object(VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  return state->complex_object;
}

void Init_duktape_ext()
{
  utf16enc = rb_enc_find("UTF-16LE");
  id_complex_object = rb_intern("complex_object");
  id_ruby_bridge = rb_intern("ruby_bridge");

  mDuktape = rb_define_module("Duktape");
  cContext = rb_define_class_under(mDuktape, "Context", rb_cObject);
  cComplexObject = rb_define_class_under(mDuktape, "ComplexObject", rb_cObject);

  eInternalError = rb_define_class_under(mDuktape, "InternalError", rb_eStandardError);
  eUnimplementedError = rb_define_class_under(mDuktape, "UnimplementedError", eInternalError);
  eUnsupportedError = rb_define_class_under(mDuktape, "UnsupportedError", eInternalError);
  eAllocError = rb_define_class_under(mDuktape, "AllocError", eInternalError);
  eAssertionError = rb_define_class_under(mDuktape, "AssertionError", eInternalError);
  eAPIError = rb_define_class_under(mDuktape, "APIError", eInternalError);
  eUncaughtError = rb_define_class_under(mDuktape, "UncaughtError", eInternalError);

  eError = rb_define_class_under(mDuktape, "Error", rb_eStandardError);
  eEvalError = rb_define_class_under(mDuktape, "EvalError", eError);
  eRangeError = rb_define_class_under(mDuktape, "RangeError", eError);
  eReferenceError = rb_define_class_under(mDuktape, "ReferenceError", eError);
  eSyntaxError = rb_define_class_under(mDuktape, "SyntaxError", eError);
  eTypeError = rb_define_class_under(mDuktape, "TypeError", eError);
  eURIError = rb_define_class_under(mDuktape, "URIError", eError);

  rb_define_alloc_func(cContext, ctx_alloc);

  rb_define_method(cContext, "initialize", ctx_initialize, -1);
  rb_define_method(cContext, "complex_object", ctx_complex_object, 0);
  rb_define_method(cContext, "eval_string", ctx_eval_string, 2);
  rb_define_method(cContext, "exec_string", ctx_exec_string, 2);
  rb_define_method(cContext, "get_prop", ctx_get_prop, 1);
  rb_define_method(cContext, "call_prop", ctx_call_prop, -1);
  rb_define_method(cContext, "_valid?", ctx_is_valid, 0);

  oComplexObject = rb_obj_alloc(cComplexObject);
  rb_define_singleton_method(cComplexObject, "instance", complex_object_instance, 0);
  rb_ivar_set(cComplexObject, rb_intern("duktape.instance"), oComplexObject);
}


/* UTF8 crap which is not exposed by Ruby */

static const unsigned long utf8_limits[] = {
    0x0,			/* 1 */
    0x80,			/* 2 */
    0x800,			/* 3 */
    0x10000,			/* 4 */
    0x200000,			/* 5 */
    0x4000000,			/* 6 */
    0x80000000,			/* 7 */
};

static unsigned long
utf8_to_uv(const char *p, long *lenp)
{
  int c = *p++ & 0xff;
  unsigned long uv = c;
  long n;

  if (!(uv & 0x80)) {
    *lenp = 1;
    return uv;
  }
  if (!(uv & 0x40)) {
    *lenp = 1;
    rb_raise(rb_eArgError, "malformed UTF-8 character");
  }

  if      (!(uv & 0x20)) { n = 2; uv &= 0x1f; }
  else if (!(uv & 0x10)) { n = 3; uv &= 0x0f; }
  else if (!(uv & 0x08)) { n = 4; uv &= 0x07; }
  else if (!(uv & 0x04)) { n = 5; uv &= 0x03; }
  else if (!(uv & 0x02)) { n = 6; uv &= 0x01; }
  else {
    *lenp = 1;
    rb_raise(rb_eArgError, "malformed UTF-8 character");
  }
  if (n > *lenp) {
    rb_raise(rb_eArgError, "malformed UTF-8 character (expected %ld bytes, given %ld bytes)",
        n, *lenp);
  }
  *lenp = n--;
  if (n != 0) {
    while (n--) {
      c = *p++ & 0xff;
      if ((c & 0xc0) != 0x80) {
        *lenp -= n + 1;
        rb_raise(rb_eArgError, "malformed UTF-8 character");
      }
      else {
        c &= 0x3f;
        uv = uv << 6 | c;
      }
    }
  }
  n = *lenp - 1;
  if (uv < utf8_limits[n]) {
    rb_raise(rb_eArgError, "redundant UTF-8 sequence");
  }
  return uv;
}


