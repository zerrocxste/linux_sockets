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

		std::printf("socket return %d", sock.get_sock());
		return 1;
	}

	sockaddr_in sockaddrin;
	sockaddrin.sin_port = htons(port);
	sockaddrin.sin_addr.s_addr = INADDR_ANY;
	sockaddrin.sin_family = AF_INET;
	auto connect_ret = connect(sock, (const sockaddr*)&sockaddrin, sizeof(decltype(sockaddrin)));
	
	if (connect_ret != 0)
	{
		std::printf("connect return %d\n", connect_ret);
		return 1;
	}

	bool send_message = true;
	while (true)
	{	
		if (send_message)
		{
			usleep(10 * 1000000);
			const char* funny_msg = "Ehal greka cherez reku";
			send(sock, funny_msg, strlen(funny_msg), 0);
			send_message = false;
			std::printf("Message sended\n");
		}
		else
		{
			char buf[128 + 1]{};
			recv(sock, buf, sizeof(buf) - 1, 0);
			printf("Message from server: \"%s\"\n", buf);
			send_message = true;
		}
	}

	return 0;
}