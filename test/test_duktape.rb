root = File.expand_path('../..', __FILE__)
$LOAD_PATH << (root + '/lib') << (root + '/ext/duktape')

require 'minitest'
require 'minitest/autorun'
require 'duktape'

class TestDuktape < Minitest::Spec
  def setup
    @ctx = Duktape::Context.new(**options)
    @require_valid = true
  end

  def options
    {}
  end

  def teardown
    if @require_valid
      assert @ctx._valid?, "Context is in a weird state"
    end
  end

  def test_initialize
    assert Duktape::Context.new(foo: 123)
    assert Duktape::Context.new
  end

  describe "#eval_string" do
    def test_requires_string
      assert_raises(TypeError) do
        @ctx.eval_string(123)
      end
    end

    def test_with_filename
      assert_equal '123', @ctx.eval_string('"123"', __FILE__)
    end

    def test_works_with_to_str
      a = Object.new
      def a.to_str
        '"123"'
      end

      assert_equal '123', @ctx.eval_string(a)
    end

    def test_string
      assert_equal '123', @ctx.eval_string('"123"')
    end

    def test_boolean
      assert_equal true,  @ctx.eval_string('1 == 1')
      assert_equal false, @ctx.eval_string('1 == 2')
    end

    def test_number
      assert_equal 123.0, @ctx.eval_string('123')
    end

    def test_nil_undef
      assert_nil @ctx.eval_string('null')
      assert_nil @ctx.eval_string('undefined')
    end

    def test_array
      assert_equal [1, 2, 3], @ctx.eval_string('[1, 2, 3]')
      assert_equal [1, [2, 3]], @ctx.eval_string('[1, [2, 3]]')
      assert_equal [1, [2, [3]]], @ctx.eval_string('[1, [2, [3]]]')
    end

    def test_object
      assert_equal({ "a" => 1, "b" => 2 }, @ctx.eval_string('({a: 1, b: 2})'))
      assert_equal({ "a" => 1, "b" => [2] }, @ctx.eval_string('({a: 1, b: [2]})'))
      assert_equal({ "a" => 1, "b" => { "c" => 2 } }, @ctx.eval_string('({a: 1, b: {c: 2}})'))
    end

    def test_complex_object
      assert_equal Duktape::ComplexObject.instance,
        @ctx.eval_string('a = function() {}')
    end

    def test_throw_error
      err = assert_raises(Duktape::Error) do
        @ctx.eval_string('throw new Error("boom")')
      end

      assert_equal "boom", err.message
    end

    def test_reference_error
      err = assert_raises(Duktape::ReferenceError) do
        @ctx.eval_string('fail')
      end

      assert_equal "identifier 'fail' undefined", err.message
    end

    def test_syntax_error
      err = assert_raises(Duktape::SyntaxError) do
        @ctx.eval_string('{')
      end

      assert_equal "parse error (line 1, end of input)", err.message
    end

    def test_type_error
      err = assert_raises(Duktape::TypeError) do
        @ctx.eval_string('null.fail')
      end

      assert_equal "cannot read property 'fail' of null", err.message
    end

    def test_error_message_not_garbage_collected
      1000.times do
        err = assert_raises(Duktape::ReferenceError) do
          @ctx.eval_string('fail')
        end
        assert_equal "identifier 'fail' undefined", err.message
      end
    end
  end

  describe "#exec_string" do
    def test_with_filename
      @ctx.exec_string('a = 1', __FILE__)
      assert_equal 1.0, @ctx.eval_string('a', __FILE__)
    end

    def test_basic
      @ctx.exec_string('a = 1')
      assert_equal 1.0, @ctx.eval_string('a')
    end

    def test_doesnt_try_convert
      @ctx.exec_string('a = {b:1}')
      assert_equal 1.0, @ctx.eval_string('a.b')
    end

    def test_throw_error
      err = assert_raises(Duktape::Error) do
        @ctx.exec_string('throw new Error("boom")')
      end

      assert_equal "boom", err.message
    end

    def test_reference_error
      err = assert_raises(Duktape::ReferenceError) do
        @ctx.exec_string('fail')
      end

      assert_equal "identifier 'fail' undefined", err.message
    end

    def test_syntax_error
      err = assert_raises(Duktape::SyntaxError) do
        @ctx.exec_string('{')
      end

      assert_equal "parse error (line 1, end of input)", err.message
    end

    def test_type_error
      err = assert_raises(Duktape::TypeError) do
        @ctx.exec_string('null.fail')
      end

      assert_equal "cannot read property 'fail' of null", err.message
    end
  end

  describe "#get_prop" do
    def test_basic
      @ctx.eval_string('a = 1')
      assert_equal 1.0, @ctx.get_prop('a')
    end

    def test_nested
      @ctx.eval_string('a = {}; a.b = {}; a.b.c = 1')
      assert_equal 1.0, @ctx.get_prop(['a', 'b', 'c'])
    end

    def test_nested_undefined
      @ctx.eval_string('a = {}')
      assert_nil @ctx.get_prop(['a', 'missing'])
    end

    def test_missing
      err = assert_raises(Duktape::ReferenceError) do
        @ctx.get_prop('a')
      end

      assert_equal "identifier 'a' undefined", err.message
    end

    def test_nested_reference_error
      err = assert_raises(Duktape::ReferenceError) do
        @ctx.get_prop(['a', 'b'])
      end

      assert_equal "identifier 'a' undefined", err.message
    end

    def test_nested_type_error
      @ctx.eval_string('a = {};')

      err = assert_raises(Duktape::TypeError) do
        @ctx.get_prop(['a', 'b', 'c'])
      end

      assert_equal 'invalid base value', err.message
    end
  end

  describe "#call_prop" do
    before do
      @ctx.eval_string('function id(a) { return a }')
    end

    def test_str
      assert_equal 'Hei', @ctx.call_prop('id', 'Hei')
    end

    def test_symbols
      assert_equal "hello", @ctx.call_prop('id', :hello)
    end

    def test_fixnum
      assert_equal 2.0, @ctx.call_prop('id', 2)
      assert_equal 1507843033737.0, @ctx.call_prop('id', 1507843033737)
    end

    def test_float
      assert_equal 2.0, @ctx.call_prop('id', 2.0)
    end

    def test_bignum
      assert_equal 2**100, @ctx.call_prop('id', 2**100)
    end

    def test_true
      assert_equal true, @ctx.call_prop('id', true)
    end

    def test_false
      assert_equal false, @ctx.call_prop('id', false)
    end

    def test_nil
      assert_nil @ctx.call_prop('id', nil)
    end

    def test_arrays
      assert_equal [1.0], @ctx.call_prop('id', [1])
      assert_equal [['foo', [1.0]]], @ctx.call_prop('id', [['foo', [1]]])
    end

    def test_hashes
      assert_equal({'hello' => 123}, @ctx.call_prop('id', {'hello' => 123}))
      assert_equal({'hello' => [{'foo' => 123}]}, @ctx.call_prop('id', {'hello' => [{'foo' => 123}]}))
    end

    def test_hashes_with_symbol_key
      assert_equal({'hello' => 123}, @ctx.call_prop('id', {:hello => 123}))
    end

    def test_hashes_with_complex_entries
      assert_raises TypeError do
        @ctx.call_prop('id', {:hello => Object.new})
      end

      assert_raises TypeError do
        @ctx.call_prop('id', {123 => "hello"})
      end
    end

    def test_hashes_with_complex_values
      res = @ctx.eval_string('({a:1,b:function(){}})')
      assert_equal({'a' => 1}, res)
    end

    def test_objects_with_prototypes
      res = @ctx.eval_string <<-JS
      function A() {
        this.value = 123;
        this.fn = function() {};
      }
      A.prototype.b = 456;
      A.prototype.c = function() {};
      new A;
      JS

      assert_equal({'value' => 123}, res)
    end

    def test_binding
      @ctx.eval_string <<-JS
        var self = this
        function test() { return this === self }
      JS
      assert_equal true, @ctx.call_prop('test')
    end

    def test_nested_property
      @ctx.eval_string <<-JS
        a = {}
        a.b = {}
        a.b.id = function(v) { return v; }
      JS
      assert_equal 'Hei', @ctx.call_prop(['a', 'b', 'id'], 'Hei')
    end

    def test_nested_binding
      @ctx.eval_string <<-JS
        a = {}
        a.b = {}
        a.b.test = function() { return this == a.b; }
      JS
      assert_equal true, @ctx.call_prop(['a', 'b', 'test'])
    end

    def test_throw_error
      @ctx.eval_string('function fail(msg) { throw new Error(msg) }')

      err = assert_raises(Duktape::Error) do
        @ctx.call_prop('fail', 'boom')
      end

      assert_equal "boom", err.message
    end

    def test_reference_error
      err = assert_raises(Duktape::ReferenceError) do
        @ctx.call_prop('missing')
      end

      assert_equal "identifier 'missing' undefined", err.message
    end

    def test_nested_reference_error
      err = assert_raises(Duktape::ReferenceError) do
        @ctx.call_prop(['missing', 'foo'])
      end

      assert_equal "identifier 'missing' undefined", err.message
    end

    def test_nested_type_error
      @ctx.eval_string 'a = {}'

      err = assert_raises(Duktape::TypeError) do
        @ctx.call_prop(['a', 'missing'])
      end

      assert_equal 'undefined not callable', err.message
    end

    def test_unknown_argument_type
      err = assert_raises(TypeError) do
        @ctx.call_prop('id', Object.new)
      end
      assert_match /Object/, err.message

      assert_raises(TypeError) do
        @ctx.call_prop('id', [Object.new])
      end

      assert_raises(TypeError) do
        @ctx.call_prop('id', {123 => Object.new})
      end

      assert_raises(TypeError) do
        @ctx.call_prop('id', {'key' => Object.new})
      end
    end
  end

  describe "#call_iife" do
    def test_iife
      assert_equal 'Hello world', @ctx.call_iife('(function(a) { return "Hello " + a })', 'world')
    end

    def test_syntax_error
      assert_raises(Duktape::SyntaxError) do
        @ctx.call_iife('(')
      end
    end

    def test_not_a_function
      assert_raises(Duktape::TypeError) do
        @ctx.call_iife('123')
      end
    end

    def test_invalid_argument
      assert_raises(TypeError) do
        @ctx.call_iife(123)
      end

      assert_raises(TypeError) do
        @ctx.call_iife(nil)
      end
    end
  end

  describe "#define_function" do
    def test_require_name
      err = assert_raises(ArgumentError) do
        @ctx.define_function
      end
    end

    def test_require_block
      err = assert_raises(ArgumentError) do
        @ctx.define_function("hello")
      end
    end

    def test_initial_definition
      @ctx.define_function "hello" do |a, b|
        a + b
      end
      assert_equal 3, @ctx.eval_string("hello(1, 2)", __FILE__)
    end

    def test_without_arguments
      @ctx.define_function("hello") { 'hello' }
      assert_equal "hello world", @ctx.eval_string("hello() + ' world'", __FILE__)
    end

    def test_with_arguments
      @ctx.define_function("square") { |x| x * x }
      assert_equal 4, @ctx.eval_string("square(2)", __FILE__)
    end

    def test_with_2_functions
      @ctx.define_function("square") { |x| x * x }
      @ctx.define_function("inc") { |x| x + 1 }
      assert_equal 5, @ctx.eval_string("inc(square(2))", __FILE__)
    end

    def test_call_prop
      @ctx.define_function("square") { |x| x * x }
      @ctx.define_function("inc") { |x| x + 1 }
      @ctx.eval_string('function math(a) { return square(inc(a)) }')
      assert_equal 9, @ctx.call_prop('math', 2)
    end

    def test_respects_complex_object
      val = :default
      @ctx.define_function("t") { |v| val = v; nil }
      @ctx.exec_string("t(function() { })", __FILE__)

      assert_equal @ctx.complex_object, val
    end

    def test_respects_custom_complex_object
      @ctx = Duktape::Context.new(complex_object: nil)

      val = :default
      @ctx.define_function("t") { |v| val = v; nil }
      @ctx.exec_string("t(function() { })", __FILE__)

      assert_nil val
    end

    def test_is_safe
      @ctx.define_function("id") { |x| x }

      res = @ctx.eval_string <<-EOF, __FILE__
        id.state = null
        id.block = null
        id("123")
      EOF

      assert_equal "123", res
    end

    def test_readme_example
      @ctx.define_function("leftpad") do |str, n, ch=' '|
        str.rjust(n, ch)
      end

      assert_equal "  foo", @ctx.eval_string("leftpad('foo', 5)")
      assert_equal "foobar", @ctx.eval_string("leftpad('foobar', 6)")
      assert_equal "01", @ctx.eval_string("leftpad('1', 2, '0')")
    end
  end


  describe "string encoding" do
    before do
      @ctx.eval_string('function id(str) { return str }')
      @ctx.eval_string('function len(str) { return str.length }')
    end

    def test_string_utf8_encoding
      str = @ctx.eval_string('"foo"')
      assert_equal 'foo', str
      assert_equal Encoding::UTF_8, str.encoding
    end

    def test_arguments_are_transcoded_to_utf8
      # "foo" as UTF-16LE bytes
      str = "\x66\x00\x6f\x00\x6f\00".force_encoding(Encoding::UTF_16LE)
      str = @ctx.call_prop('id', str)
      assert_equal 'foo', str
      assert_equal Encoding::UTF_8, str.encoding
    end

    def test_surrogate_pairs
      # Smiling emoji
      str = "\u{1f604}".encode("UTF-8")
      assert_equal str, @ctx.call_prop('id', str)
      assert_equal 2, @ctx.call_prop('len', str)
      assert_equal str, @ctx.eval_string("'#{str}'")
      assert_equal 2, @ctx.eval_string("'#{str}'.length")

      # US flag emoji
      str = "\u{1f1fa}\u{1f1f8}".force_encoding("UTF-8")
      assert_equal str, @ctx.call_prop('id', str)
      assert_equal 4, @ctx.call_prop('len', str)
      assert_equal str, @ctx.eval_string("'#{str}'")
      assert_equal 4, @ctx.eval_string("'#{str}'.length")
    end

    def test_invalid_input_data
      str = "\xde\xad\xbe\xef".force_encoding('UTF-8')
      assert_raises(EncodingError) do
        assert_equal str, @ctx.call_prop('id', str)
      end
    end

    def test_valid_output_data
      ranges = [
        (0x0000..0xD7FF),
        (0xE000..0xFFFF),
        (0x010000..0x10FFFF)
      ]

      # Pick some code points
      challenge = []
      n = 10000
      n.times do
        challenge << rand(ranges.sample)
      end

      str = @ctx.eval_string(<<-JS)
        var res = [];
        var codepoints = #{challenge.inspect};
        for (var i = 0; i < codepoints.length; i++) {
          var codepoint = codepoints[i];
          if (codepoint > 0xFFFF) {
            codepoint -= 0x10000;
            var highSurrogate = (codepoint >> 10) + 0xD800;
            var lowSurrogate = (codepoint % 0x400) + 0xDC00;
            res.push(String.fromCharCode(highSurrogate, lowSurrogate));
          } else {
            res.push(String.fromCharCode(codepoints[i]));
          }
        }
        res.join("")
      JS

      str.each_codepoint do |code|
        assert_equal challenge.shift, code
      end
    end

    def test_invalid_output_data
      assert_raises(EncodingError) do
        @ctx.eval_string(<<-JS)
          ({data:String.fromCharCode(0xD800 + 10)})
        JS
      end
    end
  end

  describe "ComplexObject instance" do
    def test_survives_bad_people
      Duktape::ComplexObject.instance_variable_set(:@instance, nil)
      # Generate some garbage
      100.times { Array.new(1000) { " " * 100 } }
      GC.start
      assert Duktape::ComplexObject.instance
    end
  end

  describe "custom ComplexObject" do
    def options
      super.merge(complex_object: false)
    end

    def test_complex_object
      assert_equal false, @ctx.eval_string("(function() {})")
    end

    def test_hash_complex_object
      assert_equal Hash.new, @ctx.eval_string("({foo:(function() {})})")
    end

    def test_array_complex_object
      assert_equal [1, false], @ctx.eval_string("[1, function() {}]")
    end

    def test_keeps_other
      assert_equal({'foo' => false}, @ctx.eval_string("({foo:false})"))
    end

    def test_maintains_reference
      @ctx = Duktape::Context.new(complex_object: "hello")
      # Generate some garbage
      100.times { Array.new(1000) { " " * 100 } }
      GC.start
      assert_equal "hello", @ctx.complex_object
    end
  end

  def test_default_stacktrace
    res = @ctx.eval_string <<-EOF
      function run() {
        try {
          throw new Error;
        } catch (err) {
          return err.stack.toString();
        }
      }

      run();
    EOF

    assert_includes res, "(duktape):3"
  end

  def test_filename_stacktrace
    res = @ctx.eval_string <<-EOF, __FILE__
      function run() {
        try {
          throw new Error;
        } catch (err) {
          return err.stack.toString();
        }
      }

      run();
    EOF

    assert_includes res, "#{__FILE__}:3"
  end

  describe "modules" do
    def test_required_undefined
      assert_equal 'undefined',
        @ctx.eval_string('typeof require')
    end

    def test_module_undefined
      assert_equal 'undefined',
        @ctx.eval_string('typeof module')
    end

    def test_exports_undefined
      assert_equal 'undefined',
        @ctx.eval_string('typeof exports')
    end
  end

  describe "fatal handler" do
    def test_invoke_fatal
      @require_valid = false

      @ctx.define_function("fatal") { @ctx._invoke_fatal }

      assert_raises(Duktape::InternalError) do
        @ctx.exec_string("fatal()")
      end

      assert_raises(Duktape::InternalError) do
        @ctx.exec_string("1 + 1")
      end
    end
  end

  ## Previous bugs in Duktape
  describe "previous bugs" do
    def test_tailcall_bug
      # Tail calls sometimes messes up the parent frame
      res = @ctx.eval_string <<-EOF
        var reduce = function(obj, iterator, memo) {
          return obj.reduce(iterator, memo);
        };

        function replace(array, shallow) {
          return reduce(array, function(memo, value) {
            return memo.concat(shallow);
          }, []);
        }

        JSON.stringify(replace([1, 2], 1));
      EOF

      assert_equal "[1,1]", res
    end

    def test_bind_constructor
      # Function.prototype.bind doesn't create constructable functions
      res = @ctx.eval_string <<-EOF
        function Thing(value) {
          this.value = value;
        }

        one = Thing.bind(null, 1);
        var obj = new one;
        obj.value;
      EOF
    end
  end

  describe "popular libraries" do
    def test_babel
      assert source = File.read(File.expand_path("../fixtures/babel.js", __FILE__))

      @ctx.exec_string(source, "(execjs)")
      assert_equal 64, @ctx.call_prop(["babel", "eval"], "((x) => x * x)(8)")
    end

    def test_coffee_script
      assert source = File.read(File.expand_path("../fixtures/coffee-script.js", __FILE__))

      @ctx.exec_string(source, "(execjs)")
      assert_equal 64, @ctx.call_prop(["CoffeeScript", "eval"], "((x) -> x * x)(8)")
    end

    def test_uglify
      assert source = File.read(File.expand_path("../fixtures/uglify.js", __FILE__))

      @ctx.exec_string(source, "(execjs)")

      assert_equal "function foo(bar){return bar}",
        @ctx.call_prop(["uglify"], "function foo(bar) {\n  return bar;\n}")
    end

    def test_legacy_regex
      assert_equal true, @ctx.eval_string('/]/.test("[hello]")')
    end
  end
end
