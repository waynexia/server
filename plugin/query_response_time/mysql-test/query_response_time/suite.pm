package My::Suite::Query_response_time;

@ISA = qw(My::Suite);

return "No QUERY_RESPONSE_TIME plugin" unless
  $ENV{QUERY_RESPONSE_TIME_SO} or
  $::mysqld_variables{'query_response_time'} eq "ON";

return "Not run for embedded server" if $::opt_embedded_server;

bless { };

