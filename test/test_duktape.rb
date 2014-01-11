root = File.expand_path('../..', __FILE__)
$LOAD_PATH << (root + '/lib') << (root + '/ext/duktape')

require 'minitest'
require 'minitest/autorun'
require 'duktape'

class TestDuktape < Minitest::Test
  def setup
    @ctx = Duktape::Context.new
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

  def test_wrapped
    @ctx.eval_string <<-EOF, __FILE__
      function run(cb) {
        try {
          cb();
          return true;
        } catch (err) {
          return err.toString();
        }
      }
    EOF

    assert_equal true, @ctx.wrapped_eval_string('run', '1 == 1', __FILE__)
    assert_equal '123', @ctx.wrapped_eval_string('run', 'throw 123', __FILE__)
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
end
