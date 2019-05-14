require 'bundler/setup'
require 'bundler/gem_tasks'
require 'rake/extensiontask'
require 'rdoc/task'
require 'sdoc'

$gemspec = Gem::Specification.load('duktape.gemspec')

DUKTAPE_VERSION = Duktape::VERSION.split('.')[0,3].join('.')

duktape_name = "duktape-#{DUKTAPE_VERSION}"
archive_name = "#{duktape_name}.tar.xz"
archive_url = "https://duktape.org/#{archive_name}"
archive_path = "tmp/#{archive_name}"
duktape_path = "tmp/#{duktape_name}"
duktape_build_path = "tmp/#{duktape_name}/build"

directory "tmp"

file archive_path => "tmp" do
  chdir "tmp" do
    sh "curl", "-O", archive_url
  end
end

file duktape_path => archive_path do
  chdir "tmp" do
    sh "tar", "xf", archive_name
  end
end

file duktape_build_path => duktape_path do
  chdir duktape_path do
    sh "python2", "tools/configure.py", "--output-directory", "build"
  end
end

task :update_duktape => duktape_build_path do
  cp FileList[duktape_build_path + "/*.{h,c}"], 'ext/duktape'
end

Rake::ExtensionTask.new do |ext|
  ext.name = :duktape_ext
  ext.ext_dir = 'ext/duktape'
  ext.gem_spec = $gemspec
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
