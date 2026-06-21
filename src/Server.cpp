#include "../headers/Server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <stdexcept>

Server::Server(const std::vector<ServerConfig> &configs)
{
	openListeners(configs);
}

Server::~Server()
{
	std::map<int, Connection *>::iterator it = _connections.begin();
	while (it != _connections.end())
	{
		close(it->first);
		delete it->second;
		++it;
	}
	std::map<int, std::vector<ServerConfig> >::iterator lit = _listenConfigs.begin();
	while (lit != _listenConfigs.end())
	{
		close(lit->first);
		++lit;
	}
}

void Server::openListeners(const std::vector<ServerConfig> &configs)
{
	std::map<std::string, std::vector<ServerConfig> >	groups;

	for (size_t i = 0; i < configs.size(); ++i)
		groups[endpointKey(configs[i])].push_back(configs[i]);

	std::map<std::string, std::vector<ServerConfig> >::iterator it;
	for (it = groups.begin(); it != groups.end(); ++it)
	{
		const ServerConfig &endpoint = it->second[0];
		int fd = createListenSocket(endpoint._host, endpoint._port);
		_listenConfigs[fd] = it->second;
		watchFd(fd, POLLIN);
	}
}

std::string Server::endpointKey(const ServerConfig &config) const
{
	std::ostringstream	key;

	key << config._host << ":" << config._port;
	return key.str();
}

int Server::createListenSocket(const std::string &host, int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		throw std::runtime_error("socket() failed");

	int enable = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	makeNonBlocking(fd);

	struct sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(host.c_str());
	address.sin_port = htons(port);

	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
		throw std::runtime_error("bind() failed");
	if (listen(fd, 128) < 0)
		throw std::runtime_error("listen() failed");
	return fd;
}

bool Server::isListenSocket(int fd) const
{
	return _listenConfigs.find(fd) != _listenConfigs.end();
}

void Server::makeNonBlocking(int fd)
{
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		throw std::runtime_error("fcntl() failed");
}

void Server::watchFd(int fd, short events)
{
	struct pollfd entry;

	entry.fd = fd;
	entry.events = events;
	entry.revents = 0;
	_pollFds.push_back(entry);
}

void Server::start()
{
	while (true)
	{
		updateWriteInterest();
		if (poll(&_pollFds[0], _pollFds.size(), -1) < 0)
			throw std::runtime_error("poll() failed");
		processReadyFds();
		removeClosedConnections();
	}
}

void Server::updateWriteInterest()
{
	for (size_t i = 0; i < _pollFds.size(); ++i)
	{
		if (isListenSocket(_pollFds[i].fd))
			continue;
		_pollFds[i].events = POLLIN;
		if (_connections[_pollFds[i].fd]->hasDataToSend())
			_pollFds[i].events |= POLLOUT;
	}
}

void Server::processReadyFds()
{
	for (size_t i = 0; i < _pollFds.size(); ++i)
	{
		short	happened = _pollFds[i].revents;
		int		fd = _pollFds[i].fd;

		if (happened == 0)
			continue;
		if (isListenSocket(fd))
		{
			acceptConnection(fd);
			continue;
		}
		if (happened & (POLLHUP | POLLERR | POLLNVAL))
		{
			scheduleClose(fd);
			continue;
		}
		if ((happened & POLLIN) && !_connections[fd]->receive())
		{
			scheduleClose(fd);
			continue;
		}
		if ((happened & POLLOUT) && !_connections[fd]->sendData())
			scheduleClose(fd);
	}
}

void Server::acceptConnection(int listenFd)
{
	int fd = accept(listenFd, NULL, NULL);

	if (fd < 0)
		return;
	makeNonBlocking(fd);
	_connections[fd] = new Connection(fd);
	watchFd(fd, POLLIN);
}

void Server::scheduleClose(int fd)
{
	_closedFds.push_back(fd);
}

void Server::removeClosedConnections()
{
	for (size_t i = 0; i < _closedFds.size(); ++i)
		closeConnection(_closedFds[i]);
	_closedFds.clear();
}

void Server::closeConnection(int fd)
{
	if (_connections.find(fd) == _connections.end())
		return;

	close(fd);
	delete _connections[fd];
	_connections.erase(fd);
	for (size_t i = 0; i < _pollFds.size(); ++i)
	{
		if (_pollFds[i].fd == fd)
		{
			_pollFds.erase(_pollFds.begin() + i);
			break;
		}
	}
}
