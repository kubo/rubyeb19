#!/usr/bin/env ruby

require 'eb'

version = EB::RUBYEB_VERSION
dir = "rubyeb-#{version}"
files = %w( ChangeLog COPYING eb.c eb.html extconf.rb hook2.rb test.rb )

(cmds=<<EEOOSS).each do |cmd| system cmd end
  rm -rf #{dir} #{dir+".tar.gz "}
  mkdir #{dir}
  cp -p #{files.join(" ")} #{dir}
  tar zcvf #{dir+".tar.gz "} #{dir}
  rm -r #{dir}
EEOOSS
