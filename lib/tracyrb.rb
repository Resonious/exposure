# frozen_string_literal: true

require_relative 'tracyrb/version'
require_relative 'tracyrb/tracyrb.so'

module Tracyrb
  class Error < StandardError; end

  def self.start(project_root = Dir.pwd)
    raise Error, 'Already started' if $tracyrb
    $tracyrb = Trace.new(project_root)
    $tracyrb.start
  end

  def self.stop
    $tracyrb.stop
    $tracyrb = nil
  end

  def self.frame(name, &block)
    $tracyrb.frame(name, &block)
  end
end
