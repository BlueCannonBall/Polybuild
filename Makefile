CXXFLAGS := -Wall -std=c++17 -O3

default: polybuild
.PHONY: default

obj/main.o: ./main.cpp ./toml.hpp ./toml/parser.hpp ./toml/combinator.hpp ./toml/region.hpp ./toml/color.hpp ./toml/result.hpp ./toml/traits.hpp ./toml/from.hpp ./toml/into.hpp ./toml/version.hpp ./toml/utility.hpp ./toml/lexer.hpp ./toml/macros.hpp ./toml/types.hpp ./toml/comments.hpp ./toml/datetime.hpp ./toml/string.hpp ./toml/value.hpp ./toml/exception.hpp ./toml/source_location.hpp ./toml/storage.hpp ./toml/literal.hpp ./toml/serializer.hpp ./toml/get.hpp
	@echo -e '\033[1m[POLYBUILD]\033[0m Building $@ from $<...'
	@mkdir -p obj
	@$(CXX) -c $< $(CXXFLAGS) -o $@
	@echo -e '\033[1m[POLYBUILD]\033[0m Finished building $@ from $<!'

polybuild: obj/main.o
	@echo -e '\033[1m[POLYBUILD]\033[0m Building $@...'
	@$(CXX) $^ $(CXXFLAGS) $(LDLIBS) -o $@
	@echo -e '\033[1m[POLYBUILD]\033[0m Finished building $@!'

clean:
	@echo -e '\033[1m[POLYBUILD]\033[0m Deleting polybuild and obj...'
	@rm -rf polybuild obj
	@echo -e '\033[1m[POLYBUILD]\033[0m Finished deleting polybuild and obj!'
.PHONY: clean

install:
	@echo -e '\033[1m[POLYBUILD]\033[0m Copying polybuild to /usr/local/bin...'
	@cp polybuild /usr/local/bin
	@echo -e '\033[1m[POLYBUILD]\033[0m Finished copying polybuild to /usr/local/bin!'
.PHONY: install
