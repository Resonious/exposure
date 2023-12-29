require_relative '../lib/exposure.rb'

class TestClass
  def maybe_leaf(doit)
    should_be_leaf if doit == false # silly but invokes a method
  end

  def should_be_leaf
    puts 'leafing'
  end

  def self.singleton_leaf
    puts 'singletone'
  end
end

Exposure.start project_root: __dir__
puts 'exposure started. press enter to continue'
gets

t = TestClass.new
t.maybe_leaf true
t.maybe_leaf false
TestClass.singleton_leaf

Exposure.stop

puts 'done. press enter to exit (wtf)'
gets
