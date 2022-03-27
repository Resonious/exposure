# frozen_string_literal: true

require_relative 'fasttrace/version'
require_relative 'fasttrace/fasttrace.so'

module Fasttrace
  class Error < StandardError; end

  def self.start(trace_file = 'tmp/trace.out', project_root: nil)
    raise Error, 'Already started' if $fasttrace

    $fasttrace = Trace.new(trace_file, project_root)
    $fasttrace.tracepoint.enable
  end

  def self.stop
    $fasttrace.tracepoint.disable
    $fasttrace = nil
  end
end
