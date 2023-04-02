# frozen_string_literal: true

require 'mkmf'

$CFLAGS += ' -DTRACY_ENABLE'
$CXXFLAGS += ' -DTRACY_ENABLE'

create_makefile('fasttrace/fasttrace')
