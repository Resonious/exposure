# frozen_string_literal: true

RSpec.describe Fasttrace do
  it 'has a version number' do
    expect(Fasttrace::VERSION).not_to be nil
  end

  it 'does something useful' do
    expect(Fasttrace::Trace).to be_a Class

    tp = Fasttrace::Trace.new('tmp/test.out')
    expect(tp).to be_a Fasttrace::Trace
    expect(tp.tracepoint).to be_a TracePoint
  end
end
