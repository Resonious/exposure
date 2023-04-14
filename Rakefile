# frozen_string_literal: true

require 'bundler/gem_tasks'
require 'rspec/core/rake_task'

RSpec::Core::RakeTask.new(:spec)

require 'rubocop/rake_task'

RuboCop::RakeTask.new

require 'rake/extensiontask'

task build: :compile

Rake::ExtensionTask.new('tracyrb') do |ext|
  ext.lib_dir = 'lib/tracyrb'
end

task default: %i[clobber compile spec rubocop]
