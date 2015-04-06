require 'bundler/setup'
require 'rake/extensiontask'
require 'rdoc/task'
require 'sdoc'

$gemspec = Gem::Specification.load('duktape.gemspec')

DUKTAPE_VERSION = Duktape::VERSION.split('.')[0,3].join('.')
task :update_duktape do
  url = "http://duktape.org/duktape-#{DUKTAPE_VERSION}.tar.xz"
  mkdir_p "tmp"
  chdir "tmp" do
    sh "curl", "-O", url
    sh "tar", "xf", "duktape-#{DUKTAPE_VERSION}.tar.xz"
    cp FileList["duktape-#{DUKTAPE_VERSION}/src/*"], '../ext/duktape'
  end
end

Rake::ExtensionTask.new do |ext|
  ext.name = :duktape_ext
  ext.ext_dir = 'ext/duktape'
  ext.gem_spec = $gemspec
end

Gem::PackageTask.new($gemspec) do |pkg|
end

RDoc::Task.new(:docs) do |rd|
  rd.main = "README.md"
  rd.rdoc_files.include("README.md", "ext/duktape/duktape_ext.c")
  rd.options << '--fmt' << 'sdoc'
  rd.rdoc_dir = 'docs'
end

task :test => :compile do
  ruby 'test/test_duktape.rb'
end

task :default => :test
