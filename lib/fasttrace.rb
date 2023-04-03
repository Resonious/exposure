# frozen_string_literal: true

require_relative 'fasttrace/version'
require_relative 'fasttrace/fasttrace.so'

module Fasttrace
  class Error < StandardError; end

  def self.start(project_root = Dir.pwd)
    raise Error, 'Already started' if $fasttrace
    $fasttrace = Trace.new(project_root)
    $fasttrace.start
  end

  def self.stop
    $fasttrace.stop
    $fasttrace = nil
  end

  def self.frame(name, &block)
    $fasttrace.frame(name, &block)
  end
end
