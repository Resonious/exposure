# All of the following source files were copied from imanel/websocket-ruby
# https://github.com/imanel/websocket-ruby/tree/v1.2.10

module Exposure
  module WebSocket
    # Limit of frame size payload in bytes
    def self.max_frame_size
      20 * 1024 * 1024 # 20MB
    end

    # If set to true error will be raised instead of setting `error` method.
    # All errors inherit from WebSocket::Error.
    def self.should_raise
      false
    end
  end
end

require_relative 'websocket/error'
require_relative 'websocket/frame/data'
require_relative 'websocket/frame/base'
require_relative 'websocket/frame/handler/base'
require_relative 'websocket/frame/handler/handler03'
require_relative 'websocket/frame/handler/handler04'
require_relative 'websocket/frame/handler/handler05'
require_relative 'websocket/frame/handler/handler07'
require_relative 'websocket/frame/handler/handler75'
require_relative 'websocket/frame/outgoing'
require_relative 'websocket/frame/outgoing/server'

=begin
(The MIT License)

Copyright © 2012 Bernard Potocki

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ‘Software’), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED ‘AS IS’, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
=end
