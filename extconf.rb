require 'mkmf'

have_library("z")
have_library("intl") if /freebsd/ =~ RUBY_PLATFORM
have_library('eb')
have_func("rb_block_proc")
have_func("eb_bitmap_to_png")
have_header('eb/sysdefs.h')

if have_func("eb_pthread_enabled")
  print "checking that the EB library is pthread enabled... "
  STDOUT.flush
  if try_run(<<EOS)
#include <eb/eb.h>

int main()
{
    printf("eb_pthread_enabled() => %d\\n", eb_pthread_enabled());
    return eb_pthread_enabled() ? 0 : 1;
}
EOS
    puts "yes"
    $defs << '-DRUBY_EB_ENABLE_PTHREAD'
  else
    puts "no"
  end
end

create_makefile("eb")
