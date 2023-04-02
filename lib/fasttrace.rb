# frozen_string_literal: true

require_relative 'fasttrace/version'
require_relative 'fasttrace/fasttrace.so'

module Fasttrace
  class Error < StandardError; end

  def self.start
    raise Error, 'Already started' if $fasttrace
    $fasttrace = Trace.new
    $fasttrace.start
  end

  def self.stop
    $fasttrace.stop
    $fasttrace = nil
  end
end
