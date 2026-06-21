#ifndef LOCATION_HPP
# define LOCATION_HPP

# include <string>
# include <vector>

struct Location
{
	std::string					_path;
	std::string					_root;
	std::string					_index;
	bool						_autoindex;
	std::vector<std::string>	_allowedMethods;
	int							_redirectCode;		// 0 = no redirect, else 301/302...
	std::string					_redirectUrl;
	std::string					_uploadPath;
	std::string					_cgiExtension;
	std::string					_cgiPath;

	Location();
};

#endif