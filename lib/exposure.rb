# frozen_string_literal: true

require_relative 'exposure/version'
require_relative 'exposure/server'
require_relative 'exposure/exposure.so'

module Exposure
  class Error < StandardError; end

  # @param project_root [String] optional root of project. some things won't be tracked outside of project
  # @param path_blocklist [Array<String>] any paths that contain one of these strings will not be checked
  def self.start(
    project_root: nil,
    path_blocklist: nil
  )
    raise Error, 'Already started' if $_exposure_thread
    if path_blocklist && !path_blocklist.is_a?(Array)
      raise ArgumentError, 'path_blocklist must be Array'
    end

    server = Exposure::Server.new
    $_exposure_thread = Thread.new do
      # TODO: should wait, but for now uh
      $_exposure = Trace.new(
        project_root ? project_root.to_s : Dir.pwd,
        path_blocklist,
        server
      )
      $_exposure.tracepoint.enable

      loop do
        from_server = server.ractor.take
        puts "GOT FROM SERVER: #{from_server}"
      end
    end
  end

  def self.stop
    return if $_exposure_thread.nil?
    if $_exposure
      $_exposure.tracepoint.disable
      $_exposure = nil
    end
    $_exposure_thread&.kill
    $_exposure_thread = nil
  end
end
