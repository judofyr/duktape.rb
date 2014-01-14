#include "ruby.h"
#include "duktape.h"

static VALUE mDuktape;
static VALUE cContext;
static VALUE eContextError;
static void error_handler(duk_context *, int);

static void ctx_dealloc(void *ctx)
{
  duk_destroy_heap((duk_context *)ctx);
}

static VALUE ctx_alloc(VALUE klass)
{
  duk_context *ctx = duk_create_heap(NULL, NULL, NULL, NULL, error_handler);
  return Data_Wrap_Struct(klass, NULL, ctx_dealloc, ctx);
}

static VALUE ctx_stack_to_value(duk_context *ctx, int index)
{
  size_t len;
  char *buf;
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
    case DUK_TYPE_BUFFER:
    case DUK_TYPE_POINTER:
    default:
      rb_raise(eContextError, "cannot convert complex object");
  }

  return Qnil;
}

static int ctx_push_ruby_object(duk_context *ctx, VALUE obj)
{
  switch (TYPE(obj)) {
    case T_FIXNUM:
      duk_push_int(ctx, NUM2INT(obj));
      break;

    case T_FLOAT:
      duk_push_number(ctx, NUM2DBL(obj));
      break;

    case T_STRING:
      duk_push_lstring(ctx, RSTRING_PTR(obj), RSTRING_LEN(obj));
      break;

    case T_TRUE:
      duk_push_true(ctx);
      break;

    case T_FALSE:
      duk_push_false(ctx);
      break;

    case T_NIL:
      duk_push_null(ctx);
      break;

    default:
      // Cannot convert
      return 0;
  }

  // Everything is fine
  return 1;
}

static VALUE ctx_eval_string(VALUE self, VALUE source, VALUE filename)
{
  duk_context *ctx;
  Data_Get_Struct(self, duk_context, ctx);

  duk_push_lstring(ctx, RSTRING_PTR(source), RSTRING_LEN(source));
  duk_push_lstring(ctx, RSTRING_PTR(filename), RSTRING_LEN(filename));
  duk_compile(ctx, DUK_COMPILE_EVAL);
  duk_call(ctx, 0);

  VALUE res = ctx_stack_to_value(ctx, -1);
  duk_set_top(ctx, 0);
  return res;
}

static VALUE ctx_exec_string(VALUE self, VALUE source, VALUE filename)
{
  duk_context *ctx;
  Data_Get_Struct(self, duk_context, ctx);

  duk_push_lstring(ctx, RSTRING_PTR(source), RSTRING_LEN(source));
  duk_push_lstring(ctx, RSTRING_PTR(filename), RSTRING_LEN(filename));
  duk_compile(ctx, 0);
  duk_call(ctx, 0);
  duk_set_top(ctx, 0);
  return Qnil;
}

static VALUE ctx_get_prop(VALUE self, VALUE prop)
{
  duk_context *ctx;
  Data_Get_Struct(self, duk_context, ctx);

  Check_Type(prop, T_STRING);

  duk_push_global_object(ctx);
  duk_push_lstring(ctx, RSTRING_PTR(prop), RSTRING_LEN(prop));
  if (!duk_get_prop(ctx, -2)) {
    duk_set_top(ctx, 0);
    rb_raise(eContextError, "no such prop: %"PRIsVALUE, prop);
  }

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

  Check_Type(prop, T_STRING);

  duk_push_global_object(ctx);
  duk_push_lstring(ctx, RSTRING_PTR(prop), RSTRING_LEN(prop));

  for (int i = 1; i < argc; i++) {
    if (!ctx_push_ruby_object(ctx, argv[i])) {
      duk_set_top(ctx, 0);
      rb_raise(eContextError, "unknown object: %+"PRIsVALUE, argv[i]);
    }
  }

  duk_call_prop(ctx, -(argc + 1), (argc - 1));
  return ctx_stack_to_value(ctx, -1);
}

static void error_handler(duk_context *ctx, int code)
{
  duk_set_top(ctx, 0);
  rb_raise(eContextError, "fatal duktape error");
}

void Init_duktape_ext()
{
  mDuktape = rb_define_module("Duktape");
  cContext = rb_define_class_under(mDuktape, "Context", rb_cObject);
  eContextError = rb_define_class_under(mDuktape, "ContextError", rb_eStandardError);

  rb_define_alloc_func(cContext, ctx_alloc);

  rb_define_method(cContext, "eval_string", ctx_eval_string, 2);
  rb_define_method(cContext, "exec_string", ctx_exec_string, 2);
  rb_define_method(cContext, "get_prop", ctx_get_prop, 1);
  rb_define_method(cContext, "call_prop", ctx_call_prop, -1);
}

