# frozen_string_literal: true

require_relative 'exposure/version'
require_relative 'exposure/exposure.so'

module Exposure
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
    raise Error, 'Already started' if $_exposure
    if path_blocklist && !path_blocklist.is_a?(Array)
      raise ArgumentError, 'path_blocklist must be Array'
    end

    FileUtils.mkdir_p(trace_dir)
    FileUtils.touch(File.join(trace_dir, 'exposure.returns'))
    FileUtils.touch(File.join(trace_dir, 'exposure.locals'))
    FileUtils.touch(File.join(trace_dir, 'exposure.blocks'))

    $_exposure = Trace.new(
      trace_dir,
      project_root ? project_root.to_s : nil,
      path_blocklist,
      track_block_receivers
    )
    $_exposure.tracepoint.enable
  end

  def self.stop
    return if $_exposure&.tracepoint.nil?
    $_exposure.tracepoint.disable
    $_exposure = nil
  end
end
