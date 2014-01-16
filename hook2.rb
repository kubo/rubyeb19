#!/usr/bin/env ruby -Ke
require "eb"

eb=EB::Book.new
hkset = EB::Hookset.new
eb.bind("/cdrom")
eb.set(0)
eb.hookset=hkset
eb.fontcode=0
puts "Copyright:"
puts eb.copyright

hkset.register(EB::HOOK_BEGIN_REFERENCE) do |eb2,argv|
  "<#begin reference>"
end
hkset.register(EB::HOOK_END_REFERENCE) do |eb2,argv| 
  format('<#end reference page=%d offset=%d>',argv[1], argv[2])
end

hkset.register(EB::HOOK_NEWLINE) do |eb2,argv|
  "<BR>\n"
end

hkset.register(EB::HOOK_WIDE_FONT) do |eb2,argv|
  efont = eb2.get_widefont(argv[0])
  puts efont.to_xbm
  format("<extfont wide code=%d>",argv[0])
end

hkset.register(EB::HOOK_NARROW_FONT) do |eb2,argv|
  efont = eb2.get_narrowfont(argv[0])
  puts efont.to_xbm
  format("<extfont narrow code=%d>",argv[0])
end

hkset.register(EB::HOOK_BEGIN_COLOR_BMP) do |eb2,argv|
  page = argv[2]
  offset = argv[3]
  open("#{page}-#{offset}.bmp","w") do |f| 
    txt= eb2.read_colorgraphic(EB::Position.new(page,offset),0x40000)
    puts "binary size="<< format("%x",txt.size)
    f.write txt
  end
  format "<#begin bmp page=%d offset=%d obj=%s>",page,offset,argv.inspect
end

hkset.register(EB::HOOK_BEGIN_COLOR_JPEG) do |eb2,argv|
  "<#begin jpeg>"
end
hkset.register(EB::HOOK_END_COLOR_GRAPHIC) do |eb2,argv|
  format "<#end graphic obj=%s>",argv.inspect
end

puts "16dot ext-font size support? : #{ eb.fontcode_list.include?(EB::FONT_16)}"
puts "24dot ext-font size support? : #{ eb.fontcode_list.include?(EB::FONT_24)}"
puts "30dot ext-font size support? : #{ eb.fontcode_list.include?(EB::FONT_30)}"
puts "48dot ext-font size support? : #{ eb.fontcode_list.include?(EB::FONT_48)}"

puts "FontSize set to " << EB::FONT_16.to_s
eb.fontcode=EB::FONT_16
puts "Current Font:" << eb.fontcode.to_s
puts eb.wide_startcode
puts eb.wide_endcode
font= eb.get_widefont(eb.wide_startcode)
puts "Font code: #{font.code}"
puts "Font wide?:#{font.widefont?}"

p eb.content(EB::Position.new(1352,932))

eb.exactsearch2('うぐいす') do |pos,word|
p [pos,word]
  print "====================\n"
  print pos.page,":", pos.offset,"\n"
  print "==   ",word,"\n"
  print "\n",eb.content(pos),"\n"
  EB::Cancel   # yield will be stopped
end

print "END\n"

