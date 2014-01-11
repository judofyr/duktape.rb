file 'ext/duktape/Makefile' => 'ext/duktape/extconf.rb' do
  cd 'ext/duktape' do
    ruby 'extconf.rb'
  end
end

task :clean => 'ext/duktape/Makefile' do
  cd 'ext/duktape' do
    sh 'make clean'
  end
end

task :build => 'ext/duktape/Makefile' do
  cd 'ext/duktape' do
    sh 'make'
  end
end

