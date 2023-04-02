# frozen_string_literal: true

require 'mkmf'

$CFLAGS += ' -DTRACY_ENABLE -DTRACY_FIBERS'
$CXXFLAGS += ' -DTRACY_ENABLE -DTRACY_FIBERS'

create_makefile('fasttrace/fasttrace')
