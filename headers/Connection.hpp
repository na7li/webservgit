#ifndef CONNECTION_HPP
# define CONNECTION_HPP

# include <string>

# include "HttpRequest.hpp"

class Connection
{
	private:
		int			_fd;
		std::string	_incoming;
		std::string	_outgoing;
		bool		_responseReady;
		HttpRequest	_request;

		Connection(const Connection &other);
		Connection &operator=(const Connection &other);

		void	prepareResponse();
		void	prepareError(int code, const std::string &reason);

	public:
		Connection(int fd);
		~Connection();

		bool	hasDataToSend() const;
		bool	receive();
		bool	sendData();
};

#endif
