#ifndef SERVERCONFIG_HPP
# define SERVERCONFIG_HPP

# include <string>
# include <vector>
# include <map>

# include "Location.hpp"

struct ServerConfig
{
	std::string					_host;
	int							_port;					// exemple: 8080
	std::vector<std::string>	_serverNames;
	std::map<int, std::string>	_errorPages;
	size_t						_clientMaxBodySize;
	std::vector<Location>		_locations;

	ServerConfig();
};

#endif