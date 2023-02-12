# ChangeLog

## v2.7.0.0 (2023-02-12)

* Upgrade to Duktape v2.7.0

## v2.6.0.0 (2021-01-31)

* Upgrade to Duktape v2.6.0

## v2.3.0.0 (2019-05-15)

* Upgrade to Duktape v2.3.0

## v2.0.1.1 (2019-05-13)

* Support passing big numbers (64-bit + bignum) to JS (#43, did, judofyr)

## v2.0.1.0 (2018-05-21)

* Upgrade to Duktape v2.0.1

## v1.6.1.0 (2017-02-11)

* Upgrade to Duktape v1.6.1 (did)

## v1.3.0.6 (2016-03-27)

* Implement `#define_function` (#16, judofyr, did)

## v1.3.0.5 (2016-01-22)

* Convert symbols to strings (judofyr, Jean-Baptiste Aviat)

## v1.3.0.4 (2015-11-23)

* Support Illumos and Solaris (#33, F4S4K4N)

## v1.3.0.3 (2015-09-25)

* Support for big-endian platforms (#30, nanaya)

## v1.3.0.2 (2015-09-25)

* Include all required files in the gem (#29, dapi)

## v1.3.0.1 (2015-09-24)

* Actually make the gem compile

## v1.3.0.0 (2015-09-24)

* Upgrade to Duktape v1.3.0

## v1.2.1.0 (2015-04-12)

* Upgrade to Duktape v1.2.1 (josh)

## v1.1.2.0 (2015-04-06)

* Upgrade to Duktape v1.1.2 (josh)
* Bundle duktape.{c,h} in the Git repo (josh)
* Add more documentation (josh)
* Add default filename (josh)
* Support encodings properly

## v1.0.2.1 (never released)

* Add error subclasses for the various Duktape errors
* Return instance of ComplexObject for complex objects
* Arrays and objects can now be returned from JavaScript context
* Arrays and hashes can now be passes into JavaScript context
* `call_prop` and `get_prop` now allows nested access
* Transcode strings to UTF-8

## v1.0.2.0 (2014-12-06)

* Upgraded Duktape to version 2.0.1
* ContextError now includes the error message from Duktape
* README and CHANGELOG is now included in gem
