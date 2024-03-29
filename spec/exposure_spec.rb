# frozen_string_literal: true

require_relative './support/block_me'

module TestModule
  def on_module
    'module method'
  end
end

class TestBaseClass
  def on_base
    'base class instance method'
  end
end

class TestClass < TestBaseClass
  include TestModule

  attr_reader :block

  def initialize
    @block = lambda do |block_arg|
      "block(#{block_arg.inspect}) value"
    end
    super
  end

  def self.on_class
    'class method'
  end

  def on_instance
    'instance method'
  end
end

RSpec.describe Exposure do
  it 'has a version number' do
    expect(Exposure::VERSION).not_to be nil
  end

  it 'does something useful' do
    expect(Exposure::Trace).to be_a Class

    File.delete 'tmp/exposure.returns' rescue nil
    File.delete 'tmp/exposure.locals' rescue nil

    tp = Exposure::Trace.new(
      'tmp',
      (File.absolute_path File.join __dir__, '..'),
      ['support/block_me'],
      true
    )
    expect(tp).to be_a Exposure::Trace
    expect(tp.tracepoint).to be_a TracePoint

    tp.tracepoint.enable
    begin
      results = []
      test_class = TestClass.new

      def test_class.on_singleton
        'singleton method'
      end

      test_class.extend(
        Module.new do
          def on_anonymous_module
            'anonymous module method'
          end
        end
      )

      results << TestClass.on_class
      results << test_class.on_instance
      results << test_class.on_base
      results << test_class.on_module
      results << test_class.on_singleton
      results << test_class.on_anonymous_module
      results << test_class.block.call(self)
      'test'.instance_exec(1, &test_class.block) # <- "String" should show up in exposure.blocks as receiver
    ensure
      tp.tracepoint.disable
    end

    expect(results).to match_array [
      'class method',
      'instance method',
      'base class instance method',
      'module method',
      'singleton method',
      'anonymous module method',
      'block(#<RSpec::ExampleGroups::Exposure "does something useful" (./spec/exposure_spec.rb:43)>) value'
    ]
  end
end
