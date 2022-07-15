# frozen_string_literal: true

require_relative 'fasttrace/version'
require_relative 'fasttrace/fasttrace.so'

module Fasttrace
  class Error < StandardError; end

  # @param trace_dir [String] where to store trace files
  # @param project_root [String] optional root of project. some things won't be tracked outside of project
  # @param path_blocklist [Array<String>] any paths that contain one of these strings will not be checked
  # @param track_block_receivers [Boolean] whether to record the type of block receivers
  def self.start(
    trace_dir = '.exposure',
    project_root: nil,
    path_blocklist: nil,
    track_block_receivers: false
  )
    raise Error, 'Already started' if $fasttrace
    if path_blocklist && !path_blocklist.is_a?(Array)
      raise ArgumentError, 'path_blocklist must be Array'
    end

    $fasttrace = Trace.new(
      trace_dir,
      project_root ? project_root.to_s : nil,
      path_blocklist,
      track_block_receivers
    )
    $fasttrace.tracepoint.enable
  end

  def self.stop
    $fasttrace.tracepoint.disable
    $fasttrace = nil
  end
end
