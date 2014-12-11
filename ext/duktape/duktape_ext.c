#include "ruby.h"
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

static void error_handler(duk_context *, int, const char *);
static int ctx_push_hash_element(VALUE key, VALUE val, VALUE extra);

static void ctx_dealloc(void *ctx)
{
  duk_destroy_heap((duk_context *)ctx);
}

static VALUE ctx_alloc(VALUE klass)
{
  duk_context *ctx = duk_create_heap(NULL, NULL, NULL, NULL, error_handler);

  // Undefine require property
  duk_push_global_object(ctx);
  duk_push_string(ctx, "require");
  duk_del_prop(ctx, -2);
  duk_set_top(ctx, 0);

  return Data_Wrap_Struct(klass, NULL, ctx_dealloc, ctx);
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

static VALUE ctx_stack_to_value(duk_context *ctx, int index)
{
  size_t len;
  const char *buf;
  int type;

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
      return rb_str_new(buf, len);

    case DUK_TYPE_OBJECT:
      if (duk_is_function(ctx, index)) {
        return oComplexObject;
      } else if (duk_is_array(ctx, index)) {
        VALUE ary = rb_ary_new();
        duk_enum(ctx, index, DUK_ENUM_ARRAY_INDICES_ONLY);
        while (duk_next(ctx, -1, 1)) {
          rb_ary_store(ary, duk_to_int(ctx, -2), ctx_stack_to_value(ctx, -1));
          duk_pop_2(ctx);
        }
        duk_pop(ctx);
        return ary;
      } else if (duk_is_object(ctx, index)) {
        VALUE hash = rb_hash_new();
        duk_enum(ctx, index, 0);
        while (duk_next(ctx, -1, 1)) {
          rb_hash_aset(hash, ctx_stack_to_value(ctx, -2), ctx_stack_to_value(ctx, -1));
          duk_pop_2(ctx);
        }
        duk_pop(ctx);
        return hash;
      } else {
        return oComplexObject;
      }

    case DUK_TYPE_BUFFER:
    case DUK_TYPE_POINTER:
    default:
      return oComplexObject;
  }

  return Qnil;
}

static void ctx_push_ruby_object(duk_context *ctx, VALUE obj)
{
  duk_idx_t arr_idx;

  switch (TYPE(obj)) {
    case T_FIXNUM:
      duk_push_int(ctx, NUM2INT(obj));
      return;

    case T_FLOAT:
      duk_push_number(ctx, NUM2DBL(obj));
      return;

    case T_STRING:
      duk_push_lstring(ctx, RSTRING_PTR(obj), RSTRING_LEN(obj));
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
        ctx_push_ruby_object(ctx, rb_ary_entry(obj, idx));
        duk_put_prop_index(ctx, arr_idx, idx);
      }
      return;

    case T_HASH:
      duk_push_object(ctx);
      rb_hash_foreach(obj, ctx_push_hash_element, (VALUE)ctx);
      return;

    default:
      // Cannot convert
      break;
  }

  rb_raise(rb_eTypeError, "cannot convert %s", rb_obj_classname(obj));
}

struct ARGS
{
  duk_context *ctx;
  int argc;
  VALUE *argv;
};

static VALUE ctx_push_args(VALUE vargs)
{
  struct ARGS *args = (struct ARGS*)vargs;
  for (int i = 0; i < args->argc; i++) {
    ctx_push_ruby_object(args->ctx, args->argv[i]);
  }
  return Qnil;
}

static int ctx_push_hash_element(VALUE key, VALUE val, VALUE extra)
{
  duk_context *ctx = (duk_context*) extra;

  Check_Type(key, T_STRING);
  duk_push_lstring(ctx, RSTRING_PTR(key), RSTRING_LEN(key));
  ctx_push_ruby_object(ctx, val);
  duk_put_prop(ctx, -3);
  return ST_CONTINUE;
}

static void raise_ctx_error(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "name");
  const char *name = duk_safe_to_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "message");
  const char *message = duk_to_string(ctx, -1);
  duk_pop(ctx);

  rb_raise(error_name_class(name), "%s", message);
}

static VALUE ctx_eval_string(VALUE self, VALUE source, VALUE filename)
{
  duk_context *ctx;
  Data_Get_Struct(self, duk_context, ctx);

  StringValue(source);
  StringValue(filename);

  duk_push_lstring(ctx, RSTRING_PTR(source), RSTRING_LEN(source));
  duk_push_lstring(ctx, RSTRING_PTR(filename), RSTRING_LEN(filename));

  if (duk_pcompile(ctx, DUK_COMPILE_EVAL) == DUK_EXEC_ERROR) {
    raise_ctx_error(ctx);
  }

  if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR) {
    raise_ctx_error(ctx);
  }

  VALUE res = ctx_stack_to_value(ctx, -1);
  duk_set_top(ctx, 0);
  return res;
}

static VALUE ctx_exec_string(VALUE self, VALUE source, VALUE filename)
{
  duk_context *ctx;
  Data_Get_Struct(self, duk_context, ctx);

  StringValue(source);
  StringValue(filename);

  duk_push_lstring(ctx, RSTRING_PTR(source), RSTRING_LEN(source));
  duk_push_lstring(ctx, RSTRING_PTR(filename), RSTRING_LEN(filename));

  if (duk_pcompile(ctx, 0) == DUK_EXEC_ERROR) {
    raise_ctx_error(ctx);
  }

  if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR) {
    raise_ctx_error(ctx);
  }

  duk_set_top(ctx, 0);
  return Qnil;
}

static void ctx_get_one_prop(duk_context *ctx, VALUE name, int strict)
{
  // Don't allow prop access on undefined/null
  if (duk_check_type_mask(ctx, -1, DUK_TYPE_MASK_UNDEFINED | DUK_TYPE_MASK_NULL)) {
    rb_raise(eTypeError, "invalid base value");
  }

  duk_push_lstring(ctx, RSTRING_PTR(name), RSTRING_LEN(name));
  duk_bool_t exists = duk_get_prop(ctx, -2);

  if (!exists && strict) {
    const char *str = StringValueCStr(name);
    rb_raise(eReferenceError, "identifier '%s' undefined", str);
  }
}

static void ctx_get_nested_prop(duk_context *ctx, VALUE props)
{
  switch (TYPE(props)) {
    case T_STRING:
      duk_push_global_object(ctx);
      ctx_get_one_prop(ctx, props, 1);
      return;

    case T_ARRAY:
      duk_push_global_object(ctx);

      long len = RARRAY_LEN(props);
      for (int i = 0; i < len; i++) {
        VALUE item = rb_ary_entry(props, i);
        Check_Type(item, T_STRING);

        // Only do a strict check on the first item
        ctx_get_one_prop(ctx, item, i == 0);
      }
      return;

    default:
      rb_raise(rb_eTypeError, "wrong argument type %s (expected String or Array)", rb_obj_classname(props));
      return;
  }
}

static VALUE ctx_get_prop(VALUE self, VALUE prop)
{
  duk_context *ctx;
  Data_Get_Struct(self, duk_context, ctx);

  ctx_get_nested_prop(ctx, prop);

  VALUE res = ctx_stack_to_value(ctx, -1);
  duk_set_top(ctx, 0);
  return res;
}

static VALUE ctx_call_prop(int argc, VALUE* argv, VALUE self)
{
  duk_context *ctx;
  Data_Get_Struct(self, duk_context, ctx);

  VALUE prop;
  VALUE *prop_args;
  rb_scan_args(argc, argv, "1*", &prop, &prop_args);

  ctx_get_nested_prop(ctx, prop);

  // Swap receiver and function
  duk_swap_top(ctx, -2);

  struct ARGS args;
  args.ctx = ctx;
  args.argc = argc - 1;
  args.argv = argv + 1;
  int state = 0;

  rb_protect(ctx_push_args, (VALUE)&args, &state);

  if (state) {
    // Exception happened when handling arguments
    // Reset stack to ensure we don't leak any data
    duk_set_top(ctx, 0);
    rb_jump_tag(state);
  }

  if (duk_pcall_method(ctx, (argc - 1)) == DUK_EXEC_ERROR) {
    raise_ctx_error(ctx);
  }

  VALUE res = ctx_stack_to_value(ctx, -1);
  duk_set_top(ctx, 0);
  return res;
}

static void error_handler(duk_context *ctx, int code, const char *msg)
{
  duk_set_top(ctx, 0);
  return rb_raise(error_code_class(code), "%s", msg);
}

VALUE complex_object_instance(VALUE self)
{
  return oComplexObject;
}

void Init_duktape_ext()
{
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

  rb_define_method(cContext, "eval_string", ctx_eval_string, 2);
  rb_define_method(cContext, "exec_string", ctx_exec_string, 2);
  rb_define_method(cContext, "get_prop", ctx_get_prop, 1);
  rb_define_method(cContext, "call_prop", ctx_call_prop, -1);

  oComplexObject = rb_obj_alloc(cComplexObject);
  rb_define_singleton_method(cComplexObject, "instance", complex_object_instance, 0);
  rb_ivar_set(cComplexObject, rb_intern("duktape.instance"), oComplexObject);
}
