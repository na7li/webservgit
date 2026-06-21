NAME    = webserv
CC      = c++
CFLAGS  = -Wall -Wextra -Werror -std=c++98

SRCDIR  = src
OBJDIR  = obj

SRC     = $(SRCDIR)/main.cpp \
          $(SRCDIR)/Server.cpp \
          $(SRCDIR)/Connection.cpp \
          $(SRCDIR)/HttpRequest.cpp \
          $(SRCDIR)/ConfigParser.cpp \
          $(SRCDIR)/ServerConfig.cpp \
          $(SRCDIR)/Location.cpp
OBJ     = $(addprefix $(OBJDIR)/, $(notdir $(SRC:.cpp=.o)))

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(NAME)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
