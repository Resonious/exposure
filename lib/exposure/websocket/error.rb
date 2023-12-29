# frozen_string_literal: true

module Exposure
  module WebSocket
    class Error < RuntimeError
      class Frame < ::Exposure::WebSocket::Error
        class ControlFramePayloadTooLong < ::Exposure::WebSocket::Error::Frame
          def message
            :control_frame_payload_too_long
          end
        end

        class DataFrameInsteadContinuation < ::Exposure::WebSocket::Error::Frame
          def message
            :data_frame_instead_continuation
          end
        end

        class FragmentedControlFrame < ::Exposure::WebSocket::Error::Frame
          def message
            :fragmented_control_frame
          end
        end

        class Invalid < ::Exposure::WebSocket::Error::Frame
          def message
            :invalid_frame
          end
        end

        class InvalidPayloadEncoding < ::Exposure::WebSocket::Error::Frame
          def message
            :invalid_payload_encoding
          end
        end

        class MaskTooShort < ::Exposure::WebSocket::Error::Frame
          def message
            :mask_is_too_short
          end
        end

        class ReservedBitUsed < ::Exposure::WebSocket::Error::Frame
          def message
            :reserved_bit_used
          end
        end

        class TooLong < ::Exposure::WebSocket::Error::Frame
          def message
            :frame_too_long
          end
        end

        class UnexpectedContinuationFrame < ::Exposure::WebSocket::Error::Frame
          def message
            :unexpected_continuation_frame
          end
        end

        class UnknownFrameType < ::Exposure::WebSocket::Error::Frame
          def message
            :unknown_frame_type
          end
        end

        class UnknownOpcode < ::Exposure::WebSocket::Error::Frame
          def message
            :unknown_opcode
          end
        end

        class UnknownCloseCode < ::Exposure::WebSocket::Error::Frame
          def message
            :unknown_close_code
          end
        end

        class UnknownVersion < ::Exposure::WebSocket::Error::Frame
          def message
            :unknown_protocol_version
          end
        end
      end

      class Handshake < ::Exposure::WebSocket::Error
        class GetRequestRequired < ::Exposure::WebSocket::Error::Handshake
          def message
            :get_request_required
          end
        end

        class InvalidAuthentication < ::Exposure::WebSocket::Error::Handshake
          def message
            :invalid_handshake_authentication
          end
        end

        class InvalidHeader < ::Exposure::WebSocket::Error::Handshake
          def message
            :invalid_header
          end
        end

        class UnsupportedProtocol < ::Exposure::WebSocket::Error::Handshake
          def message
            :unsupported_protocol
          end
        end

        class InvalidStatusCode < ::Exposure::WebSocket::Error::Handshake
          def message
            :invalid_status_code
          end
        end

        class NoHostProvided < ::Exposure::WebSocket::Error::Handshake
          def message
            :no_host_provided
          end
        end

        class UnknownVersion < ::Exposure::WebSocket::Error::Handshake
          def message
            :unknown_protocol_version
          end
        end
      end
    end
  end
end
