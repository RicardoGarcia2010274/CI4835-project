CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11

all: servidor cliente

servidor: src/servidor.cpp
	$(CXX) $(CXXFLAGS) src/servidor.cpp -o servidor

cliente: src/cliente.cpp
	$(CXX) $(CXXFLAGS) src/cliente.cpp -o cliente

clean:
	rm -f servidor cliente

runservidor: 
	./servidor

runcliente: 
	./cliente