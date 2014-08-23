require 'bundler/setup'
require 'rake/extensiontask'

$gemspec = Gem::Specification.load('duktape.gemspec')
Rake::ExtensionTask.new do |ext|
  ext.name = :duktape_ext
  ext.ext_dir = 'ext/duktape'
  ext.gem_spec = $gemspec
end

task :compile => 'ext/duktape/duktape.c'

Gem::PackageTask.new($gemspec) do |pkg|
end

DUKTAPE_VERSION = Duktape::VERSION.split('.')[0,3].join('.')
file 'ext/duktape/duktape.c' do
  url = "http://duktape.org/duktape-#{DUKTAPE_VERSION}.tar.xz"
  mkdir_p "tmp"
  chdir "tmp" do
    sh "curl", "-O", url
    sh "tar", "xf", "duktape-#{DUKTAPE_VERSION}.tar.xz"
    cp FileList["duktape-#{DUKTAPE_VERSION}/src/*"], '../ext/duktape'
  end
end

task :test => :compile do
  ruby 'test/test_duktape.rb'
end

task :default => :test

