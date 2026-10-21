CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11

all: servidor cliente

servidor: servidor.cpp
	$(CXX) $(CXXFLAGS) servidor.cpp -o servidor

cliente: cliente.cpp
	$(CXX) $(CXXFLAGS) cliente.cpp -o cliente

clean:
	rm -f servidor cliente

runservidor: 
	./servidor

runcliente: 
	./cliente