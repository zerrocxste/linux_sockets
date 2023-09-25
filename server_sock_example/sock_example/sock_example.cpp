#include "sock_example.h"

constexpr auto MAX_CONNECTIONS = std::uint32_t(-1);

class ip_sock {
	std::int32_t sock;
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
	inline operator type() { return this->get(); }
};

template <std::size_t size>
using to_ch_string = to_string<char, size>;

int recv_(int sock, char* buf, int size)
{
	int resuidal_data_packet_size = 0;

	while (resuidal_data_packet_size < size)
	{
		auto recv_ret = recv(sock, buf + resuidal_data_packet_size, size - resuidal_data_packet_size, 0);

		if (recv_ret == -1)
		{
			std::printf("recv return %d\n", recv_ret);
			return -1;
		}

		if (recv_ret == 0)
			break;

		resuidal_data_packet_size += recv_ret;
	}

	return resuidal_data_packet_size;
}

int send_(int sock, char* buf, int size)
{
	int packet_sended_byte_count = 0;

	while (packet_sended_byte_count < size)
	{
		auto send_ret = send(sock, buf + packet_sended_byte_count, size - packet_sended_byte_count, 0);

		if (send_ret == -1)
		{
			std::printf("send return %d\n", send_ret);
			return -1;
		}

		if (send_ret == 0)
			break;

		packet_sended_byte_count += send_ret;
	}

	return packet_sended_byte_count;
}

int main(int argc, char** argv)
{
	std::printf("%s\n", argv[0]);

	auto port = 1488;

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

	std::fstream out_file(std::string(port_str.get()) + ".txt", std::ios::out | std::ios::ate);
	out_file.seekp(0, std::ios::beg);

	auto epoll_ret = epoll_create1(0);

	if (epoll_ret == -1)
	{
		std::printf("epoll_create return %d\n", epoll_ret);
		return 1;
	}

	epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = sock.get_sock();
	auto epoll_ctl_ret = epoll_ctl(epoll_ret, EPOLL_CTL_ADD, sock.get_sock(), &event);
	if (epoll_ctl_ret != 0)
	{
		std::printf("epoll_ctl return %d\n", epoll_ctl_ret);
		return 1;
	}

	int clients = 0;

	constexpr auto EVENT_MAX = 1;
	auto p_epoll_event = new epoll_event[EVENT_MAX];
	while (true) {

		std::memset(p_epoll_event, 0, sizeof(epoll_event) * EVENT_MAX);
		auto avalible_events = epoll_wait(epoll_ret, p_epoll_event, EVENT_MAX, -1);

		if (avalible_events == -1) {
			std::printf("epoll_wait return %d\n", avalible_events);
			break;
		}

		for (auto i = 0; i < avalible_events; i++) 
		{
			auto fd_sock = p_epoll_event[i].data.fd;

			if (fd_sock == sock.get_sock())
			{
				clients++;

				sockaddr_in sockaddrin_client;
				auto sockaddrin_client_length = sizeof(decltype(sockaddrin_client));
				auto client_sock = accept(sock.get_sock(), (sockaddr*)&sockaddrin_client, (socklen_t*)&sockaddrin_client_length);

				if (client_sock == -1) {
					std::printf("accept return %d\n", accept);
					continue;
				}

				epoll_event cl_event;
				event.events = EPOLLIN | EPOLLET;
				event.data.fd = client_sock;
				auto epoll_ctl_ret = epoll_ctl(epoll_ret, EPOLL_CTL_ADD, client_sock, &event);
				if (epoll_ctl_ret != 0)
				{
					std::printf("epoll_ctl return %d\n", epoll_ctl_ret);
					return 1;
				}

				std::printf("[+] New client %d\n", clients);
				continue;
			}

			char buf[128 + 2]{};
			auto received = recv_(fd_sock, buf, sizeof(buf) - 2);

			printf("received: %d\n", received);

			if (received <= 0)
			{
				clients--;

				auto epoll_ctl_ret = epoll_ctl(epoll_ret, EPOLL_CTL_DEL, fd_sock, nullptr);
				if (epoll_ctl_ret != 0)
				{
					std::printf("epoll_ctl return %d\n", epoll_ctl_ret);
					return 1;
				}

				printf("[-] Connecion dropped: %d\n", clients);
				continue;
			}

			std::printf("[+] New message \"%s\"\n", buf);

			auto buf_len = strlen(buf);
			*(buf + buf_len) = '\n';
			out_file.write(buf, buf_len + 1);

			char accepted_msg[] = "ACCEPTED";
			send_(fd_sock, accepted_msg, strlen(accepted_msg));

			usleep(3 * 1000000);
		}
	}

	return 0;
}
