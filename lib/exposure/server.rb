require 'socket'
require 'websocket'

module Exposure
  class Server
    attr_reader :ractor

    def initialize(bind = "127.0.0.1", port = 9909)
      @ractor = Ractor.new do
        bind_addr, bind_port = Ractor.receive
        listener = TCPServer.new(bind_addr, bind_port)

        address = listener.addr

        puts "Exposure listening on #{address[2]}:#{address[1]}"

        sockets = [listener]
        clients = []

        # Mapping from socket to websocket headers
        handshakes = {}

        # First loop until we find a client
        while clients.empty?
          ready_sockets = select(sockets)
          next if ready_sockets.nil?

          ready_sockets[0].each do |socket|
            # New connection
            if socket == listener
              client = listener.accept
              sockets << client
              next
            end

            # Closed connection
            if socket.eof?
              puts "Exposure lost client connection"
              socket.close
              sockets.delete socket
              clients.delete socket
            # Received data
            else
              handshakes[socket] ||= {}
              data = socket.readline.chomp
              data_lowercase = data.downcase

              if data_lowercase == 'upgrade: websocket'
                handshakes[socket][:wants_websocket] = true

              elsif data_lowercase.start_with?('sec-websocket-key: ')
                handshakes[socket][:websocket_key] = data.split(':').last.strip

              elsif data.empty? && handshakes.dig(socket, :wants_websocket)
                clients << socket
                Ractor.yield :connected

                response = [
                  'HTTP/1.1 101 OK',
                  'Upgrade: websocket',
                  'Connection: upgrade',
                ]
                if (key = handshakes.dig(socket, :websocket_key))
                  response.push "Sec-WebSocket-Accept: #{key}"
                end
                response.push ''

                socket.write response.join("\n")
              end
            end
          end
        end

        # Now loop forever ... (until client closes I guess? then maybe loop BACK to the top?)
        loop do
          buffer = Ractor.receive

          case buffer
          when :close
            puts 'Closing Exposure'
            sockets.each do |socket|
              socket.close
            end
            break
          end

          puts "FORWARDING: #{buffer.class} #{buffer.each_byte.map { |b| b.to_i.to_s(16) }.join}" # TODO obviously bad. send to websocket connection in real
          clients.each do |client|
            client.write(buffer)
            client.flush
            # TODO: catch eof?
          end
        rescue => e
          puts "WARNING! Exposure server exiting due to #{e.class} #{e.message}"
        end

        :exited
      end

      @ractor.send([bind, port], move: true)
    end

    # Gets called from the C extension when there's some data we want to send to the visualizer.
    def call(data)
      @ractor.send(data, move: true)
    end
  end
end
