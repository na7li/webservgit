#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <vector>
# include <map>
# include <poll.h>

# include "Connection.hpp"
# include "ServerConfig.hpp"

class Server
{
	private:
		std::map<int, std::vector<ServerConfig> >	_listenConfigs;
		std::vector<struct pollfd>					_pollFds;
		std::map<int, Connection *>					_connections;
		std::vector<int>							_closedFds;

		Server(const Server &other);
		Server &operator=(const Server &other);

		void		openListeners(const std::vector<ServerConfig> &configs);
		std::string	endpointKey(const ServerConfig &config) const;
		int			createListenSocket(const std::string &host, int port);
		bool		isListenSocket(int fd) const;
		void		makeNonBlocking(int fd);
		void		watchFd(int fd, short events);
		void		updateWriteInterest();
		void		processReadyFds();
		void		acceptConnection(int listenFd);
		void		scheduleClose(int fd);
		void		removeClosedConnections();
		void		closeConnection(int fd);

	public:
		Server(const std::vector<ServerConfig> &configs);
		~Server();

		void	start();
};

#endif
