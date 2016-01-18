require 'bundler/setup'
require 'bundler/gem_tasks'
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

task :upload do
  require 'aws-sdk'
  s3 = Aws::S3::Resource.new
  bucket = s3.bucket('holmium')
  prefix = "duktape-gem/"
  commit = `git rev-parse HEAD`.strip
  version = RUBY_VERSION.split(".")[0,2].join(".")

  Dir.glob("pkg/*.gem") do |filename|
    path = File.expand_path(filename)
    target = File.basename(filename)
    target = target.sub(/\.gem/, ".ruby#{version}.sha#{commit}.gem")
    object = bucket.object(prefix + target)
    object.upload_file(path, :acl => "public-read")
    puts "Gem available at #{object.public_url}"
  end
end

task :fatlinux => :build do |t, args|
  platform = "x86_64-linux"
  commit = `git rev-parse HEAD`.strip
  version = Duktape::VERSION

  paths = ["pkg/duktape-#{version}.gem"]

  %w[1.9 2.0 2.1 2.2 2.3].each do |ruby_version|
    paths << "https://holmium.s3-eu-west-1.amazonaws.com/duktape-gem/duktape-#{version}-#{platform}.ruby#{ruby_version}.sha#{commit}.gem"
  end

  sh "fatgem", *paths
end

