require 'mkmf'
require 'zlib'

source = File.expand_path('../duktape.c', __FILE__)
gzipped_source = source + ".gz"

Zlib::GzipReader.open(gzipped_source) do |f|
  File.open(source, "w") do |g|
    IO.copy_stream(f, g)
  end
end

create_makefile 'duktape_ext'

