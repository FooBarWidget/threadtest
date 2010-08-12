#!/usr/bin/env ruby
require 'socket'
require 'thread'

CONNECT_PASSWORD = "1234"
RESPONSE = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nhello world\n"

def do_work
	100.times do
		1 + 2 * 3 / 4
	end
end

class SimpleRequestHandler
	def start
		File.unlink('server.sock') rescue nil
		server = UNIXServer.new('server.sock')
		server.listen(1024)
		begin
			main(server)
		ensure
			server.close
			File.unlink('server.sock') rescue nil
		end
	end
	
	def main(server)
		a, b = IO.pipe
		c, d = IO.pipe
		buffer = ''
		while true
			ios = select([server, a, b, c, d]).first
			client = ios[0].accept
			begin
				client.read(nil, buffer)
				do_work
				client.write(RESPONSE)
			ensure
				client.close
			end
		end
	end
end

# Queue with thread pool
class ThreadedRequestHandler < SimpleRequestHandler
	def initialize
		super
		thread_count  = 10
		@threads      = []
		@queue        = []
		@queue_mutex  = Mutex.new
		@queue_pushed = ConditionVariable.new
		@queue_popped = ConditionVariable.new
		@queue_limit  = thread_count * 5
		thread_count.times do
			@threads << Thread.new(&method(:thread_main))
		end
	end
	
	def thread_main
		buffer  = ''
		queue = @queue
		queue_mutex = @queue_mutex
		queue_pushed = @queue_pushed
		queue_popped = @queue_popped
		while true
			client = nil
			queue_mutex.synchronize do
				while queue.size == 0
					queue_pushed.wait(queue_mutex)
				end
				client = queue.shift
				queue_popped.signal
			end
			begin
				client.read(nil, buffer)
				do_work
				client.write(RESPONSE)
			ensure
				client.close
			end
		end
	end
	
	def main(server)
		a, b = IO.pipe
		c, d = IO.pipe
		queue = @queue
		queue_mutex = @queue_mutex
		queue_pushed = @queue_pushed
		queue_popped = @queue_popped
		queue_limit  = @queue_limit
		thread_size_multiple = @threads.size * 2
		while true
			ios = select([server, a, b, c, d]).first
			queue_mutex.synchronize do
				while queue.size >= queue_limit
					queue_popped.wait(queue_mutex)
				end
				
				accept_count = queue.size + thread_size_multiple
				accept_count = queue_limit if accept_count > queue_limit
				begin
					while queue.size < accept_count
						queue.push(ios[0].accept_nonblock)
					end
				rescue Errno::EAGAIN
				end
				# 'signal' yields significantly better performance
				# on 1.8 even though it doesn't wake all threads
				queue_pushed.broadcast
			end
		end
	end
end

# Create new thread for every client
class ThreadedRequestHandler2 < SimpleRequestHandler
	def main(server)
		a, b = IO.pipe
		c, d = IO.pipe
		while true
			ios = select([server, a, b, c, d]).first
			client = ios[0].accept
			Thread.new do
				begin
					client.read
					do_work
					client.write(RESPONSE)
				ensure
					client.close
				end
			end
		end
	end
end

# Let threads do the select() and accept()
class ThreadedRequestHandler3 < SimpleRequestHandler
	def initialize
		super
		@thread_count  = 10
		@threads      = []
		@a, @b        = IO.pipe
		@jruby        = RUBY_PLATFORM == 'java'
	end
	
	def thread_main
		buffer = ''
		while true
			if @jruby
				client = @server.accept
			elsif true
				ios = select([@server, @a, @b]).first
				begin
					client = @server.accept_nonblock
				rescue Errno::EAGAIN
					# Noticably improves performance
					sleep rand(0.1)
					next
				end
			else
				ios = select([@server, @a, @b]).first
				fd = NativeSupport.accept(@server.fileno)
				if fd.nil?
					sleep rand(0.25)
					next
				else
					client = UNIXSocket.for_fd(fd)
				end
			end
			begin
				client.read(nil, buffer)
				do_work
				client.write(RESPONSE)
			ensure
				client.close
			end
		end
	end
	
	def main(server)
		@server = server
		@thread_count.times do
			@threads << Thread.new(&method(:thread_main))
		end
		sleep
	end
end

Thread.abort_on_exception = true
method = ARGV[0] ? ARGV[0].to_i : 1
case method
when 1
	require File.expand_path("#{ENV['HOME']}/Projects/passenger/lib/phusion_passenger")
	require 'phusion_passenger/rack/request_handler'
	a, b = IO.pipe
	app = lambda do |env|
		do_work
		[200, { "Content-Type" => "text/plain" }, [RESPONSE]]
	end
	handler = PhusionPassenger::Rack::RequestHandler.new(a, app, "connect_password" => CONNECT_PASSWORD)
	begin
		puts handler.server_sockets[:main][0]
		handler.main_loop
	ensure
		handler.cleanup
	end
when 2
	handler = SimpleRequestHandler.new
	handler.start
when 3
	handler = ThreadedRequestHandler.new
	handler.start
when 4
	handler = ThreadedRequestHandler2.new
	handler.start
when 5
	# baseline => 8500
	# accept_nonblock without exception => 8600
	handler = ThreadedRequestHandler3.new
	handler.start
end
# EventMachine: 8800
