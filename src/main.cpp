#include "../headers/Server.hpp"
#include "../headers/ConfigParser.hpp"

#include <iostream>
#include <string>
#include <exception>

int main(int argc, char **argv)
{
	std::string	path = (argc > 1) ? argv[1] : "config/default.conf";

	try
	{
		ConfigParser				parser;
		std::vector<ServerConfig>	configs = parser.parse(path);
		Server						server(configs);
		server.start();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}
