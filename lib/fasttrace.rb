# frozen_string_literal: true

require_relative 'fasttrace/version'
require_relative 'fasttrace/fasttrace.so'

module Fasttrace
  class Error < StandardError; end

  def self.start(trace_dir = '.exposure', project_root: nil, track_block_receivers: false)
    raise Error, 'Already started' if $fasttrace

    $fasttrace = Trace.new(
      trace_dir,
      project_root ? project_root.to_s : nil,
      track_block_receivers
    )
    $fasttrace.tracepoint.enable
  end

  def self.stop
    $fasttrace.tracepoint.disable
    $fasttrace = nil
  end
end
