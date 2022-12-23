# Exposure

This is a Ruby gem that analyzes a running Ruby program to extract type info. The type info is saved to a very fast (and disk space-hungry) on-file hashmap that can be read in real-time by a special Solargraph fork. The result: accurate type info in your editor!

## Installation

Add this line to your application's Gemfile:

```ruby
gem 'exposure'
```

And then execute:

    $ bundle install

Or install it yourself as:

    $ gem install exposure

## Usage

```ruby
Exposure.start
my_ruby_code
Exposure.stop
```

Type info for methods, local variables, and blocks will be saved in a folder called `.exposure`. These files get _very large_ very fast, as the data format is heavily optimized for quick read/write (and simplicity).

## Development

After checking out the repo, run `bin/setup` to install dependencies. Then, run `rake spec` to run the tests. You can also run `bin/console` for an interactive prompt that will allow you to experiment.

To install this gem onto your local machine, run `bundle exec rake install`. To release a new version, update the version number in `version.rb`, and then run `bundle exec rake release`, which will create a git tag for the version, push git commits and the created tag, and push the `.gem` file to [rubygems.org](https://rubygems.org).

## Contributing

Bug reports and pull requests are welcome on GitHub at https://github.com/Resonious/exposure.

## License

The gem is available as open source under the terms of the [MIT License](https://opensource.org/licenses/MIT).

This gem borrows a lot of code from the ruby-prof project, and thus we also include [ruby-prof's license](https://github.com/ruby-prof/ruby-prof/blob/b235b8784834f6bcc78d73b9c0cbad8a92b54811/LICENSE).
