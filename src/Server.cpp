#include "../headers/Server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <signal.h>
#include <cerrno>
#include <iostream>

static volatile sig_atomic_t running = 1;
static void stopServer(int)
{
    running = 0;
}

Server::Server(const std::vector<ServerConfig> &configs)
{
    running = 1;
    _closedFds.reserve(1024);
    _pollFds.reserve(2048);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, stopServer);
    signal(SIGTERM, stopServer);
    try
    {
        openListeners(configs);
    }
    catch (...)
    {
        for (std::map<int, std::vector<ServerConfig> >::iterator it = _listenConfigs.begin();
             it != _listenConfigs.end(); ++it)
            close(it->first);
        throw;
    }
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
    std::map<std::string, std::vector<ServerConfig> > groups;

    for (size_t i = 0; i < configs.size(); ++i)
        groups[endpointKey(configs[i])].push_back(configs[i]);

    std::map<std::string, std::vector<ServerConfig> >::iterator it;
    for (it = groups.begin(); it != groups.end(); ++it)
    {
        const ServerConfig &endpoint = it->second[0];
        int fd = createListenSocket(endpoint._host, endpoint._port);
        try
        {
            _listenConfigs[fd] = it->second;
        }
        catch (...)
        {
            close(fd);
            throw;
        }
        watchFd(fd, POLLIN);
    }
}

std::string Server::endpointKey(const ServerConfig &config) const
{
    std::ostringstream key;

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
    try
    {
        makeNonBlocking(fd);
    }
    catch (...)
    {
        close(fd);
        throw;
    }

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    unsigned long ip = 0;
    std::istringstream hostParts(host);
    std::string octet;
    while (std::getline(hostParts, octet, '.'))
    {
        unsigned value = 0;
        std::istringstream number(octet);
        number >> value;
        ip = (ip << 8) | value;
    }
    address.sin_addr.s_addr = htonl(ip);
    address.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        close(fd);
        throw std::runtime_error("bind() failed");
    }
    if (listen(fd, 128) < 0)
    {
        close(fd);
        throw std::runtime_error("listen() failed");
    }
    return fd;
}

bool Server::isListenSocket(int fd) const
{
    return _listenConfigs.find(fd) != _listenConfigs.end();
}

void Server::makeNonBlocking(int fd)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
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
    while (running)
    {
        Cgi::reap();
        for (std::map<int, Connection *>::iterator it = _connections.begin();
             it != _connections.end(); ++it)
        {
            try
            {
                if (!it->second->tick())
                    scheduleClose(it->first);
            }
            catch (...)
            {
                scheduleClose(it->first);
            }
        }
        removeClosedConnections();
        updateWriteInterest();
        int ready = poll(&_pollFds[0], _pollFds.size(), 100);
        if (ready < 0)
        {
            if (!running || errno == EINTR)
                continue;
            throw std::runtime_error("poll() failed");
        }
        if (ready > 0)
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
        _pollFds[i].events = _connections[_pollFds[i].fd]->wantsRead() ? POLLIN : 0;
        if (_connections[_pollFds[i].fd]->hasDataToSend())
            _pollFds[i].events |= POLLOUT;
    }
}

void Server::processReadyFds()
{
    size_t count = _pollFds.size();
    for (size_t i = 0; i < count; ++i)
    {
        short happened = _pollFds[i].revents;
        int fd = _pollFds[i].fd;
        if (!happened)
            continue;
        if (isListenSocket(fd))
        {
            if (happened & POLLIN)
                acceptConnection(fd);
            continue;
        }
        if (happened & (POLLERR | POLLNVAL))
        {
            scheduleClose(fd);
            continue;
        }
        try
        {
            if ((happened & POLLIN) && _connections[fd]->wantsRead() &&
                !_connections[fd]->receive())
            {
                scheduleClose(fd);
                continue;
            }
            if ((happened & POLLOUT) && _connections[fd]->hasDataToSend() &&
                !_connections[fd]->sendData())
            {
                scheduleClose(fd);
                continue;
            }
            if ((happened & POLLHUP) && !(happened & POLLIN))
                scheduleClose(fd);
        }
        catch (...)
        {
            scheduleClose(fd);
        }
    }
}

void Server::acceptConnection(int listenFd)
{
    struct sockaddr_in peer;
    socklen_t size = sizeof(peer);
    int fd = accept(listenFd, reinterpret_cast<struct sockaddr *>(&peer), &size);
    if (fd < 0)
        return;
    if (_connections.size() >= 1024)
    {
        close(fd);
        return;
    }
    Connection *connection = NULL;
    try
    {
        makeNonBlocking(fd);
        unsigned long ip = ntohl(peer.sin_addr.s_addr);
        std::ostringstream remote;
        remote << ((ip >> 24) & 255) << '.' << ((ip >> 16) & 255) << '.' << ((ip >> 8) & 255) << '.'
               << (ip & 255);
        connection = new Connection(fd, _listenConfigs[listenFd], remote.str());
        _connections[fd] = connection;
        watchFd(fd, POLLIN);
    }
    catch (...)
    {
        _connections.erase(fd);
        delete connection;
        close(fd);
    }
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
