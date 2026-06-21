#ifndef CONFIGPARSER_HPP
# define CONFIGPARSER_HPP

# include <string>
# include <vector>
# include <set>

# include "ServerConfig.hpp"

class ConfigParser
{
	private:
		std::vector<std::string>	_tokens;
		size_t						_index;

		std::string	readFile(const std::string &path) const;
		std::string	stripComments(const std::string &content) const;
		std::string	padSpecials(const std::string &content) const;
		void		tokenize(const std::string &content);

		bool		atEnd() const;
		std::string	peek() const;
		std::string	advance();
		void		expect(const std::string &token);

		ServerConfig	parseServer();
		void			parseServerDirective(ServerConfig &server, std::set<std::string> &seen);
		Location		parseLocation();
		void			parseLocationDirective(Location &location, std::set<std::string> &seen);

		std::vector<std::string>	collectArgs();
		void	parseListen(ServerConfig &server, const std::vector<std::string> &args);
		void	parseErrorPage(ServerConfig &server, const std::vector<std::string> &args);

		void	requireArgs(const std::vector<std::string> &args, size_t n,
						const std::string &directive) const;
		void	requireExactArgs(const std::vector<std::string> &args, size_t n,
						const std::string &directive) const;
		void	markSeen(std::set<std::string> &seen, const std::string &key) const;

		void	validateServers(const std::vector<ServerConfig> &servers) const;
		bool	sameEndpoint(const ServerConfig &a, const ServerConfig &b) const;
		bool	shareName(const ServerConfig &a, const ServerConfig &b) const;

		int		toInt(const std::string &value) const;
		int		toPort(const std::string &value) const;
		size_t	toSize(const std::string &value) const;
		std::string	sizeToStr(size_t n) const;

	public:
		ConfigParser();
		~ConfigParser();

		std::vector<ServerConfig>	parse(const std::string &path);
};

#endif