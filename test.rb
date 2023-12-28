require_relative './lib/exposure.rb'

def method_one
  method_two
  method_two
end

def method_two
  puts 'hello'
end

Exposure.start

method_one
method_two

Exposure.stop