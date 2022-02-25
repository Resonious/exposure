# Fasttrace

This gem spits out an execution trace of your Ruby program. You'll get a huge file with every line number and method call, as well as timing information.

The goal is to eventually use this for full-program visualization.

## Installation

Add this line to your application's Gemfile:

```ruby
gem 'fasttrace'
```

And then execute:

    $ bundle install

Or install it yourself as:

    $ gem install fasttrace

## Usage

```ruby
Fasttrace.start 'path/to/trace.out'
my_ruby_code
Fasttrace.stop
```

NOTE! Every time you run `Fasttrace.start`, the trace file will be truncated.

Also, the trace files can get massive. Be careful!

## Development

After checking out the repo, run `bin/setup` to install dependencies. Then, run `rake spec` to run the tests. You can also run `bin/console` for an interactive prompt that will allow you to experiment.

To install this gem onto your local machine, run `bundle exec rake install`. To release a new version, update the version number in `version.rb`, and then run `bundle exec rake release`, which will create a git tag for the version, push git commits and the created tag, and push the `.gem` file to [rubygems.org](https://rubygems.org).

## Contributing

Bug reports and pull requests are welcome on GitHub at https://github.com/Resonious/fasttrace.

## License

The gem is available as open source under the terms of the [MIT License](https://opensource.org/licenses/MIT).

This gem borrows a lot of code from the ruby-prof project, and thus we also include [ruby-prof's license](https://github.com/ruby-prof/ruby-prof/blob/b235b8784834f6bcc78d73b9c0cbad8a92b54811/LICENSE).
