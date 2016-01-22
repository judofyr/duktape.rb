require 'mkmf'
require 'zlib'

$CFLAGS += ' -std=c99'
have_func 'rb_sym2str'
create_makefile 'duktape_ext'

