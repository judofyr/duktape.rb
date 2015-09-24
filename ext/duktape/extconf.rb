require 'mkmf'
require 'zlib'

$CFLAGS += ' -std=c99'
create_makefile 'duktape_ext'

