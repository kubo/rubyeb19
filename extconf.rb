require 'mkmf'

have_library("z")
have_library("intl") if /freebsd/ =~ RUBY_PLATFORM
have_library('eb')
have_func("rb_block_proc")
have_func("eb_bitmap_to_png")
have_header('eb/sysdefs.h')                                                    

# uncomment the following line if you use eb-4.0beta* with pthread support.
# $defs << '-DEBCONF_ENABLE_PTHREAD'

create_makefile("eb")
