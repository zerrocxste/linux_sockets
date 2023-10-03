#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include <fstream>
#include <string>
#include <chrono>
#include <thread>

#include <liburing.h>

using namespace std::chrono_literals;

auto remove_file(const char* filename)
{
	std::remove(filename);
}

class write_fs
{
	std::fstream* _fs;
public:
	write_fs(const char* filename, std::ios_base::openmode mode)
	{
		_fs = new std::fstream(filename, mode | std::ios::out);
		_fs->seekp(0, std::ios::beg);
	}

	~write_fs()
	{
		delete this->_fs;
	}

	write_fs& write_string(const char* string, std::size_t str_length = 0)
	{
		this->_fs->write(string, str_length == 0 ? strlen(string) : str_length);
		return *this;
	}

	void close()
	{
		this->_fs->close();	
	}
};

template <class chrono_time_type>
auto ex_sleep(std::uint64_t time)
{
	auto timepoint = std::chrono::system_clock::now();
	while (std::chrono::duration_cast<chrono_time_type>(std::chrono::system_clock::now() - timepoint).count() < time);
}

class ip_sock {
	int sock;
public:
	ip_sock() { this->sock = socket(AF_INET, SOCK_STREAM, 0); }
	~ip_sock() { close(this->sock); }

	inline auto& get_sock() { return this->sock; }
	inline operator int() { return this->get_sock(); }
	inline operator bool() { return (bool)this->get_sock(); }
};

template <class type, std::size_t size>
class to_string
{
	type buf[size];
public:
	template <class... args>
	to_string(const type* s, args&&... arg) { sprintf(this->buf, s, arg...); }

	inline auto get() { return this->buf; }
	inline operator type*() { return this->get(); }
};

template <std::size_t size>
using to_ch_string = to_string<char, size>;

template <class rep, class per>
class c2kts
{
	__kernel_timespec _kts;
public:
	constexpr c2kts(const std::chrono::duration<rep, per>&& t) : _kts{}
	{
		auto sec = std::chrono::duration_cast<std::chrono::seconds>(t);
		_kts = { sec.count(), std::chrono::duration_cast<std::chrono::nanoseconds>(t - sec).count() };
	}

	auto* get_kts()
	{
		return &this->_kts;
	}
};
#define chrono_to_timespec(t) [](){ constexpr c2kts obj(t); return obj; }().get_kts()

int main(int argc, char** argv)
{
	std::printf("%s\n", argv[0]);

	auto port = 1337;

	if (argc <= 1)
		std::printf("argc <= 1, use default port\n");
	else
	{
		port = std::atoi(argv[1]);
	}
	
	to_ch_string<64 + 1> port_str("%d", port);

	ip_sock sock;

	if (!sock) {
		std::printf("socket return %d", sock);
		return 1;
	}
	
	sockaddr_in sockaddrin;
	sockaddrin.sin_port = htons(port);
	sockaddrin.sin_addr.s_addr = INADDR_ANY;
	sockaddrin.sin_family = AF_INET;

	auto bind_ret = bind(sock, (const sockaddr*)&sockaddrin, sizeof(decltype(sockaddrin)));
	if (bind_ret != 0)
	{
		std::printf("bind return %d\n", bind_ret);
		return 1;
	}

	auto listen_ret = listen(sock, 512);

	if (listen_ret != 0)
	{
		std::printf("listen return %d\n", listen_ret);
		return 1;
	}

	std::string output_filename = std::string(port_str.get()) + ".txt";

	remove_file(output_filename.c_str());

	io_uring ioring;
	auto io_uring_queue_init_ret = io_uring_queue_init(1024, &ioring, 0);

	if (io_uring_queue_init_ret < 0) {
		std::printf("io_uring_queue_init return %d\n", io_uring_queue_init_ret);
		return 1;
	}

	struct uring_sock_udata_t
	{
		enum user_command
		{
			ACCEPT,
			RECEIVE,
			SEND_TIMEOUT,
			SEND,
			MAX_SIZE_CMD
		} 
		_ucmd;
		int _sock;
		char* _received_data;
		std::chrono::system_clock::time_point _timestamp;

		uring_sock_udata_t(user_command ucmd, int sock, char* received_data = nullptr, std::chrono::system_clock::time_point _timestamp = {}) : 
			_ucmd(ucmd), _sock(sock), _received_data(received_data), _timestamp(_timestamp)
		{
			
		}
	};

	auto next_accept = [](io_uring& ioring, ip_sock& sock) -> void
	{
		auto sqe = io_uring_get_sqe(&ioring);

		static sockaddr_in sockaddrin_client{};
		static auto sockaddrin_client_length = sizeof(decltype(sockaddrin_client));
		io_uring_prep_accept(sqe, sock, (sockaddr*)&sockaddrin_client, (socklen_t*)&sockaddrin_client_length, 0);
		io_uring_sqe_set_data(sqe, new uring_sock_udata_t{ uring_sock_udata_t::ACCEPT, sock, nullptr, std::chrono::system_clock::now() });
		io_uring_submit(&ioring);
	};

	auto trigger_receive = [](io_uring& ioring, int sock) -> void
	{
		io_uring_sqe_set_data(io_uring_get_sqe(&ioring), new uring_sock_udata_t{ uring_sock_udata_t::RECEIVE, sock, nullptr, std::chrono::system_clock::now() });
		io_uring_submit(&ioring);
	};

	auto next_receive = [](io_uring& ioring, int sock) -> void
	{
		auto sqe = io_uring_get_sqe(&ioring);

		constexpr auto MAX_MESSAGE_LENGTH = 128;
		auto buffer = new (std::nothrow) char[MAX_MESSAGE_LENGTH + 1]();

		if (buffer == nullptr) {
			std::printf("Fatal error allocate\n");
			raise(SIGTRAP);
		}

		io_uring_prep_recv(sqe, sock, buffer, MAX_MESSAGE_LENGTH, 0);
		io_uring_sqe_set_data(sqe, new uring_sock_udata_t{ uring_sock_udata_t::RECEIVE, sock, buffer, std::chrono::system_clock::now() });
		io_uring_submit(&ioring);
	};

	auto next_send_timeout = [](io_uring& ioring, int sock) -> void
	{
		auto sqe = io_uring_get_sqe(&ioring);
		io_uring_prep_timeout(sqe, chrono_to_timespec(3s), 0, 0);
		io_uring_sqe_set_data(sqe, new uring_sock_udata_t{ uring_sock_udata_t::SEND_TIMEOUT, sock, nullptr, std::chrono::system_clock::now() });
		io_uring_submit(&ioring);
	};

	auto next_send = [](io_uring& ioring, int sock) -> void
	{
		auto sqe = io_uring_get_sqe(&ioring);	

		static char accepted_msg[] = "ACCEPTED";
		io_uring_prep_send(sqe, sock, accepted_msg, strlen(accepted_msg), 0);
		io_uring_sqe_set_data(sqe, new uring_sock_udata_t{ uring_sock_udata_t::SEND, sock, nullptr, std::chrono::system_clock::now() });
		io_uring_submit(&ioring);

		std::printf("Sended message \"ACCEPTED\"\n");
	};

	next_accept(ioring, sock);

	while (true) 
	{
		static io_uring_cqe *cqe_arr[256];
		std::memset(cqe_arr, 0, sizeof(cqe_arr));

		auto wait_cqe = io_uring_wait_cqe(&ioring, &cqe_arr[0]);

		auto cqe_num = io_uring_peek_batch_cqe(&ioring, cqe_arr, sizeof(cqe_arr) / sizeof(cqe_arr[0]));
		for (int i = 0; i < cqe_num; i++) 
		{
			auto cqe = cqe_arr[i];

			auto ud = (uring_sock_udata_t*)io_uring_cqe_get_data(cqe);

			switch (ud->_ucmd)
			{
				case uring_sock_udata_t::user_command::ACCEPT:
					next_accept(ioring, sock);
					trigger_receive(ioring, cqe->res);
					std::printf("new client\n");
					break;
				case uring_sock_udata_t::user_command::RECEIVE:
				{
					auto msg_from_client = ud->_received_data;

					if (msg_from_client == nullptr)
					{
						next_receive(ioring, ud->_sock);
						break;
					}

					if (msg_from_client && cqe->res <= 0)
					{
						std::printf("disconnected client\n");
						shutdown(ud->_sock, SHUT_RDWR);
						break;
					}		

					auto msg_len = strlen(msg_from_client);

					std::printf("Msg length: %d Msg: \"%s\"\n", msg_len, msg_from_client);

					write_fs(output_filename.c_str(), std::ios::app)
						.write_string(msg_from_client, msg_len)
						.write_string("\n")
						.close();

					delete msg_from_client;

					next_send_timeout(ioring, ud->_sock);
					break;
				}
				case uring_sock_udata_t::user_command::SEND_TIMEOUT:
				{
					std::printf("send timeout\n");
					next_send(ioring, ud->_sock);
					break;
				}
				case uring_sock_udata_t::user_command::SEND:
				{
					next_receive(ioring, ud->_sock);
					break;
				}
			}

			delete ud;
			io_uring_cqe_seen(&ioring, cqe);
		}
	}

	return 0;
}