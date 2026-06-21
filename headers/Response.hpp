#ifndef RESPONSE_HPP
# define RESPONSE_HPP

# include <string>
# include <vector>

# include "HttpRequest.hpp"
# include "ServerConfig.hpp"

class Response
{
	private:
		Response();
		Response(const Response &other);
		Response &operator=(const Response &other);

	public:
		static std::string	build(const HttpRequest &request,
								  const std::vector<ServerConfig> &configs);
};

#endif