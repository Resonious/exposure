# frozen_string_literal: true

require 'mkmf'

CONFIG['optflags'] = '-O0'
CONFIG['debugflags'] = '-ggdb3'

create_makefile('exposure/exposure')
