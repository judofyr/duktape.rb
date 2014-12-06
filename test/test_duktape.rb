root = File.expand_path('../..', __FILE__)
$LOAD_PATH << (root + '/lib') << (root + '/ext/duktape')

require 'minitest'
require 'minitest/autorun'
require 'duktape'
require 'open-uri'

class TestDuktape < Minitest::Spec
  def setup
    @ctx = Duktape::Context.new
  end

  describe "#eval_string" do
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


    def test_complex_error
      assert_raises(Duktape::ContextError) do
        @ctx.eval_string('a = {b:1}', __FILE__)
      end
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
  end

  describe "#get_prop" do
    def test_basic
      @ctx.eval_string('a = 1', __FILE__)
      assert_equal 1.0, @ctx.get_prop('a')
    end

    def test_missing
      err = assert_raises(Duktape::ContextError) do
        @ctx.get_prop('a')
      end

      assert_equal 'no such prop: a', err.message
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

    def test_unknown
      err = assert_raises(Duktape::ContextError) do
        @ctx.call_prop('id', Object.new)
      end
      assert_match /Object/, err.message
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

    def test_coffeescript
      source = open("http://coffeescript.org/extras/coffee-script.js").read
      @ctx.eval_string(source, "coffee-script.js")
      @ctx.eval_string("CoffeeScript.compile('square = (x) -> x * x')")
    end
  end
end
