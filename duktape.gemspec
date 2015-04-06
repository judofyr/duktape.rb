require File.expand_path("../lib/duktape/version", __FILE__)

Gem::Specification.new do |s|
  s.name        = "duktape"
  s.version     = Duktape::VERSION
  s.date        = "2015-04-06"
  s.summary     = "Bindings to the Duktape JavaScript interpreter"
  
  s.author      = "Magnus Holm"
  s.email       = "judofyr@gmail.com"
  s.homepage    = "https://github.com/judofyr/duktape.rb"
  s.license     = "MIT"
  
  s.files       = File.readlines('MANIFEST.txt').map(&:strip)
  s.extensions  = "ext/duktape/extconf.rb"
  
  s.required_ruby_version = ">= 1.9.3"
end
