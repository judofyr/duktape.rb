# Duktape.rb

Duktape.rb is a C extension for the [Duktape](http://duktape.org/) JavaScript
interpreter.

## Quickstart

```sh
$ rake
$ ruby example.rb
```

## Usage

```ruby
require 'duktape'

# Create a new context
ctx = Duktape::Context.new

## Evaluate a string
p ctx.eval_string('1 + 1')  # => 2
```

### Contexts

Creating a context creates a fresh evaluation environment with no global
variables or functions defined.

A common pattern is to create a new context, define static functions once, and
reuse the context to invoke the function many times with `call_prop`.

``` ruby
ctx = Duktape::Context.new

ctx.exec_string <<-JS
  function process(str, options) {
    // ...
  }
JS

ctx.call_prop('process', 'some data', a: 1, b: 2)
```

### Call APIs

* `exec_string` - Evaluate a JavaScript String on the context and return `nil`.
* `eval_string` - Evaluate a JavaScript String expression and return the result
                  as a Ruby Object.
- `get_prop`    - Access the property of the global object and return the value
                  as a Ruby Object.
- `call_prop`   - Call a defined function with the given parameters and return
                  the value as a Ruby Object.

### Defining functions

You can define simple functions in Ruby that can be called from
JavaScript:

```ruby
ctx.define_function("leftpad") do |str, n, ch=' '|
  str.rjust(n, ch)
end
```

### Exceptions

Executing JS may raise two classes of errors: `Duktape::Error` and
`Duktape::InternalError`.

Any JS runtime error that is thrown in the interpreter is converted to a Ruby
`Duktape::Error`. Specific error subclasses, such as `SyntaxError` and
`TypeError`, are mapped from JS to the Ruby equivalent of the same name.

``` ruby
ctx = Duktape::Context.new
ctx.exec_string <<JS
  (function() {
    throw new Error("fail");
  })();
JS
# raises Duktape::Error: fail
```

The second error hierarchy, `Duktape::InternalError`, is reserved for errors
in the Duktape interpreter itself. It may be an indication of a bug in this
library.
