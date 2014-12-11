require 'mkmf'
require 'zlib'

$CFLAGS += ' -std=c99'
$defs << '-DDUK_OPT_DEEP_C_STACK'
create_makefile 'duktape_ext'

