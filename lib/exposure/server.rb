require 'socket'

module Exposure
  class Server
    attr_reader :ractor

    def initialize
      @ractor = Ractor.new do
        loop do
          buffer = Ractor.receive
          puts "RECEIVING: #{buffer.class} #{buffer.each_byte.map { |b| b.to_i.to_s(16) }.join}" # TODO obviously bad. send to websocket connection in real
        rescue => e
          puts "WARNING! Exposure server exiting due to #{e.class} #{e.message}"
        end
      end
    end

    # Gets called from the C extension when there's some data we want to send to the visualizer.
    def call(data)
      @ractor.send(data, move: true)
    end
  end
end
