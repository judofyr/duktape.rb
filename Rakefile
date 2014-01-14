require 'bundler/setup'
require 'rake/extensiontask'

$gemspec = Gem::Specification.load('duktape.gemspec')
Rake::ExtensionTask.new do |ext|
  ext.name = :duktape_ext
  ext.ext_dir = 'ext/duktape'
  ext.gem_spec = $gemspec
end

Gem::PackageTask.new($gemspec) do |pkg|
end

task :test => :compile do
  ruby 'test/test_duktape.rb'
end

task :default => :test

