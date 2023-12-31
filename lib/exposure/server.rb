require 'socket'
require 'digest/sha1'
require 'base64'

require_relative 'websocket'

module Exposure
  # This is the server's main loop. Runs in a Ractor.
  SERVER_RACTOR_BODY = proc do
    compute_websocket_accept = lambda do |sec_websocket_key|
      guid = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
      accept_key = sec_websocket_key + guid
      sha1_result = Digest::SHA1.digest(accept_key)
      Base64.encode64(sha1_result).strip
    end

    bind_addr, bind_port = Ractor.receive
    listener = TCPServer.new(bind_addr, bind_port)

    address = listener.addr

    puts "Exposure listening on #{address[2]}:#{address[1]}"

    sockets = [listener]
    clients = {}

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

          elsif data_lowercase.start_with?('sec-websocket-version: ')
            handshakes[socket][:websocket_version] = data.split(':').last.strip.to_i

          elsif data.empty? && handshakes.dig(socket, :wants_websocket)
            clients[socket] = handshakes[socket]
            Ractor.yield :connected

            response = [
              'HTTP/1.1 101 Switching Protocols',
              'Upgrade: websocket',
              'Connection: upgrade',
            ]
            if (key = handshakes.dig(socket, :websocket_key))
              response << "Sec-WebSocket-Accept: #{compute_websocket_accept.call key}"
            end

            socket.write response.join("\r\n")
            2.times { socket.write "\r\n" }
            socket.flush
          end
        end
      end
    end

    # Now loop forever ... (until client closes I guess? then maybe loop BACK to the top?)
    loop do
      data = Ractor.receive

      case data
      when :close
        puts 'Closing Exposure'
        sockets.each do |socket|
          socket.close
        end
        break
      end

      puts "FORWARDING: #{data.class} #{data.each_byte.map { |b| b.to_i.to_s(16) }.join}" # TODO obviously bad. send to websocket connection in real
      clients.each do |client, handshake|
        version = handshake.fetch(:websocket_version, 13)
        frame = WebSocket::Frame::Outgoing::Server.new(version:, data:, type: :binary)
        puts "-> #{frame.to_s.each_byte.map { |b| b.to_i.to_s(16) }.join}"
        client.write(frame.to_s)
        # TODO: catch eof?
      end
    end

    :exited
  end

  class Server
    attr_reader :ractor

    def initialize(bind = "127.0.0.1", port = 9909)
      @ractor = Ractor.new(&SERVER_RACTOR_BODY)
      @ractor.send([bind, port], move: true)
    end

    # Gets called from the C extension when there's some data we want to send to the visualizer.
    def call(data)
      @ractor.send(data, move: true)
    end
  end
end
