#!/usr/bin/env ruby -Ke
# -*- coding: euc-jp -*-
require "eb"

if defined? Encoding
  Encoding.default_internal = "UTF-8"
else
  if $KCODE!="EUC" then
    raise RuntimeError,"lib eb requires EUC coding system"
  end
end

b=EB::Book.new

WORD1="じしょ"
WORD2="ちしき"

print "\n****** Initial Test\n"
b.bind("/cdrom")
p b.disktype
p b.bound?
p b.path
p b.charcode
p b.subbook_count
p b.subbook_list

print "\n****** Sub Books\n"
0.upto(b.subbook_count-1) do |i|
  print "===============\n"
  p b.title(i), b.directory(i)
end

print "\n****** Sub Book Test\n"

b.subbook=0
p b.subbook

p b.search_available?
p b.exactsearch_available?
p b.endsearch_available?

print "\n****** Search Test\n"
items=b.exactsearch( WORD1 )
print "Hit :", items.size,"\n"
items.each do |item|
  print "====================\n"
  print "==   ",item[0],"\n"
  print "\n",item[1],"\n"
end

b.exactsearch( WORD2 ) do |item|
  print "====================\n"
  print "==   ",item[0],"\n"
  print "\n",item[1],"\n"
#  EB::Cancel   # yield will be stopped
end
