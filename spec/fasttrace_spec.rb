# frozen_string_literal: true

RSpec.describe Fasttrace do
  it "has a version number" do
    expect(Fasttrace::VERSION).not_to be nil
  end

  it "does something useful" do
    expect(Fasttrace::Trace).to be_a Class
    expect(Fasttrace::Trace.new.start('test test')).to eq nil
  end
end
