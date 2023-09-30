#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include <fstream>
#include <string>
#include <chrono>

#include <liburing.h>

auto remove_file(const char* filename)
{
	std::remove(filename);
}

class write_fs
{
	std::fstream fs;
public:
	write_fs(const char* filename, std::ios_base::openmode mode)
	{
		fs = std::fstream(filename, mode);
		fs.seekp(0, std::ios::beg);
	}

	write_fs& write_string(const char* string, std::size_t str_length = 0)
	{
		this->fs.write(string, str_length == 0 ? strlen(string) : str_length);
		return *this;
	}
};

template <class chrono_time_type>
auto ex_sleep(std::uint64_t time)
{
	using namespace std::chrono;
	auto timepoint = system_clock::now();
	while (duration_cast<chrono_time_type>(system_clock::now() - timepoint).count() < time);
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

void sleep_ms(std::uint64_t ms)
{
	auto tm = std::chrono::system_clock::now();
	while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - tm).count() < ms);
}

int main(int argc, char** argv)
{
	std::printf("%s\n", argv[0]);

	auto port = 14880;

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

	int sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	const int val = 1;
	setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	
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

	std::remove(output_filename.c_str());

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
			DELAY_SEND,
			SEND,
			MAX_SIZE_CMD
		} 
		_ucmd;
		int _sock;
		void* _context_data;
		std::chrono::system_clock::time_point _timestamp;

		uring_sock_udata_t(user_command ucmd, int sock, void* context_data = nullptr, std::chrono::system_clock::time_point _timestamp = {}) : 
			_ucmd(ucmd), _sock(sock), _context_data(context_data), _timestamp(_timestamp)
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

	auto generate_receive = [](io_uring& ioring, int sock) -> void
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

	auto next_send = [](io_uring& ioring, int sock) -> void
	{
		io_uring_sqe_set_data(io_uring_get_sqe(&ioring), new uring_sock_udata_t{ uring_sock_udata_t::DELAY_SEND, sock, nullptr, std::chrono::system_clock::now() });
		io_uring_submit(&ioring);
	};

	auto accepted_message = [](io_uring& ioring, int sock) -> void
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

			auto seen = true;

			switch (ud->_ucmd)
			{
			case uring_sock_udata_t::user_command::ACCEPT:
				next_accept(ioring, sock);
				generate_receive(ioring, cqe->res);
				std::printf("new client\n");
				break;
			case uring_sock_udata_t::user_command::RECEIVE:
			{
				if (ud->_context_data == nullptr)
				{
					next_receive(ioring, ud->_sock);
					break;
				}

				if (ud->_context_data && cqe->res <= 0)
				{
					std::printf("disconnected client\n");
					shutdown(ud->_sock, SHUT_RDWR);
					break;
				}		

				if (auto msg = (char*)ud->_context_data)
				{
					auto msg_len = strlen(msg);

					std::printf("Msg length: %d Msg: \"%s\"\n", msg_len, msg);

					write_fs(output_filename.c_str(), std::ios::app)
					.write_string(msg, msg_len)
					.write_string("\n");

					delete ud->_context_data;
				}

				next_send(ioring, ud->_sock);
				break;
			}
			case uring_sock_udata_t::user_command::DELAY_SEND:
			{
				auto time_point = std::chrono::system_clock::now();
				if (std::chrono::duration_cast<std::chrono::seconds>(time_point - ud->_timestamp).count() >= 3)
					accepted_message(ioring, ud->_sock);
				else
					seen = false;

				break;
			}
			case uring_sock_udata_t::user_command::SEND:
				next_receive(ioring, ud->_sock);
				break;
			}

			if (seen)
			{
				delete ud;
				io_uring_cqe_seen(&ioring, cqe);
			}
		}
	}

	return 0;
}