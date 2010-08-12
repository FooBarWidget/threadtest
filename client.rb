#!/usr/bin/env ruby
require 'socket'

ITERATIONS = 40_000
CONNECT_PASSWORD = "1234"
REQUEST_DATA = "REQUEST_METHOD\0PING\0PATH_INFO\0/\0PASSENGER_CONNECT_PASSWORD\0#{CONNECT_PASSWORD}\0"
REQUEST = [REQUEST_DATA.size].pack('N') + REQUEST_DATA

start_time = Time.now
buffer = ''
ITERATIONS.times do
	socket = UNIXSocket.new(ARGV[0] || 'server.sock')
	begin
		socket.write(REQUEST)
		socket.close_write
		socket.read(nil, buffer)
	ensure
		socket.close
	end
end
end_time = Time.now

printf "%.1f req/sec\n", ITERATIONS / (end_time - start_time)
