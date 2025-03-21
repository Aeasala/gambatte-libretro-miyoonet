#include "net_serial.h"
#include "libretro.h"
#include "gambatte_log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

NetSerial::NetSerial()
: is_stopped_(true)
, is_server_(false)
, port_(12345)
, hostname_()
, server_fd_(-1)
, sockfd_(-1)
, fault_(NETSERIAL_NO_FAULT)
, faultHeartbeat_(true)
, lastConnectAttempt_(0)
{
	#ifdef _WIN32
	WORD wVersionRequested = MAKEWORD(2,2);
	WSADATA wsaData;
	WSAStartup(wVersionRequested, &wsaData);
	#endif
}

NetSerial::~NetSerial()
{
	stop();
	#ifdef _WIN32
	WSACleanup();
	#endif
}

bool NetSerial::start(bool is_server, int port, const std::string& hostname)
{
	stop();

	gambatte_log(RETRO_LOG_INFO, "Starting GameLink network %s on %s:%d\n",
			is_server ? "server" : "client", hostname.c_str(), port);
	is_server_ = is_server;
	port_ = port;
	hostname_ = hostname;
	is_stopped_ = false;
	return checkAndRestoreConnection(false);
}
void NetSerial::stop()
{
	if (!is_stopped_) {
		gambatte_log(RETRO_LOG_INFO, "Stopping GameLink network\n");
		is_stopped_ = true;
		if (sockfd_ >= 0) {
			shutdown(sockfd_,2);
			#ifdef _WIN32
			closesocket(sockfd_);
			#else
			close(sockfd_);
			#endif
			sockfd_ = -1;
		}
		if (server_fd_ >= 0) {
			shutdown(server_fd_,2);
			#ifdef _WIN32
			closesocket(server_fd_);
			#else
			close(server_fd_);
			#endif
			server_fd_ = -1;
		}
	}

	clock_t now = clock();
	while(((clock() - now) / CLOCKS_PER_SEC) < 1) {} // dwell a second
}
void NetSerial::resetFault()
{
	fault_ = NETSERIAL_NO_FAULT;
	faultHeartbeat_ = false;
	criticalFaultCooldown_ = false;
}

bool NetSerial::checkAndRestoreConnection(bool throttle)
{
	if (is_stopped_) {
		return false;
	}
	if(criticalFaultCooldown_) {
		clock_t now = clock();
		if (((now - timeSinceFault_) / CLOCKS_PER_SEC) < 20) {
			return false;
		}
		resetFault();
	}
	if (sockfd_ < 0 && throttle) {
		clock_t now = clock();
		// Only attempt to establish the connection every 5 seconds
		if (((now - lastConnectAttempt_) / CLOCKS_PER_SEC) < 5) {
			return false;
		}
	}
	lastConnectAttempt_ = clock();
	if (is_server_) {
		if (!startServerSocket()) {
			return false;
		}
		if (!acceptClient()) {
			return false;
		}
	} else {
		if (!startClientSocket()) {
			return false;
		}
	}
	return true;
}
void NetSerial::setFault(NetSerialFault_t fault)
{
	fault_ = fault;
	faultHeartbeat_ = true;
	if(fault != NETSERIAL_CONN_ERR)
	{
		criticalFaultCooldown_ = true;
		timeSinceFault_ = clock();
	}
	return;
}
bool NetSerial::startServerSocket()
{
	int fd;
	int retval;
	struct sockaddr_in server_addr;

	if (server_fd_ < 0) {
		memset((char *)&server_addr, '\0', sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(port_);
		server_addr.sin_addr.s_addr = INADDR_ANY;

		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error opening socket: %s, retval {%d}\n", strerror(errno), fd);
			#ifdef _WIN32
			gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
			#endif
			setFault(NETSERIAL_FD_ERR);
			return false;
		}

		int opt = 1;



		if ((retval = bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr))) < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error on binding: %s, retval {%d}\n", strerror(errno), retval);
			#ifdef _WIN32
			gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
			#endif
			shutdown(fd, 2);
			#ifdef _WIN32
			closesocket(fd);
			#else
			close(fd);
			#endif
			setFault(NETSERIAL_SOCK_ERR);
			return false;
		}
		#ifdef _WIN32
		#else
		gambatte_log(RETRO_LOG_DEBUG, "Sockopt retval %d\n", setsockopt(server_fd_,SOL_SOCKET,SO_REUSEPORT,(char *) &opt, sizeof(opt))); 
		gambatte_log(RETRO_LOG_DEBUG, "Sockopt retval %d\n", setsockopt(server_fd_,SOL_SOCKET,SO_REUSEADDR,(char *) &opt, sizeof(opt)));
		#endif
		if ((retval = listen(fd, 1)) < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error listening: %s, retval {%d}\n", strerror(errno), retval);
			#ifdef _WIN32
			gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
			#endif
			shutdown(fd, 2);
			#ifdef _WIN32
			closesocket(fd);
			#else
			close(fd);
			#endif
			setFault(NETSERIAL_SOCK_ERR);
			return false;
		}

		server_fd_ = fd;
		gambatte_log(RETRO_LOG_INFO, "GameLink network server started!\n");
		#ifdef _WIN32
		DWORD timeout = 2000;
		#else
		struct timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		#endif
		gambatte_log(RETRO_LOG_DEBUG, "Sockopt retval %d\n", setsockopt(server_fd_,SOL_SOCKET,SO_RCVTIMEO,(char *) &timeout, sizeof(timeout))); // 5 seconds.
		gambatte_log(RETRO_LOG_DEBUG, "Sockopt retval %d\n", setsockopt(server_fd_,SOL_SOCKET,SO_SNDTIMEO,(char *) &timeout, sizeof(timeout))); // 5 seconds.
		
		
	}
	return true;
}
bool NetSerial::acceptClient()
{
	struct sockaddr_in client_addr;
	struct timeval tv;
	fd_set rfds;

	/* Not a server, or not configured. */
	if (server_fd_ < 0) {
		return false;
	}

	/* Don't have an active client yet. */
	if (sockfd_ < 0) {
		int retval;

		FD_ZERO(&rfds);
		FD_SET(server_fd_, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		if (select(server_fd_ + 1, &rfds, NULL, NULL, &tv) <= 0) {
			return false;
		}

		socklen_t client_len = sizeof(client_addr);
		sockfd_ = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
		if (sockfd_ < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error on accept: %s, retval {%d}\n", strerror(errno), sockfd_);
			#ifdef _WIN32
			gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
			#endif
			setFault(NETSERIAL_SOCK_ERR);
			return false;
		}
		gambatte_log(RETRO_LOG_INFO, "GameLink network server connected to client!\n");
		#ifdef _WIN32
		DWORD timeout = 5000;
		#else
		struct timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		#endif
		gambatte_log(RETRO_LOG_DEBUG, "Sockopt retval %d\n", setsockopt(sockfd_,SOL_SOCKET,SO_RCVTIMEO,(char *) &timeout, sizeof(timeout))); // 5 seconds.
		gambatte_log(RETRO_LOG_DEBUG, "Sockopt retval %d\n", setsockopt(sockfd_,SOL_SOCKET,SO_SNDTIMEO,(char *) &timeout, sizeof(timeout))); // 5 seconds.
	
	}
	return true;
}
bool NetSerial::startClientSocket()
{
	int fd;
	int tmpRetVal;

	struct sockaddr_in server_addr;

	if (sockfd_ < 0) {
		memset((char *)&server_addr, '\0', sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(port_);

		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error opening socket: %s, retval {%d}\n", strerror(errno), fd);
			#ifdef _WIN32
			gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
			#endif
			setFault(NETSERIAL_FD_ERR);
			return false;
		}

		struct hostent* server_hostname = gethostbyname(hostname_.c_str());
		if (server_hostname == NULL) {
			gambatte_log(RETRO_LOG_ERROR, "Error, no such host: %s, retval {NULL}\n", hostname_.c_str());
			#ifdef _WIN32
			gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
			#endif
			setFault(NETSERIAL_CONN_ERR);
			shutdown(fd, 2);
			#ifdef _WIN32
			closesocket(fd);
			#else
			close(fd);
			#endif
			return false;
		}

		memmove((char*)&server_addr.sin_addr.s_addr, (char*)server_hostname->h_addr, server_hostname->h_length);
		if ((tmpRetVal = connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr))) < 0) {
			gambatte_log(RETRO_LOG_ERROR, "Error connecting to server: %s, retval {%d}\n", strerror(errno), tmpRetVal);
			#ifdef _WIN32
			gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
			#endif
			setFault(NETSERIAL_CONN_ERR);
			shutdown(fd, 2);
			#ifdef _WIN32
			closesocket(fd);
			#else
			close(fd);
			#endif
			return false;
		}
		sockfd_ = fd;
		gambatte_log(RETRO_LOG_INFO, "GameLink network client connected to server, with fd {%d}!\n", fd);
	}
	#ifdef _WIN32
	DWORD timeout = 5000;
	#else
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	#endif
	gambatte_log(RETRO_LOG_DEBUG, "Sockopt retval %d\n", setsockopt(sockfd_,SOL_SOCKET,SO_RCVTIMEO,(char *) &timeout, sizeof(timeout))); // 5 seconds.
	gambatte_log(RETRO_LOG_DEBUG, "Sockopt retval %d\n", setsockopt(sockfd_,SOL_SOCKET,SO_SNDTIMEO,(char *) &timeout, sizeof(timeout))); // 5 seconds.
	return true;
}

/* Send out serial data.  Attempts to re-establish connections. */
unsigned char NetSerial::send(unsigned char data, bool fastCgb)
{
	unsigned char buffer[2];
	int tmpRetVal;

	if (is_stopped_) {
		return 0xFF;
	}
	if (sockfd_ < 0) {
		if (!checkAndRestoreConnection(true)) {
			return 0xFF;
		}
	}

	buffer[0] = data;
	buffer[1] = fastCgb;

	// TODO: if attempting to write 0,0?  don't fault if we don't hear back from our friend.
	/* Attempt to write two bytes into the socket. */
#ifdef _WIN32
	if (::send(sockfd_, (char*) buffer, 2, 0) <= 0)
#else
	if (write(sockfd_, buffer, 2) <= 0)
#endif
	{
		gambatte_log(RETRO_LOG_ERROR, "Error writing to socket: %s\n", strerror(errno));
		#ifdef _WIN32
		gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
		#endif
		gambatte_log(RETRO_LOG_DEBUG, "\tAttempted to write: {%x, %x}\n", data, fastCgb);
		gambatte_log(RETRO_LOG_DEBUG, "\tBuffer contents: {%x, %x}\n", buffer[0], buffer[1]);
		setFault(NETSERIAL_SND_ERR);
		shutdown(sockfd_, 2);
		#ifdef _WIN32
		closesocket(sockfd_);
		#else
		close(sockfd_);
		#endif
		sockfd_ = -1;
		return 0xFF;
	}
	gambatte_log(RETRO_LOG_ERROR, "Wrote bytes: %x, %x\n", buffer[0], buffer[1]);


	// unrecoverable hang can occur here.  explore sock timeout options and an appropriate termination

	/* Attempt to read two bytes from the socket. */
#ifdef _WIN32
	if (recv(sockfd_, (char*) buffer, 2, 0) <= 0) 
#else
	if (read(sockfd_, buffer, 2) <= 0) 
#endif
	{
		/* debug */
		gambatte_log(RETRO_LOG_ERROR, "Error reading from socket: %s\n", strerror(errno));
		#ifdef _WIN32
		gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
		#endif

		/* ops */
		setFault(NETSERIAL_RCV_ERR);
		shutdown(sockfd_, 2);
		#ifdef _WIN32
		closesocket(sockfd_);
		#else
		close(sockfd_);
		#endif
		sockfd_ = -1;
		return 0xFF;
	}
	gambatte_log(RETRO_LOG_ERROR, "Read byte: %x\n", buffer[0]);
	return buffer[0];
}

/* Check for received serial data, send response.  Attempts to re-establish connections. */
bool NetSerial::check(unsigned char out, unsigned char& in, bool& fastCgb)
{
	unsigned char buffer[2];
#ifdef _WIN32
	u_long bytes_avail = 0;
#else
	int bytes_avail = 0;
#endif
	if (is_stopped_) {
		return false;
	}
	if (sockfd_ < 0) {
		if (!checkAndRestoreConnection(true)) {
			return false;
		}
	}
#ifdef _WIN32
	if (ioctlsocket(sockfd_, FIONREAD, &bytes_avail) < 0)
#else
	if (ioctl(sockfd_, FIONREAD, &bytes_avail) < 0)
#endif
	{
		gambatte_log(RETRO_LOG_ERROR, "IOCTL Failed: %s\n", strerror(errno));
		#ifdef _WIN32
		gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
		#endif
		setFault(NETSERIAL_IOCTL_ERR);
		shutdown(sockfd_, 2);
		#ifdef _WIN32
		closesocket(sockfd_);
		#else
		close(sockfd_);
		#endif
		sockfd_ = -1;
		return false;
	}

	// No data available yet
	if (bytes_avail < 2) {
		return false;
	}
	gambatte_log(RETRO_LOG_DEBUG, "Bytes remaining in socket: %d\n", bytes_avail);
#ifdef _WIN32
	if (recv(sockfd_, (char*) buffer, 2, 0) <= 0) 
#else
	if (read(sockfd_, buffer, 2) <= 0) 
#endif
	{
		/* debug */
		gambatte_log(RETRO_LOG_ERROR, "Error reading from socket: %s\n", strerror(errno));
		#ifdef _WIN32
		gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
		#endif

		/* ops */
		setFault(NETSERIAL_RCV_ERR);
		shutdown(sockfd_, 2);
		#ifdef _WIN32
		closesocket(sockfd_);
		#else
		close(sockfd_);
		#endif
		sockfd_ = -1;
		return false;
	}

	/* Write out the two received bytes into {in}, {fastCgb}: passed by reference */
	in = buffer[0];
	fastCgb = buffer[1];
	gambatte_log(RETRO_LOG_ERROR, "Read bytes: %x, %x\n", buffer[0], buffer[1]);

	/* Send 2 bytes. */
	buffer[0] = out; // data
	buffer[1] = 128; // transfer enable

#ifdef _WIN32
	if (::send(sockfd_, (char*) buffer, 2, 0) <= 0)
#else
	if (write(sockfd_, buffer, 2) <= 0)
#endif
	{
		/* debug */
		gambatte_log(RETRO_LOG_ERROR, "Error writing to socket: %s\n", strerror(errno));
		#ifdef _WIN32
		gambatte_log(RETRO_LOG_ERROR, "\tWSA code: %d\n", WSAGetLastError());
		#endif
		gambatte_log(RETRO_LOG_DEBUG, "\tAttempted to write: {%x, %x}\n", out, 128);
		gambatte_log(RETRO_LOG_DEBUG, "\tBuffer contents: {%x, %x}\n", buffer[0], buffer[1]);

		/* ops */
		setFault(NETSERIAL_SND_ERR);
		shutdown(sockfd_, 2);
		#ifdef _WIN32
		closesocket(sockfd_);
		#else
		close(sockfd_);
		#endif
		sockfd_ = -1;
		return false;
	}
	gambatte_log(RETRO_LOG_ERROR, "Wrote bytes: %x, %x\n", buffer[0], buffer[1]);

	return true;
}
NetSerialFault_t NetSerial::getFault()
{
	return fault_;
}
bool NetSerial::hasNewFault()
{
	bool currentHeartbeat = faultHeartbeat_;
	faultHeartbeat_ = false;
	return currentHeartbeat;
}