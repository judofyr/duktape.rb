root = File.expand_path('../..', __FILE__)
$LOAD_PATH << (root + '/lib') << (root + '/ext/duktape')

require 'minitest'
require 'minitest/autorun'
require 'duktape'

class TestDuktape < Minitest::Spec
  def setup
    @ctx = Duktape::Context.new
  end

  def teardown
    assert @ctx._valid?, "Context is in a weird state"
  end

  describe "#eval_string" do
    def test_requires_string
      assert_raises(TypeError) do
        @ctx.eval_string(123, __FILE__)
      end
    end

    def test_works_with_to_str
      a = Object.new
      def a.to_str
        '"123"'
      end

      assert_equal '123', @ctx.eval_string(a, __FILE__)
    end

    def test_string
      assert_equal '123', @ctx.eval_string('"123"', __FILE__)
    end

    def test_boolean
      assert_equal true,  @ctx.eval_string('1 == 1', __FILE__)
      assert_equal false, @ctx.eval_string('1 == 2', __FILE__)
    end

    def test_number
      assert_equal 123.0, @ctx.eval_string('123', __FILE__)
    end

    def test_nil_undef
      assert_nil @ctx.eval_string('null', __FILE__)
      assert_nil @ctx.eval_string('undefined', __FILE__)
    end

    def test_array
      assert_equal [1, 2, 3], @ctx.eval_string('[1, 2, 3]', __FILE__)
      assert_equal [1, [2, 3]], @ctx.eval_string('[1, [2, 3]]', __FILE__)
      assert_equal [1, [2, [3]]], @ctx.eval_string('[1, [2, [3]]]', __FILE__)
    end

    def test_object
      assert_equal({ "a" => 1, "b" => 2 }, @ctx.eval_string('({a: 1, b: 2})', __FILE__))
      assert_equal({ "a" => 1, "b" => [2] }, @ctx.eval_string('({a: 1, b: [2]})', __FILE__))
      assert_equal({ "a" => 1, "b" => { "c" => 2 } }, @ctx.eval_string('({a: 1, b: {c: 2}})', __FILE__))
    end

    def test_complex_object
      assert_equal Duktape::ComplexObject.instance,
        @ctx.eval_string('a = function() {}', __FILE__)
    end

    def test_throw_error
      err = assert_raises(Duktape::Error) do
        @ctx.eval_string('throw new Error("boom")', __FILE__)
      end

      assert_equal "boom", err.message
    end

    def test_reference_error
      err = assert_raises(Duktape::ReferenceError) do
        @ctx.eval_string('fail', __FILE__)
      end

      assert_equal "identifier 'fail' undefined", err.message
    end

    def test_syntax_error
      err = assert_raises(Duktape::SyntaxError) do
        @ctx.eval_string('{', __FILE__)
      end

      assert_equal "parse error (line 1)", err.message
    end

    def test_type_error
      err = assert_raises(Duktape::TypeError) do
        @ctx.eval_string('null.fail', __FILE__)
      end

      assert_equal "invalid base value", err.message
    end
  end

  describe "#exec_string" do
    def test_basic
      @ctx.exec_string('a = 1', __FILE__)
      assert_equal 1.0, @ctx.eval_string('a', __FILE__)
    end

    def test_doesnt_try_convert
      @ctx.exec_string('a = {b:1}', __FILE__)
      assert_equal 1.0, @ctx.eval_string('a.b', __FILE__)
    end

    def test_throw_error
      err = assert_raises(Duktape::Error) do
        @ctx.exec_string('throw new Error("boom")', __FILE__)
      end

      assert_equal "boom", err.message
    end

    def test_reference_error
      err = assert_raises(Duktape::ReferenceError) do
        @ctx.exec_string('fail', __FILE__)
      end

      assert_equal "identifier 'fail' undefined", err.message
    end

    def test_syntax_error
      err = assert_raises(Duktape::SyntaxError) do
        @ctx.exec_string('{', __FILE__)
      end

      assert_equal "parse error (line 1)", err.message
    end

    def test_type_error
      err = assert_raises(Duktape::TypeError) do
        @ctx.exec_string('null.fail', __FILE__)
      end

      assert_equal 'invalid base value', err.message
    end
  end

  describe "#get_prop" do
    def test_basic
      @ctx.eval_string('a = 1', __FILE__)
      assert_equal 1.0, @ctx.get_prop('a')
    end

    def test_nested
      @ctx.eval_string('a = {}; a.b = {}; a.b.c = 1', __FILE__)
      assert_equal 1.0, @ctx.get_prop(['a', 'b', 'c'])
    end

    def test_nested_undefined
      @ctx.eval_string('a = {}', __FILE__)
      assert_equal nil, @ctx.get_prop(['a', 'missing'])
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
      @ctx.eval_string('a = {};', __FILE__)

      err = assert_raises(Duktape::TypeError) do
        @ctx.get_prop(['a', 'b', 'c'])
      end

      assert_equal 'invalid base value', err.message
    end
  end

  describe "#call_prop" do
    before do
      @ctx.eval_string('function id(a) { return a }', __FILE__)
    end

    def test_str
      assert_equal 'Hei', @ctx.call_prop('id', 'Hei')
    end

    def test_fixnum
      assert_equal 2.0, @ctx.call_prop('id', 2)
    end

    def test_float
      assert_equal 2.0, @ctx.call_prop('id', 2.0)
    end

    def test_true
      assert_equal true, @ctx.call_prop('id', true)
    end

    def test_false
      assert_equal false, @ctx.call_prop('id', false)
    end

    def test_nil
      assert_equal nil, @ctx.call_prop('id', nil)
    end

    def test_arrays
      assert_equal [1.0], @ctx.call_prop('id', [1])
      assert_equal [['foo', [1.0]]], @ctx.call_prop('id', [['foo', [1]]])
    end

    def test_hashes
      assert_equal({'hello' => 123}, @ctx.call_prop('id', {'hello' => 123}))
      assert_equal({'hello' => [{'foo' => 123}]}, @ctx.call_prop('id', {'hello' => [{'foo' => 123}]}))
    end

    def test_hashes_with_complex_values
      res = @ctx.eval_string('({a:1,b:function(){}})', __FILE__)
      assert_equal({'a' => 1}, res)
    end

    def test_objects_with_prototypes
      res = @ctx.eval_string <<-JS, __FILE__
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
      @ctx.eval_string <<-JS, __FILE__
        var self = this
        function test() { return this === self }
      JS
      assert_equal true, @ctx.call_prop('test')
    end

    def test_nested_property
      @ctx.eval_string <<-JS, __FILE__
        a = {}
        a.b = {}
        a.b.id = function(v) { return v; }
      JS
      assert_equal 'Hei', @ctx.call_prop(['a', 'b', 'id'], 'Hei')
    end

    def test_nested_binding
      @ctx.eval_string <<-JS, __FILE__
        a = {}
        a.b = {}
        a.b.test = function() { return this == a.b; }
      JS
      assert_equal true, @ctx.call_prop(['a', 'b', 'test'])
    end

    def test_throw_error
      @ctx.eval_string('function fail(msg) { throw new Error(msg) }', __FILE__)

      err = assert_raises(Duktape::Error) do
        @ctx.call_prop('fail', 'boom', __FILE__)
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
      @ctx.eval_string 'a = {}', __FILE__

      err = assert_raises(Duktape::TypeError) do
        @ctx.call_prop(['a', 'missing'])
      end

      assert_equal 'not callable', err.message
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

  describe "string encoding" do
    before do
      @ctx.eval_string('function id(str) { return str }', __FILE__)
      @ctx.eval_string('function len(str) { return str.length }', __FILE__)
    end

    def test_string_utf8_encoding
      str = @ctx.eval_string('"foo"', __FILE__)
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
      assert_equal str, @ctx.eval_string("'#{str}'", __FILE__)
      assert_equal 2, @ctx.eval_string("'#{str}'.length", __FILE__)

      # US flag emoji
      str = "\u{1f1fa}\u{1f1f8}".force_encoding("UTF-8")
      assert_equal str, @ctx.call_prop('id', str)
      assert_equal 4, @ctx.call_prop('len', str)
      assert_equal str, @ctx.eval_string("'#{str}'", __FILE__)
      assert_equal 4, @ctx.eval_string("'#{str}'.length", __FILE__)
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

      str = @ctx.eval_string(<<-JS, __FILE__)
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
        @ctx.eval_string(<<-JS, __FILE__)
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

  def test_stacktrace
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
        @ctx.eval_string('typeof require', __FILE__)
    end

    def test_module_undefined
      assert_equal 'undefined',
        @ctx.eval_string('typeof module', __FILE__)
    end

    def test_exports_undefined
      assert_equal 'undefined',
        @ctx.eval_string('typeof exports', __FILE__)
    end
  end

  ## Previous bugs in Duktape
  describe "previous bugs" do
    def test_tailcall_bug
      # Tail calls sometimes messes up the parent frame
      res = @ctx.eval_string <<-EOF, __FILE__
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
      res = @ctx.eval_string <<-EOF, __FILE__
        function Thing(value) {
          this.value = value;
        }

        one = Thing.bind(null, 1);
        var obj = new one;
        obj.value;
      EOF
    end
  end
end
