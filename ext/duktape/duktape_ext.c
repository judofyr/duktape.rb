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

static VALUE sDefaultFilename;
static ID id_complex_object;

static int ctx_push_hash_element(VALUE key, VALUE val, VALUE extra);

static unsigned long
utf8_to_uv(const char *p, long *lenp);

#define clean_raise(ctx, ...) (duk_set_top(ctx, 0), rb_raise(__VA_ARGS__))
#define clean_raise_exc(ctx, ...) (duk_set_top(ctx, 0), rb_exc_raise(__VA_ARGS__))

struct state {
  duk_context *ctx;
  int is_fatal;
  VALUE complex_object;
  int was_complex;
  VALUE blocks;
};

static void error_handler(void *, const char *);
static void check_fatal(struct state *);

static void ctx_dealloc(void *ptr)
{
  struct state *state = (struct state *)ptr;
  duk_destroy_heap(state->ctx);
  free(state);
}

static void ctx_mark(struct state *state)
{
  rb_gc_mark(state->complex_object);
  rb_gc_mark(state->blocks);
}

static VALUE ctx_alloc(VALUE klass)
{
  struct state *state = malloc(sizeof(struct state));

  duk_context *ctx = duk_create_heap(NULL, NULL, NULL, state, error_handler);

  state->ctx = ctx;
  state->is_fatal = 0;
  state->complex_object = oComplexObject;
  state->blocks = rb_ary_new();

  // Undefine require property
  duk_push_global_object(ctx);
  duk_push_string(ctx, "require");
  duk_del_prop(ctx, -2);
  duk_set_top(ctx, 0);

  return Data_Wrap_Struct(klass, ctx_mark, ctx_dealloc, state);
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
      duk_push_number(ctx, NUM2LONG(obj));
      return;

    case T_FLOAT:
      duk_push_number(ctx, NUM2DBL(obj));
      return;

    case T_BIGNUM:
      duk_push_number(ctx, NUM2DBL(obj));
      return;

    case T_SYMBOL:
#ifdef HAVE_RB_SYM2STR
      obj = rb_sym2str(obj);
#else
      obj = rb_id2str(SYM2ID(obj));
#endif
      // Intentional fall-through:

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
      // Cannot convert
      break;
  }

  clean_raise(ctx, rb_eTypeError, "cannot convert %s", rb_obj_classname(obj));
}

static int ctx_push_hash_element(VALUE key, VALUE val, VALUE extra)
{
  struct state *state = (struct state*) extra;
  duk_context *ctx = state->ctx;

  switch (TYPE(key)) {
    case T_SYMBOL:
    case T_STRING:
      ctx_push_ruby_object(state, key);
      break;
    default:
      clean_raise(ctx, rb_eTypeError, "invalid key type %s", rb_obj_classname(key));
  }

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

/*
 * call-seq:
 *   eval_string(string[, filename]) -> obj
 *
 * Evaluate JavaScript expression within context returning the value as a Ruby
 * object.
 *
 *     ctx.eval_string("40 + 2") #=> 42
 *
 */
static VALUE ctx_eval_string(int argc, VALUE *argv, VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);
  check_fatal(state);

  VALUE source;
  VALUE filename;

  rb_scan_args(argc, argv, "11", &source, &filename);

  if (NIL_P(filename)) {
    filename = sDefaultFilename;
  }

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

/*
 * call-seq:
 *   exec_string(string[, filename]) -> nil
 *
 * Evaluate JavaScript expression within context returning the value as a Ruby
 * object.
 *
 *     ctx.exec_string("var foo = 42")
 *     ctx.eval_string("foo") #=> 42
 *
 */
static VALUE ctx_exec_string(int argc, VALUE *argv, VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);
  check_fatal(state);

  VALUE source;
  VALUE filename;

  rb_scan_args(argc, argv, "11", &source, &filename);

  if (NIL_P(filename)) {
    filename = sDefaultFilename;
  }

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

/*
 * call-seq:
 *   get_prop(name) -> obj
 *   get_prop([names,...]) -> obj
 *
 * Access the property of the global object. An Array of names can be given
 * to access the property on a nested object.
 *
 *     ctx.exec_string("var n = 42", "foo.js")
 *     ctx.get_prop("n") #=> 42
 *
 *     ctx.get_prop(["Math", "PI"]) #=> 3.14
 *
 */
static VALUE ctx_get_prop(VALUE self, VALUE prop)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);
  check_fatal(state);

  ctx_get_nested_prop(state, prop);

  VALUE res = ctx_stack_to_value(state, -1);
  duk_set_top(state->ctx, 0);
  return res;
}


/*
 * call-seq:
 *   call_prop(name, params,...) -> obj
 *   call_prop([names,...], params,...) -> obj
 *
 * Call a function defined in the global scope with the given parameters. An
 * Array of names can be given to call a function on a nested object.
 *
 *     ctx.call_prop("parseInt", "42")       #=> 42
 *     ctx.call_prop(["Math", "pow"], 2, 10) #=> 1024
 *
 */
static VALUE ctx_call_prop(int argc, VALUE* argv, VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);
  check_fatal(state);

  VALUE prop;
  rb_scan_args(argc, argv, "1*", &prop, NULL);

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

static duk_ret_t ctx_call_pushed_function(duk_context *ctx) {
  VALUE block; // the block to yield
  struct state *state;
  int nargs = duk_get_top(ctx); // number of arguments of the block (arity)
  VALUE args = rb_ary_new(); // the block to yield needs an array of arguments
  VALUE result; // the result returned by yielding the block

  duk_push_current_function(ctx);

  // get the block which is a property of the pushed function
  duk_get_prop_string(ctx, -1, "block");
  block = (VALUE) duk_get_pointer(ctx, -1);
  duk_pop(ctx);

  // get the state so that we don't need to a create a new one
  duk_get_prop_string(ctx, -1, "state");
  state = (struct state *) duk_get_pointer(ctx, -1);
  duk_pop(ctx);

  // before pushing each argument to the array, each one needs to be converted into a ruby value
  for (int i = 0; i < nargs; i++)
    rb_ary_push(args, ctx_stack_to_value(state, i));

  result = rb_proc_call(block, args); // yield
  ctx_push_ruby_object(state, result);

  return 1;
}

/*
 * call-seq:
 *   ctx_define_function(name, &block) -> nil
 *
 * Define a function defined in the global scope and identified by a name.
 *
 *     ctx.ctx_define_function("hello_world") { |ctx| 'Hello world' } #=> nil
 *
 */
static VALUE ctx_define_function(VALUE self, VALUE prop)
{
  VALUE block;
  struct state *state;
  duk_context *ctx;

  // a block is required
  if (!rb_block_given_p())
    rb_raise(rb_eArgError, "Expected block");

  // get the context
  Data_Get_Struct(self, struct state, state);
  check_fatal(state);

  ctx = state->ctx;

  // the c function is available in the global scope
  duk_push_global_object(ctx);

  duk_push_c_function(ctx, ctx_call_pushed_function, DUK_VARARGS);

  block = rb_block_proc();
  rb_ary_push(state->blocks, block); // block will be properly garbage collected

  // both block and state are required by the pushed function
  duk_push_string(ctx, "block");
  duk_push_pointer(ctx, (void *) block);
  duk_def_prop(ctx, -3,  DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE | 0);

  duk_push_string(ctx, "state");
  duk_push_pointer(ctx, (void *) state);
  duk_def_prop(ctx, -3,  DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE | 0);

  duk_put_prop_string(ctx, -2, StringValueCStr(prop));

  return Qnil;
}

/*
 * :nodoc:
 *
 * Checks that we are in a fine state
 */
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

/*
 * :nodoc:
 *
 * Invokes duk_fatal(). Only used for testing.
 */
static VALUE ctx_invoke_fatal(VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  duk_fatal(state->ctx, "induced fatal error");

  return Qnil;
}

static void error_handler(void *udata, const char *msg)
{
  struct state *state = (struct state *)udata;

  if (msg == NULL) {
    msg = "fatal error";
  }
  state->is_fatal = 1;
  rb_raise(eInternalError, "%s", msg);
}

static void check_fatal(struct state *state)
{
  if (state->is_fatal) {
    rb_raise(eInternalError, "fatal error");
  }
}

VALUE complex_object_instance(VALUE self)
{
  return oComplexObject;
}

/*
 * call-seq:
 *   Context.new
 *   Context.new(complex_object: obj)
 *
 * Returns a new JavaScript evaluation context.
 *
 */
static VALUE ctx_initialize(int argc, VALUE *argv, VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  VALUE options;
  rb_scan_args(argc, argv, ":", &options);
  if (!NIL_P(options))
    state->complex_object = rb_hash_lookup2(options, ID2SYM(id_complex_object), state->complex_object);

  return Qnil;
}

/*
 * call-seq:
 *   complex_object -> obj
 *
 * Returns the default complex object, the value that would be returned if a
 * JavaScript object had no representation in Ruby, such as a JavaScript
 * Function. See also Context::new.
 *
 *     ctx.complex_object #=> #<Duktape::ComplexObject>
 *
 */
static VALUE ctx_complex_object(VALUE self)
{
  struct state *state;
  Data_Get_Struct(self, struct state, state);

  return state->complex_object;
}

void Init_duktape_ext()
{
  int one = 1;
  if (*(char*)(&one) == 1) {
    utf16enc = rb_enc_find("UTF-16LE");
  } else {
    utf16enc = rb_enc_find("UTF-16BE");
  }
  id_complex_object = rb_intern("complex_object");

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
  rb_define_method(cContext, "eval_string", ctx_eval_string, -1);
  rb_define_method(cContext, "exec_string", ctx_exec_string, -1);
  rb_define_method(cContext, "get_prop", ctx_get_prop, 1);
  rb_define_method(cContext, "call_prop", ctx_call_prop, -1);
  rb_define_method(cContext, "define_function", ctx_define_function, 1);
  rb_define_method(cContext, "_valid?", ctx_is_valid, 0);
  rb_define_method(cContext, "_invoke_fatal", ctx_invoke_fatal, 0);

  oComplexObject = rb_obj_alloc(cComplexObject);
  rb_define_singleton_method(cComplexObject, "instance", complex_object_instance, 0);
  rb_ivar_set(cComplexObject, rb_intern("duktape.instance"), oComplexObject);

  sDefaultFilename = rb_str_new2("(duktape)");
  OBJ_FREEZE(sDefaultFilename);
  rb_global_variable(&sDefaultFilename);
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
