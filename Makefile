CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11
BUZON = buzon

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

limpiar_buzon: 
	rm -f $(BUZON)/*.txt