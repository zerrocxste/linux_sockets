// client.cpp: определяет точку входа для приложения.
//

#include "client.h"

class ip_sock {
	std::int32_t sock;
public:
	ip_sock() { this->sock = socket(AF_INET, SOCK_STREAM, 0); }
	~ip_sock() { close(this->sock); }

	inline auto get_sock() { return this->sock; }
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
		usleep(3 * 1000000);
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

	usleep(1 * 1000000);

	char funny_message[] = "Ehal greka cherez reku";
	send_(sock.get_sock(), funny_message, strlen(funny_message));

	std::printf("Message sended!\n");

	usleep(1 * 1000000);

	char accepted_buffer[128 + 1]{};
	auto recv_ret = recv_(sock, accepted_buffer, sizeof(accepted_buffer) - 1);

	printf("recv_ret: %d\n", recv_ret);

	if (recv_ret <= 0)
	{
		printf("[-] Dropped server connection\n");
		break;
	}

	printf("Accept msg: \"%s\"\n", accepted_buffer);

	close(sock);

	return 0;
}
