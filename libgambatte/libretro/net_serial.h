#ifndef _NET_SERIAL_H
#define _NET_SERIAL_H

#if defined(__HAIKU__)
#include <sys/socket.h>
#include <sys/select.h>
#endif

#include <gambatte.h>
#include <time.h>

enum NetSerialFault_t {
	NETSERIAL_NO_FAULT	= 0,
	NETSERIAL_CONN_ERR	= 1,
	NETSERIAL_SND_ERR	= 2,
	NETSERIAL_RCV_ERR	= 3,
	NETSERIAL_IOCTL_ERR	= 4,
	NETSERIAL_FD_ERR	= 5,
	NETSERIAL_SOCK_ERR	= 6,
};

static const char *NetSerialFaultTextMap[] = {
	"No Fault",
	"Connection Error",
	"Send Error",
	"Receive Error",
	"ioctl Error",
	"fd Error",
	"Socket Error"
};

class NetSerial : public gambatte::SerialIO
{
	public:
		NetSerial();
		~NetSerial();

		bool start(bool is_server, int port, const std::string& hostname);
		void stop();
		void resetFault();

		virtual bool check(unsigned char out, unsigned char& in, bool& fastCgb);
		virtual unsigned char send(unsigned char data, bool fastCgb);
		NetSerialFault_t getFault();
		bool hasNewFault();

	private:
		bool startServerSocket();
		bool startClientSocket();
		bool acceptClient();
		bool checkAndRestoreConnection(bool throttle);
		void setFault(NetSerialFault_t fault);

		bool is_stopped_;
		bool is_server_;
		int  port_;
		std::string hostname_;

		int server_fd_;
		int sockfd_;

		bool faultHeartbeat_;
		bool criticalFaultCooldown_;
		NetSerialFault_t fault_;

		clock_t timeSinceFault_;
		clock_t lastConnectAttempt_;
};

#endif
